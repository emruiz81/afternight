# Capturing the guide's pictures

> **Audience:** whoever takes or retakes a picture of the application for the user guide.
>
> **Source of truth:** `build/scripts/tools/capture_guide_screenshots.ps1` and
> `build/scripts/tools/optimize_guide_image.py`, whose headers say everything they do, and the recipes under
> `docs/user-guide/captures/`. What a picture must be - its format, its name, the state of the window, pins, pairs and
> provenance - is "Media" in [docs/dev/USER_GUIDE_AUTHORING.md](../dev/USER_GUIDE_AUTHORING.md); this file is how to
> make one.

A picture of the application is taken from the application itself by a script, so that two people on two machines get
the same picture: the same theme, preset, density and scale, the same locale, a profile nobody has used, the window at
the size the guide's pictures have. The script exists because the harness that took the README's pictures lived in a
scratch folder and was lost, and what it had learned about driving the application was learned again. The pictures
are described once, as recipes, so that the whole guide can be taken again when the window changes.

## What you need

- **A deployed Release build** on Windows: `bin\msvc\release\AfterNight.exe`, built at the commit the picture is of.
- **Windows PowerShell 5.1** (`powershell.exe`), which carries UI Automation. The capture script does not run under
  PowerShell 7.
- **Pillow, in the documentation environment.** The build helper makes `docs\user-guide\.venv`; Pillow is pinned
  beside it and installed by hand, once, by whoever captures. It is never installed in continuous integration:

  ```cmd
  docs\user-guide\.venv\Scripts\python -m pip install --require-hashes -r docs\user-guide\requirements-capture.txt
  ```

- **The data.** The scripts take the files as arguments, and no path on anyone's machine is committed. A recipe names
  its data by a role of `captures/roles.json`, which says what each role is:

  | Role | For | Data |
  | --- | --- | --- |
  | `linear` | Color, noise, stretch and finishing pages, the interface pages | A stacked RGB master that was never stretched |
  | `flat` | A second image beside the first: workspaces, docking | A master flat of the same set |
  | `ngc253`, `eagle` | Calibration, registration and stacking pages, the workflows | The color-camera and narrowband sets the workflow pages name |
  | `cfa` | The CFA tools | A raw color-filter frame of the color-camera set |
  | `synthetic` | A tool that needs no sky | A mono frame made for the purpose |

  A stretched master is the linear master after the stretch `-Prepare` applies in the run, so the data stays one
  file. The data is the maintainer's own, or data the project may publish.

## The window the pictures show

The script starts the application with a storage root of its own, which it removes afterwards: the dark theme, the
default preset and the Compact density a new profile starts with, the menu bar hidden until Alt, in the `en_US` locale
and without display scaling. It gives the main window a place and a size, 1440 x 900, before anything else.

A process opens as a **task**, a page of the main window, and its picture is that page: the task's header, the canvas
at the left and the column at the right, cropped to them. The rail and the top bar are in a picture only on a page about
them. What else a picture shows is said by `-Crop` and `-MainCrop`:

| Part | Shows |
| --- | --- |
| `task` | A task's page: its header, canvas and column (the default for a task) |
| `column` | A task's column alone |
| `canvas` | A task's canvas alone |
| `view` | The image of the image view the main window shows, without the margins beside it |
| `window` | The whole main window, frame and title bar included |
| an object name | Any part of the main window: `observatoryRightColumn` (the readout), `observatoryRail`, `observatoryTopBar`, `observatoryCentre` (the centre), `observatoryStatusLine` |

A dialog is taken as its own window; a task floated in a window of its own (`-Float`) is taken as that window.

## Capture

```cmd
powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\tools\capture_guide_screenshots.ps1 ^
    -Application bin\msvc\release\AfterNight.exe -Output <a folder outside the repository> ^
    -Open <the linear master> -Display Autostretch ^
    -Page processes\stretching\histogram-transformation.md -Set "shadowsValue=0.02;midtonesValue=0.02"
```

- `-Page` takes a page of the guide that states a `menu`: a process page, or a tutorial that works in one task. The
  script reads the page's `menu`, chooses it from the menu bar - shown by Alt, as a person shows it - and names the
  picture after the page.
- `-Capture "<name>=<menu path>"` takes any menu path instead, and whatever it opens: a task, cropped as `-Crop` says,
  or a dialog; several are separated by semicolons. `-Capture "<name>=@<button>"` presses a button of the main window
  instead, by the name it shows: a view's Autostretch Settings, which no menu opens.
- `-View "<menu path>;..."` chooses menu items that act at once, after `-Prepare` and before the captures: **Image >
  View > Zoom 100%** for a pair at 100 %, **View > Session Console** to open the console. `@<button>` presses a button
  of the main window there too, or chooses an item of the rail - `@Integration`, `@Home`. `%display=<mode>` chooses the
  active view's display mode, `%middle` puts the scroll bars at their middle, and `%shot=<name>[@<part>]` saves the
  main window there and then. `@<button>|<control>=<value>`, several joined by `&`, opens a dialog by a button of the
  main window, changes it and closes it: `@Autostretch Settings|linkedChannels=On`. `-AfterView` takes all of them,
  and is done once the captures are done.
- `-Open` opens the images the pictures need, in order, through **File > Open**. A raw color-filter frame asks
  whether to debayer it on opening; `-OpenAnswer No` keeps the mosaic.
- `-Display Autostretch` shows the opened image stretched, by the display mode of its floating toolbar. A linear
  master is nearly black without it, and the preview of most processes follows the view.
- `-MainWindow <name>` adds a picture of the main window as it is at the end, cropped to `-MainCrop`. An image the
  captures left open - the master a run produced - is shown as `-Display` says first.
- `-MenuShot "<name>=<menu path>"` adds a picture of the main window with the menu bar shown, the menus of the path
  open and the pointer on its last item. Each open menu is drawn by itself and laid over the main window's picture, so
  the screen may be locked while it is taken.
- `-Left`, `-Top`, `-Width` and `-Height` place the main window.
- `-List` prints the controls of each task or dialog it captures: their type, their object name and the name they show.
- `-HoldSeconds <n>` waits before a picture: a preview that runs long on one thread - Patch PCA Denoise,
  GRAYCstoration - can look settled while it still works.
- `-WhatIf` lists what the run would save, and starts nothing.

The script waits for the window to stop changing before it saves a picture, and says when it was still changing after
`-SettleSeconds`. It leaves one PNG per capture in `-Output` and stops the application. It exits 1 when a capture was
not saved.

Look at every capture before going on: the script cannot tell a window in the wrong state from one in the right one.

## The task at work, and the pair

A task as it opens often shows nothing of what it is for: a stretch at its defaults leaves a linear master black. A
page's opening picture shows the task in the state of the page's worked example, and the worked example of a process
judged by its result ends with the image before and after. One run takes all three:

```cmd
powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\tools\capture_guide_screenshots.ps1 ^
    -Application bin\msvc\release\AfterNight.exe -Output <a folder outside the repository> ^
    -Open <the linear master> -Page processes\stretching\histogram-transformation.md ^
    -Set "shadowsValue=0.02;midtonesValue=0.02" -Apply
```

- `-Set` does in the task what the worked example says, in order, before the picture is taken. A control is named by
  its object name, as the window's inventory under `data/ui/` records it, or by the name it shows. The task's controls
  are looked for in its page only:

  | Written | Does |
  | --- | --- |
  | `shadowsValue=0.02` | Sets a spin box or a slider to a number, written with a decimal point. |
  | `channelSelector=Red` | Chooses an item of a combo box. |
  | `preview=Off` | Switches a check box or a checkable button `On` or `Off`. |
  | `liteOutputDirectoryEdit=C:\stack` | Types a text into a field. |
  | `autostretchLinked` | Presses a button. |
  | `curveEditor@25,40` | Clicks in a control the application draws - a curve, a plot - at 25 % of its width and 40 % of its height from its top left corner. |
  | `curvesEditorScrollArea@28,69>28,82` | Drags in such a control, from the first place to the second: what adds a point to a curve and moves it. |
  | `liteAddFolderButton?C:\frames\lights` | Presses a button that opens the system's file or folder dialog, answers it with the path and waits for what is loaded. Several files are written as the dialog takes them: `"C:\a.fits" "C:\b.fits"`. |
  | `liteRunIntegrationButton!` | Presses a button that starts a run and waits until the button is enabled again, up to `-RunTimeoutSeconds`. |
  | `spccProcessAssignImageStateButton^Yes` | Presses a button that opens a message or a dialog, and answers it with the button of that name, in the first new window that has one. |
  | `%wait=Complete` | Waits, up to `-RunTimeoutSeconds`, for a run to end with a window whose title begins so, and answers it with OK; a window titled "Failed" or "Completed with Errors" stops the capture. |
  | `%shot=tasks@window` | Saves the main window, cropped to a part, while the task is still open: what a task draws over the image, or the task in its window. |
  | `%until=btnOpenReport` | Waits, up to `-RunTimeoutSeconds`, until the control is shown and enabled: how a run on a page - Image Integration's - says it has ended. |
  | `%keys=Tab,Down,Down` | Posts keys to the window, without UI Automation: what a dialog too large for it - Settings, while it is modal - takes. |
  | `%type=Gauss` | Types a text into what has the keyboard: the command palette's field, a dialog's search. |

- `-Apply` takes the pair, from the image's own view. Before the task opens it saves the upper two thirds of the view,
  clear of its floating toolbar, as `<name>-before.png`; after the task's picture it presses the task's **Apply**, waits
  for the run to end, closes the task and saves the same part of the view as `<name>-after.png`. The image is in the
  same place at the same zoom in both, under the display mode it was opened in. Open a linear master without
  `-Display` for a stretch: the pair then shows the image as it is, black before and visible after.
- `-Prepare "<menu path>|<control>=<value>;..."` runs a process on the image before any capture: it opens the task, sets
  it as `-Set` does, applies it and closes it. A page of a process that works on a stretched image names the stretch in
  its worked example, and is captured with it:

  ```cmd
      -Prepare "Process > Intensity Transformations > Histogram Transformation|shadowsValue=0.02;midtonesValue=0.02" ^
      -Page processes\stretching\gamma-correction.md -Set "gammaValue=1.5" -Apply
  ```

`-Set` applies to every task of the run, so a page is captured in a run of its own. What a run leaves on the screen -
the message that it is complete - is dismissed before its task is closed, and an image it opened stays for
`-MainWindow`.

- **Several preparations** go in one `-Prepare`, separated by `||`, and run in that order. A task with OK in place of
  Apply is applied with OK; a preparation whose last change runs it already (`OK!`) is not applied again, and one with
  neither Apply nor OK - a tool that saves a file - does only what its changes say.
- **A dialog a button of the main window opens** is prepared as `"@<button>|<control>=<value>"`: its changes act at
  once and it is closed. A color calibration's pair needs `"@Autostretch Settings|linkedChannels=On"`, since the
  view's Autostretch stretches each channel on its own unless its channels are linked, and so hides a color change.
- **A process that needs a tool's path** - Starnet++ and StarNet2 - is captured in a profile that knows it: a run
  with `-Storage <new folder>` keeps its storage root, its settings database (`afternight_data.db`, table
  `global_settings`) takes the path under the key the External Tools page writes (`tools/starnetPath`), and later
  runs reuse that root with `-Storage <folder> -ReuseStorage`.
- **A run that makes a new image** - a recombination, a mask - is paired with that image: `-AfterView` chooses its
  view and saves it, such as `"@RGB - starless...;%shot=starnet-after@view"`. `@<name>` chooses a view's tab as it
  chooses a button, and a name ending in `...` is the beginning of the name, since a tab adds ` *` while its image is
  unsaved.

A process whose effect is in the detail - noise, sharpening, stars - is shown at 100 %: `-View "Image > View > Zoom
100%;%middle"` zooms the view and puts its scroll bars at their middle, before the first half of the pair is taken.

## The recipes

Every picture of the guide is a recipe in `docs/user-guide/captures/`: a JSON file per group of pages, and
`roles.json`. A recipe states the picture, its page, the roles of the data it opens, what the picture shows - the
"Shows" of its provenance row - and the script's options for it, without `-Application`, `-Output` and `-Open`; one that
takes a pair says what each half shows as `before` and `after`:

```json
{
  "picture": "assets/images/processes/stretching/gamma-correction.webp",
  "page": "processes/stretching/gamma-correction.md",
  "data": ["linear"],
  "shows": "The Gamma Correction task with Gamma at 0.50, the preview on",
  "options": ["-Prepare", "Process > Intensity Transformations > Histogram Transformation|shadowsValue=0.02;midtonesValue=0.02", "-Page", "processes\\stretching\\gamma-correction.md", "-Set", "gammaValue=0.5"]
}
```

The picture's name without its extension is the capture's: `-Page x.md` saves `x.png`, `-Capture "<name>=..."`,
`-MainWindow <name>` and `%shot=<name>` save `<name>.png`.

```cmd
powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\tools\capture_guide_screenshots.ps1 ^
    -Recipes docs\user-guide\captures -Only processes/stretching/ ^
    -Application bin\msvc\release\AfterNight.exe -Output <a folder outside the repository> ^
    -Data "linear=<the linear master>;flat=<a master flat>" ^
    -Optimize docs\user-guide\.venv\Scripts\python.exe -Commit <the build's commit>
```

- `-Recipes` runs each recipe in a run of its own, the application started anew, into a folder per picture.
- `-Only <pattern>[,...]` keeps the recipes whose picture or page holds one of the patterns.
- `-Data "<role>=<path>;..."` gives the files the roles name.
- `-Optimize <python>` prepares each capture into the docs folder, as the next section does, and writes the
  provenance rows it prints to `<Output>\provenance.md`, with `-Commit` naming the application's commit.
- `-Recipes ... -WhatIf` lists the pictures the recipes make, one a line, and starts nothing: the list of pictures to
  take, which the inventory of a pass over the guide is compared with.

## Optimize

```cmd
docs\user-guide\.venv\Scripts\python build\scripts\tools\optimize_guide_image.py <capture.png> ^
    --to docs\user-guide\docs\en\assets\images\processes\stretching\histogram-transformation ^
    --shows "The Histogram Transformation task on a linear master with Shadows and Midtones at 0.02, the preview on" ^
    --data "A linear RGB master of the Carina Nebula, the maintainer's own data" --commit <the build's commit>
```

It scales the capture down to the longest side the guide allows, writes it as WebP at quality 90, or as PNG when
that is smaller, lowers the quality when the file is over the guide's weight, and refuses a picture that fits at no
quality. The limits are `images` in `data/limits.yml`. It prints the file it wrote and the picture's row for the
media provenance page. `-Optimize` runs it for every picture of the recipes it runs.

Paste the row into `docs/en/assets/MEDIA_PROVENANCE.md`, under the heading of the pages' group in "Captured
windows", in the same change as the picture. A batch of pages writes its rows under its own heading, so two batches
never edit the same lines.

## Put it on the page

A page shows the picture in a `figure` block, with an alternative text and a caption written while looking at it:

````markdown
```figure
image: ../../assets/images/processes/stretching/histogram-transformation.webp
alt: The Histogram Transformation task, described for a reader who cannot see it.
caption: One sentence that says what the picture shows.
pins:
  - {n: 2, x: 91.0, y: 27.5}
```
````

A pin is placed in percent of the picture's width and height, and its number is the step's. Then run
`check_user_guide.py --page <page>` and the strict build.

A picture written before it is taken adds `owed: true` to its block, and the page shows a frame holding its
description until then. `check_user_guide.py --owed` lists the pictures still owed; when one is committed, its block
drops `owed`, and the check refuses a block that is owed and whose picture exists.

**When to capture.** At the start of a batch of pages, so that the writer describes the window they are looking at.
And again whenever a window changes: the inventory test of the application's windows fails when a control is added,
removed or changed, and the test of the main window's own parts fails when one of them changes; the picture of what
changed is retaken in the same change as its inventory, from its recipe.

## What the script knows about the application

Each of these cost time once. They are in the script; they are here for whoever extends it.

- **The main window opens maximized.** It is restored and given its place and size before anything else. It is the
  window whose object name is `observatoryMainWindow`.
- **The menu bar shows on Alt.** The script posts Alt to the window before it walks a menu path, as a person shows the
  bar; once an entry is chosen the bar hides again.
- **A process opens as a task, not as a window.** Its page is the centre's current page, an element whose object name
  begins with `observatoryTaskPage.`; the script looks for a task's controls in that page alone, and closes it by its
  header's **Close**.
- **A modal dialog holds the choice that opened it.** The invocation of a menu item whose action opens a modal dialog -
  Settings - does not return until the dialog closes, and UI Automation then answers nothing else of the application
  for a minute: the script looks only at the windows the system lists until the choice returns.
- **The application takes no file argument.** A file opens through **File > Open** and the system's dialog: the
  path goes into the dialog's name box (control 1152, or 1148) and the dialog is accepted with its default button.
- **A dialog is looked for among the application's own windows,** never by its window class alone: other programs
  keep hidden dialogs of the same class, and a message posted to the first one found may reach one of them.
- **An item of the rail is clicked.** The rail draws its items itself; UI Automation lists them, by their names, and the
  script posts a click at the middle of one.
- **The display mode is two segments of the floating toolbar,** Linear and Autostretch, chosen by their names.
- **A combo box changes by a posted click and posted keys:** the click opens its list, Home and Down reach an item
  and Return takes it. The items of the list are not in the accessibility tree, and setting the box's value through
  UI Automation changes nothing.
- **A process the application will not open answers with a message box, not a task:** the image is not one it takes.
  The script answers the box with a posted Return and stops.
- **A stretching process shows its preview unstretched,** whatever the view's display mode: its preview is what
  Apply writes. The previews of the other processes follow the view's Autostretch.
- **A control is found by its object name.** The toolkit gives each control an identifier that is the path of
  object names down to it, and the last part is the name the inventory records. The title bar's own Close button
  is named `Close` too: a task's buttons are told from it by the path in their identifier.
- **A spin box takes its number through UI Automation, and keeps the keyboard.** It then draws a focus frame and a
  blinking caret, and the window never comes to rest. The script gives the keyboard to the task's Reset button,
  or its Close button, which draw the same with it.
- **A run disables the button that started it.** Apply disables the task's Reset for as long as the run lasts, and an
  import or an integration disables its own button; that is how the script knows a run has ended. A window that is
  busy may not answer UI Automation at all, which is read as still running.
- **A curve takes a point only where it is pressed on the line.** A click away from the line adds nothing, and a
  point is moved by a press, a move with the button held and a release, all posted to the window.
- **PowerShell's variables ignore case,** and a parameter keeps its type: an array assigned to `$left` is an
  assignment to the integer `-Left`, and a picture assigned to `$picture` would have become the text of a `-Picture`
  parameter. No variable of the script is named after one of its parameters.
- **The locale comes from `LANG`.** With `LANG=en_US` the application writes `0.50`, not `0,50`, whatever the
  machine's regional format. `QT_ENABLE_HIGHDPI_SCALING=0` and `QT_FONT_DPI=96` give a picture at 100 % on any
  monitor.
- **`PrintWindow` with the full-content flag** reads the window's own content, so a window in front of it is not in
  the picture. The capture is cropped to the frame the window manager reports, which leaves out the margin kept for
  the shadow, and then to the part the picture shows, by the place UI Automation gives it. The pointer is not
  captured, but what it hovers over is drawn highlighted, so it is moved out of the frame for the moment of the
  capture.
- **UI Automation is asked for one element at a time,** by its type and its name, or a level at a time down the main
  window's tree. Walking every descendant of the main window while a large image is open times out.
- **PowerShell passes `$null` to a C# `string` parameter as `""`,** and its variables ignore case: a loop variable
  named `$capture` overwrites a `-Capture` parameter.
- **Called with `-File`, a list arrives as one text.** The script splits pages and images at commas and captures at
  semicolons.
