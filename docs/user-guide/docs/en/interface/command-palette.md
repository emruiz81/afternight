---
kind: interface
summary: >-
  The command palette finds every process, open image, recent file, menu command and setting by name: press Ctrl+K,
  type a few letters and press Enter.
menu: View > Command Palette...
---
# Command Palette

The [command palette](../reference/glossary.md#command-palette) finds every command of AfterNight by name: the processes, the open images and the recent files,
the commands of the menus and the places of the [rail](../reference/glossary.md#rail), and every page and row of [Settings](settings-dialog.md). Press
++ctrl+k++ or click the field in the top bar, type a few letters, and press ++enter++ on what you want.

```figure
image: ../assets/images/interface/command-palette.webp
alt: >-
  The main window with the command palette open over it. In the top bar, the field Search or run a command with the
  keys Ctrl K. Under it, over the image and the readout, a dark panel: a magnifier and the word stretch typed, with an
  Esc key at the right; below it, under the heading Processes, ArcSinh Stretch, selected, and Statistical Stretch,
  each with Intensity Transformations at its right; and along its foot, keys with words: Up and Down navigate, Enter
  run, Esc close, then the signs greater-than for processes, at for images and colon for settings, and three dots with
  the words all menus.
caption: >-
  The command palette with stretch typed: the processes whose names hold it, the first one selected.
pins:
  - {n: 1, x: 36.1, y: 4.8}
  - {n: 2, x: 26.3, y: 11.4}
  - {n: 3, x: 26.3, y: 16.1}
  - {n: 4, x: 26.3, y: 26.7}
  - {n: 5, x: 50.6, y: 26.7}
  - {n: 6, x: 69.6, y: 26.7}
```

## What you see

1. **The field.** **Search or run a command**, in the top bar, opens the palette.
2. **The query.** Where you type, under **Type a command, process or file...**. The key at its right is ++esc++, which
   closes the palette.
3. **The list.** What you typed finds, in groups: **Processes**, **Images**, **Views** and **Settings**. With nothing
   typed, your favourite processes and the commands you ran last; "No command matches" when there is nothing to list.
4. **The keys.** ++up++ and ++down++ move through the list, ++enter++ runs the row selected, ++esc++ closes.
5. **The prefixes.** `>` before what you type searches the processes only, `@` the images, `:` the settings.
6. **All menus.** Opens every menu of the menu bar, under the top bar's field.

## What you can do

### Run a command

1. **Open the palette.** Press ++ctrl+k++, click **Search or run a command**, or choose **View > Command
   Palette...**. The palette opens under the top bar and dims what is behind it.
2. **Type part of a name.** `hist` finds Histogram Transformation, and `ht` finds it too. Each row shows its name, where
   it is - a menu path, a category, a folder - and its shortcut.
3. **Run it.** Press ++enter++, or click the row. The palette closes and the command runs: a process opens as a
   [task](tasks.md) on the [active image](../reference/glossary.md#active-image), an open image becomes the active one,
   a recent file opens, and a row of Settings opens its page.

A row that cannot run now is dimmed and says why where its category would be, such as
"This process supports still images only, not sequences." ++enter++ does nothing on it. A process chosen with no image
open says so as its menu entry does.

### Know what it finds

- **Processes:** every process, by the name its [task](../reference/glossary.md#task) shows, with its category. A process an extension adds shows its
  extension's name and the tag **extension**, which runs in its own process.
- **Images:** each open image by its name, with its file; **Open file...**; and each recent file, with its folder.
- **Views:** every other command of the menus - the windows, the console, the layout, the [workspaces](../reference/glossary.md#workspace), the tasks, the
  [projects](../reference/glossary.md#project), **Home**, **Integration**, **Night** and **Density** - with its menu path.
- **Settings:** each page of Settings, and each of its rows as page and row, such as **Interface > Density**. The row
  opens its page.

A name that starts with what you type comes first. Then come the names whose words start with your words in order, and
the ones a category or a menu path finds. Last come names that hold your letters in order. Case does not matter. Each
kind of match lists the commands you ran most recently first, then the rest by name.

### Narrow the search

Each group shows six rows, and its header counts the rest, as "4 more": click it to show them all. Start with `>`, `@`
or `:` to search one group, shown whole; the sign alone lists that whole group. ++tab++ and ++shift+tab++ jump to the
first row of the next or the previous group.

### Keep favourites at hand

A star shows at the right of a process's row while it is selected or under the pointer. Click it to make the process a
favourite, or a favourite no longer. With nothing typed, the palette lists your favourites first, by name, then the
last eight commands you ran. The favourites are those of **Process > Favorite Processes**, and the palette remembers
the last twenty commands between sessions.

## Controls

```controls
window: Command palette
controls:
  - name: Type a command, process or file...
    kind: field
    default: ""
    effect: Searches every command as you type. A first `>`, `@` or `:` narrows the search to the processes, the images or the settings.
    enabled: Empty each time the palette opens.
    note: ++up++, ++down++, ++page-up++, ++page-down++, ++home++ and ++end++ move the selection; ++tab++ and ++shift+tab++ go to the next or the previous group, round from the last to the first; ++enter++ runs; ++esc++ or a click outside the palette closes it.
  - name: Command row
    kind: button
    default: ""
    effect: Runs the command, closing the palette first. The command joins the ones run last.
    range: Its name, its tag for an extension's process, where it is and its shortcut. A row that cannot run now is dimmed, with the reason where its menu path would be, and does not run.
    note: The list is read again each time the palette opens, so it holds the images open and the processes available then.
  - name: Favourite
    kind: toolbutton
    default: "Off"
    effect: The star of a process's row. On, the process is a favourite, listed first when nothing is typed and in **Process > Favorite Processes**.
    enabled: On a process's row, shown while the row is selected or under the pointer, and always on a favourite.
    persistence: The same list as **Process > Favorite Processes**, kept between sessions.
  - name: Group header
    kind: button
    default: ""
    effect: Shows every row of its group. It counts the rows not shown, as "4 more".
    range: Six rows show until it is clicked. A search narrowed by its first sign shows its group whole.
  - name: All menus
    kind: button
    default: ""
    effect: Closes the palette and opens every menu of the menu bar under the top bar's field, as the top bar's **All menus** does.
  - name: No command matches
    kind: readout
    default: ""
    effect: Stands in the list when nothing matches what you typed, and when nothing is typed while you have no favourite and have run no command yet.
```

## When it goes wrong

The palette says "No command matches" before you type
:   **Cause.** With nothing typed it lists your favourites and the commands you ran, and you have none yet.
:   **Fix.** Type part of a name.

A row is dimmed and ++enter++ does nothing
:   **Cause.** The command cannot run now: the active image is of the wrong kind, a task already holds it, or the
    command is greyed in its menu too. The row says why.
:   **Fix.** Make the right image the active one, or close the task that holds it, and run the command again.

A process you know is not found
:   **Cause.** A first `@` or `:` limits the search to another group, or the letters typed are not in its name in
    that order.
:   **Fix.** Take the sign away, or start with `>` and type the start of one of its words.

++ctrl+k++ does nothing
:   **Cause.** Another window has the keyboard focus: a dialog, or a task floating in a window of its own.
:   **Fix.** Click the main window first, or click **Search or run a command**.

## Related

- [Top Bar and Menus](top-bar-and-menus.md), for the field, **All menus** and the menus the palette searches.
- [The Main Window](overview.md), for the places and regions the palette's commands show.
- [Tasks](tasks.md), for the page a process opens as.
- [Settings](settings-dialog.md), for the pages and rows the palette finds.
- [Process Catalog](../processes/index.md), for every process by the stage it belongs to.
- [Keyboard Shortcuts](../reference/keyboard-shortcuts.md), for every key by window.
