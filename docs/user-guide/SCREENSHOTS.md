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
  | A stretched copy of it | Finishing pages | The same master after its stretch |
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

- `-Page` takes process pages of the guide. The script reads each page's `menu`, opens the process from that menu
  and names the picture after the page.
- `-Capture "<name>=<menu path>"` takes any menu path instead, and whatever window it opens, a dialog included;
  several are separated by semicolons.
- `-Open` opens the images the pictures need, in order, through **File > Open**.
- `-Display Autostretch` shows the opened image stretched. A linear master is nearly black without it, and the
  preview of most processes follows the view.
- `-MainWindow <name>` adds a picture of the main window as it is at the end.
- `-Left`, `-Top`, `-Width` and `-Height` place the main window; a process window keeps the size it opens with.

The script starts the application with a storage root of its own, which it removes afterwards, in the `en_US`
locale and without display scaling. It waits for each window to stop changing before it saves it, and says when a
window was still changing after `-SettleSeconds`. It leaves one PNG per capture in `-Output`, the frame and the
title bar included, and stops the application. It exits 1 when a capture was not saved.

Look at every capture before going on: the script cannot tell a window in the wrong state from one in the right one.

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
