# Capturing the guide's pictures

> **Audience:** whoever takes or retakes a picture of the application for the user guide.
>
> **Source of truth:** `build/scripts/tools/capture_guide_screenshots.ps1` and
> `build/scripts/tools/optimize_guide_image.py`, whose headers say everything they do. What a picture must be - its
> format, its name, the state of the window, pins, pairs and provenance - is "Media" in
> [docs/dev/USER_GUIDE_AUTHORING.md](../dev/USER_GUIDE_AUTHORING.md); this file is how to make one.

A picture of a window is taken from the application itself by a script, so that two people on two machines get the
same picture: the same theme and scale, the same locale, a profile nobody has used, the window at the size it opens
with. The script exists because the harness that took the README's pictures lived in a scratch folder and was lost,
and what it had learned about driving the application was learned again.

## What you need

- **A deployed Release build** on Windows: `bin\msvc\release\AfterNight.exe`, built at the commit the picture is of.
- **Windows PowerShell 5.1** (`powershell.exe`), which carries UI Automation. The capture script does not run under
  PowerShell 7.
- **Pillow, in the documentation environment.** The build helper makes `docs\user-guide\.venv`; Pillow is pinned
  beside it and installed by hand, once, by whoever captures. It is never installed in continuous integration:

  ```cmd
  docs\user-guide\.venv\Scripts\python -m pip install --require-hashes -r docs\user-guide\requirements-capture.txt
  ```

- **The data.** The scripts take the files as arguments, and no path on anyone's machine is committed. By role:

  | Role | For | Data |
  | --- | --- | --- |
  | A linear color master | Color, noise, stretch and finishing pages | A stacked RGB master that was never stretched |
  | The same master, stretched | Finishing pages | The linear master after the stretch `-Prepare` applies in the run, so that the data stays one file |
  | The workflows' frame sets | Calibration, registration and stacking pages | The color-camera and mono sets the workflow pages name |
  | A synthetic frame | A tool that needs no sky | A mono frame made for the purpose |

  The data is the maintainer's own, or data the project may publish.

## Capture

```cmd
powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\tools\capture_guide_screenshots.ps1 ^
    -Application bin\msvc\release\AfterNight.exe -Output <a folder outside the repository> ^
    -Open <the linear master> -Display Autostretch ^
    -Page processes\stretching\histogram-transformation.md,processes\stretching\gamma-correction.md
```

- `-Page` takes pages of the guide that state a `menu`: a process page, or a tutorial that works in one window. The
  script reads each page's `menu`, opens the process from that menu and names the picture after the page.
- `-Capture "<name>=<menu path>"` takes any menu path instead, and whatever window it opens, a dialog included;
  several are separated by semicolons. A window without a minimize button - Image Calibration, Image Registration,
  Plate Solve - is taken this way, since `-Page` reads such a window as a message. `-Capture "<name>=@<button>"`
  presses a button of the main window instead, by the name it shows: the Autostretch Settings of an image view,
  which no menu opens.
- `-View "<menu path>;..."` chooses menu items that act at once, after `-Prepare` and before the captures:
  **Image > View > Zoom 100%** for a pair at 100 %, **View > Session Console** to dock the console. `@<button>`
  presses a button of the main window there too, such as the console's **Clear Session**, so that the picture shows
  no line of the log, whose paths name the machine's folders.
- `-Open` opens the images the pictures need, in order, through **File > Open**. A raw color-filter frame asks
  whether to debayer it on opening; `-OpenAnswer No` keeps the mosaic.
- `-Display Autostretch` shows the opened image stretched. A linear master is nearly black without it, and the
  preview of most processes follows the view.
- `-MainWindow <name>` adds a picture of the main window as it is at the end. An image the captures left open -
  the master a run produced - is shown as `-Display` says first.
- `-MenuShot "<name>=<menu path>"` adds a picture of the main window with the menus of the path open and the pointer
  on its last item, for a page whose command acts from the menu at once. Each open menu is drawn by itself and laid
  over the main window's picture, so the screen may be locked while it is taken.
- `-Left`, `-Top`, `-Width` and `-Height` place the main window; a process window keeps the size it opens with.
- `-List` prints the controls of each window it captures: their type, their object name and the name they show.
- `-HoldSeconds <n>` waits before a window's picture: a preview that runs long on one thread - Patch PCA Denoise,
  GRAYCstoration - can look settled while it still works.

The script starts the application with a storage root of its own, which it removes afterwards, in the `en_US`
locale and without display scaling. It waits for each window to stop changing before it saves it, and says when a
window was still changing after `-SettleSeconds`. It leaves one PNG per capture in `-Output`, the frame and the
title bar included, and stops the application. It exits 1 when a capture was not saved.

Look at every capture before going on: the script cannot tell a window in the wrong state from one in the right one.

## The window at work, and the pair

A window as it opens often shows nothing of what it is for: a stretch at its defaults leaves a linear master black.
A page's opening picture shows the window in the state of the page's worked example, and the worked example of a
process judged by its result ends with the image before and after. One run takes all three:

```cmd
powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\tools\capture_guide_screenshots.ps1 ^
    -Application bin\msvc\release\AfterNight.exe -Output <a folder outside the repository> ^
    -Open <the linear master> -Page processes\stretching\histogram-transformation.md ^
    -Set "shadowsValue=0.02;midtonesValue=0.02" -Apply
```

- `-Set` does in the window what the worked example says, in order, before the picture is taken. A control is named
  by its object name, as the window's inventory under `data/ui/` records it, or by the name it shows:

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
  | `spccProcessAssignImageStateButton^Yes` | Presses a button that opens a message asking a question, and answers it with the button of that name. |

- `-Apply` takes the pair. After the window's picture it saves the main window as `<name>-before.png`, presses the
  window's **Apply**, waits for the run to end and saves the main window again as `<name>-after.png`. A message the run
  shows when it ends, with OK and no Cancel, is answered with OK. The image is in the same place at the same zoom in
  both, under the display mode it was opened in. Open a linear master without
  `-Display` for a stretch: the pair then shows the image as it is, black before and visible after.
- `-Prepare "<menu path>|<control>=<value>;..."` runs a process on the image before any capture: it opens the
  process, sets it as `-Set` does, applies it and closes it. A page of a process that works on a stretched image
  names the stretch in its worked example, and is captured with it:

  ```cmd
      -Prepare "Process > Intensity Transformations > Histogram Transformation|shadowsValue=0.02;midtonesValue=0.02" ^
      -Page processes\stretching\gamma-correction.md -Set "gammaValue=1.5" -Apply
  ```

`-Set` applies to every window of the run, so a page is captured in a run of its own. What a run leaves on the
screen - the message that it is complete, its progress window - is dismissed before its window is closed, and an
image it opened stays for `-MainWindow`.

- **Several preparations** go in one `-Prepare`, separated by `||`, and run in that order. A window with OK in place
  of Apply is applied with OK; a preparation whose last change runs it already (`OK!`) is not applied again, and one
  with neither Apply nor OK - a tool that saves a file - does only what its changes say.
- **A window a button of the main window opens** is prepared as `"@<button>|<control>=<value>"`: its changes act at
  once and it is closed. A color calibration's pair needs `"@Autostretch Settings|linkedChannels=On"`, since the
  view's Autostretch stretches each channel on its own unless its channels are linked, and so hides a color change.
- **A process that needs a tool's path** - Starnet++ and StarNet2 - is captured in a profile that knows it: a run
  with `-Storage <new folder>` keeps its storage root, its settings database (`afternight_data.db`, table
  `global_settings`) takes the path under the key the External Tools page writes (`tools/starnetPath`), and later
  runs reuse that root with `-Storage <folder> -ReuseStorage`.
- **A run that makes a new image** - a recombination, a mask - is paired with that image: `-AfterView` says what to
  do once the run has ended, such as `"Window > Group All Images;@RGB - starless - RGB - star mask...;Image > View >
  Zoom 100%;%middle"`. `@<name>` chooses a view's tab as it chooses a button, and a name ending in `...` is the
  beginning of the name, since a tab adds ` *` while its image is unsaved.

**Cutting the image out of the pair.** With the main window at its default place and size and one image open, fit
to its view, the image is the same rectangle of both pictures. Give that rectangle to the optimizing script as
`--crop LEFT,TOP,WIDTH,HEIGHT` for both, so the two crops are the same region: `246,142,1094,820` for the guide's
master in the default main window. A tone process is shown on the whole frame. A process whose effect is in the
detail - noise, sharpening, stars - is shown at 100 %: `-View "Image > View > Zoom 100%;%middle"` zooms the view and
puts its scroll bars at their middle, and `--crop 467,305,640,480` takes the middle of the view.

## Optimize

```cmd
docs\user-guide\.venv\Scripts\python build\scripts\tools\optimize_guide_image.py <capture.png> ^
    --to docs\user-guide\docs\en\assets\images\processes\stretching\histogram-transformation ^
    --shows "The Histogram Transformation window as it opens on a linear master, the preview on" ^
    --data "A linear RGB master of the Carina Nebula, the maintainer's own data" --commit <the build's commit>
```

It scales the capture down to the longest side the guide allows, writes it as WebP at quality 90, or as PNG when
that is smaller, lowers the quality when the file is over the guide's weight, and refuses a picture that fits at no
quality. The limits are `images` in `data/limits.yml`. It prints the file it wrote and the picture's row for the
media provenance page.

Paste the row into `docs/en/assets/MEDIA_PROVENANCE.md`, under the heading of the pages' group in "Captured
windows", in the same change as the picture. A batch of pages writes its rows under its own heading, so two batches
never edit the same lines.

## Put it on the page

A page shows the picture in a `figure` block, with an alternative text and a caption written while looking at it:

````markdown
```figure
image: ../../assets/images/processes/stretching/histogram-transformation.webp
alt: The Histogram Transformation window, described for a reader who cannot see it.
caption: One sentence that says what the picture shows.
pins:
  - {n: 2, x: 91.0, y: 27.5}
```
````

A pin is placed in percent of the picture's width and height, and its number is the step's. Then run
`check_user_guide.py --page <page>` and the strict build.

**When to capture.** At the start of a batch of pages, so that the writer describes the window they are looking at.
And again whenever a window changes: the inventory test of the application's windows fails when a control is added,
removed or changed, and the picture of that window is retaken in the same pull request as its inventory.

## What the script knows about the application

Each of these cost time once. They are in the script; they are here for whoever extends it.

- **The main window opens maximized.** It is restored and given its place and size before anything else.
- **The application takes no file argument.** A file opens through **File > Open** and the system's dialog: the
  path goes into the dialog's name box (control 1152, or 1148) and the dialog is accepted with its default button.
- **A dialog is looked for among the application's own windows,** never by its window class alone: other programs
  keep hidden dialogs of the same class, and a message posted to the first one found may reach one of them.
- **A process window closes on a posted Escape.** Invoking the Close button of its title bar through UI Automation
  does not close it.
- **A combo box changes by a posted click and posted keys:** the click opens its list, Home and Down reach an item
  and Return takes it. The items of the list are not in the accessibility tree, and setting the box's value through
  UI Automation changes nothing.
- **A process the application will not open answers with a message box, not a window:** it is open on the image
  already, or the image is not one it takes. A process window has a minimize button and a message box has none,
  which is how the script tells the two apart for a `-Page`; it answers the box with a posted Return and stops.
  UI Automation cannot be asked what the box is: the invocation of the menu item that raised it does not return
  until the box closes, and every other request waits behind it.
- **A stretching process shows its preview unstretched,** whatever the view's display mode: its preview is what
  Apply writes. The previews of the other processes follow the view's Autostretch.
- **A control is found by its object name.** The toolkit gives each control an identifier that is the path of
  object names down to it, and the last part is the name the inventory records. The title bar's own Close button
  is named `Close` too: the window's buttons are told from it by the path in their identifier.
- **A spin box takes its number through UI Automation, and keeps the keyboard.** It then draws a focus frame and a
  blinking caret, and the window never comes to rest. The script gives the keyboard to the window's Reset button,
  or its Close button, which draw the same with it.
- **A run disables the button that started it.** Apply disables the window's Reset for as long as the run lasts,
  and an import or an integration disables its own button; that is how the script knows a run has ended. A window
  that is busy may not answer UI Automation at all, which is read as still running.
- **A curve takes a point only where it is pressed on the line.** A click away from the line adds nothing, and a
  point is moved by a press, a move with the button held and a release, all posted to the window.
- **PowerShell's variables ignore case,** and a parameter keeps its type: an array assigned to `$left` is an
  assignment to the integer `-Left`, and fails.
- **The Dashboard stretches a thumbnail only for a file that records itself as linear.**
- **The locale comes from `LANG`.** With `LANG=en_US` the application writes `0.50`, not `0,50`, whatever the
  machine's regional format. `QT_ENABLE_HIGHDPI_SCALING=0` and `QT_FONT_DPI=96` give a picture at 100 % on any
  monitor.
- **`PrintWindow` with the full-content flag** reads the window's own content, so a window in front of it is not in
  the picture. The capture is cropped to the frame the window manager reports, which leaves out the margin kept for
  the shadow. The pointer is not captured, but what it hovers over is drawn highlighted, so it is moved out of the
  frame for the moment of the capture.
- **UI Automation is asked for one element at a time,** by its type and its name. Walking every descendant of the
  main window while a large image is open times out.
- **PowerShell passes `$null` to a C# `string` parameter as `""`,** and its variables ignore case: a loop variable
  named `$capture` overwrites a `-Capture` parameter.
- **Called with `-File`, a list arrives as one text.** The script splits pages and images at commas and captures at
  semicolons.
