---
kind: interface
summary: >-
  The menu bar's nine menus, the toolbar and a view's right-click menu: where each kind of command lives, which image
  it acts on and why it may be greyed.
---
# Toolbar & Menus

Every command of AfterNight is in the menu bar, in nine menus. The toolbar under it repeats **Open**, **Save as...**
and **Close** and holds the layout buttons. A right-click on an image view opens the image's submenus where you point.

A command acts on the [active image](../reference/glossary.md#active-image), the one the status bar's box names. A
command that does not fit that image is greyed, or says why when you choose it.

```figure
owed: true
image: ../assets/images/interface/toolbar-and-menus.webp
alt: >-
  The AfterNight main window over the Carina Nebula master. The Process menu is open under the menu bar: Favorite
  Processes and Recent Processes at the top, a line, then sixteen categories from Adjustments to Utilities. Intensity
  Transformations is highlighted, and its submenu beside it lists nine processes from ArcSinh Stretch to Statistical
  Stretch, with Histogram Transformation under the pointer. The status bar at the bottom shows the image's name in a
  box.
caption: >-
  The Process menu over the linear Carina master, open on Intensity Transformations.
pins:
  - {n: 1, x: 30.0, y: 4.2}
  - {n: 2, x: 33.0, y: 6.8}
  - {n: 3, x: 26.0, y: 7.9}
  - {n: 4, x: 26.0, y: 18.3}
  - {n: 5, x: 46.5, y: 40.5}
  - {n: 6, x: 8.0, y: 97.8}
```

## What you see

1. **The menu bar.** **File**, **Edit**, **View**, **Image**, **Process**, **Extensions**, **Window**, **Help** and
   **About**.
2. **The toolbar.** **Open**, **Save as...** and **Close**, then the buttons that lock, group, split and fit the
   views, which [Docking Panels](docking-panels.md) describes.
3. **Favorite Processes and Recent Processes.** At the top of the **Process** menu: the processes you chose to keep
   at hand, and the ones you used last.
4. **The categories.** Sixteen submenus of processes, from **Adjustments** to **Utilities**.
5. **A category's processes.** Each opens its process window, or runs at once. A process an extension adds follows a
   line at the end of its category.
6. **The active image.** The status bar's box names the image the commands act on. Choose another name to change it.

## What you can do

### Find a command

| Menu | Holds |
| --- | --- |
| **File** | Opening, recent files, a new [sequence](../reference/glossary.md#sequence), saving, closing and **Exit**. |
| **Edit** | **Undo** and **Redo**, which name the step, **Image History** and **Settings...**. |
| **View** | **Session Console**. |
| **Image** | What acts on the active image: **Actions**, **Convert Format**, **Extract Channel**, **Fast Geometry**, **Information**, **Mask**, **Sequence** and **View**, then **Show in File Browser**. |
| **Process** | The processes, by category. |
| **Extensions** | **Manage Extensions...**, then a submenu for each installed extension. |
| **Window** | The layout commands of [Docking Panels](docking-panels.md). |
| **Help** | **User Guide**. |
| **About** | **About Qt**, **About AfterNight** and **System Information**. |

The commands of **Image** that act at once are on
[Non-Interactive Processes](../processes/utility/non-interactive-processes.md), and the windows of **Information**
are those of a view's toolbar, which [Image Viewer](image-viewer.md) describes. The
[Process Catalog](../processes/index.md) lists every process by the stage it belongs to.

### Know why a command is greyed

- Without an image, most of **Image** and **Process** is greyed. **Color Spaces**, **Preprocess** and **Image List
  Analyzer** stay available, as do the commands of **File**, **Edit**, **View**, **Extensions**, **Help** and
  **About** that need no image.
- A grayscale image greys what needs color, such as **Extract Channel** and the color calibrations, and offers
  **Convert to RGB** instead.
- **Image > Sequence** needs a sequence, **EXIF Viewer** and **FITS Header** a file that stated their record, and
  **Plate Solve** a still image.
- A built-in process is greyed for no other reason. When it cannot open on the image, it says why in a message box,
  as [Process Windows](process-windows.md) describes.
- A process an extension adds is greyed while it loads, while it cannot run, or when it does not fit the image. Its
  tooltip says why.

### Act on the image you point at

Right-click a view. The click makes its image the active one first, so every command of the menu acts on it. The
menu holds **Actions**, **Process**, **Convert Format**, **Extract Channel**, **Fast Geometry**, **Information**,
**Mask**, **Sequence**, **View**, **Current Window** and, after a line, **Show in File Browser**. All but
**Current Window** are the menu bar's own, in the same state. **Current Window** arranges the views of one window.
With an image open, a right-click on the main window outside the views offers **Lock Layout** and **Unlock Layout**.

### Keep processes at hand

1. Choose **Process > Favorite Processes > Manage Favorites...**.
2. Type part of a name in **Search processes...**, tick the processes you want and choose **OK**.
3. Open **Process > Favorite Processes**. Your processes are listed by name, above **Manage Favorites...**.

**Recent Processes** fills itself, the last one first.

### Get help and information

Press ++f1++ or choose **Help > User Guide** for this guide. In a process window, ++f1++ opens the guide at that
process's page. **About > About AfterNight** shows the version and the libraries AfterNight uses, and
**About > System Information** a report of the application and the computer.

## Controls

### The menu bar

```controls
window: The menu bar
controls:
  - name: Open
    kind: action
    default: ""
    effect: "**File > Open**, and the toolbar's first button: opens one or more files, each in a view of its own. The Dashboard page says what the open shows and asks."
    enabled: Always. ++ctrl+o++ does the same.
  - name: Open Recent
    kind: action
    default: ""
    effect: Lists the recent files by their full path, the last one first, then **Clear recent files list**, which asks before it empties the list.
    enabled: Greyed while the list is empty.
    tradeoff: A picture of this menu shows your folders.
    persistence: The same list as the Dashboard's recent files, up to the **Recent Files Limit** of the General page of Settings.
    note: A file that can no longer be opened leaves the list after its "File open error".
  - name: New Sequence...
    kind: action
    default: ""
    effect: Opens New Sequence, which makes a sequence of files.
    enabled: Always. It has no shortcut.
  - name: Save as...
    kind: action
    default: ""
    effect: Saves the active image or sequence to a file you name. The toolbar's second button and **Image > Actions > Save File...** do the same.
    enabled: With an image. ++ctrl+shift+s++ does the same.
    tradeoff: The format you choose decides how the samples are stored. Saving And Exporting says what each keeps.
  - name: Close, Close All and Close All Others
    kind: action
    default: ""
    effect: Close the active view, every view, or every view but the active one. The toolbar's third button is **Close**.
    enabled: With an image.
    tradeoff: A view with unsaved changes asks "The image '...' has unsaved changes, are you sure you want to close it?". One with process windows open on it asks too, in a message titled "Close image with process attached" that names them, and the windows close with it. **Close All** and **Close All Others** go on to the next view after a No.
  - name: Exit
    kind: action
    default: ""
    effect: Closes AfterNight, after the question about unsaved changes when a view has some.
  - name: Undo and Redo
    kind: action
    default: ""
    effect: Take the active image back one step of its history, or do the step again. Each names its step, as `Undo <Gamma Correction>`.
    enabled: When there is a step to take back or to do again. ++ctrl+z++ and ++ctrl+y++ do the same.
  - name: Image History
    kind: action
    default: ""
    effect: Opens the active image's history.
    enabled: With an image.
  - name: Settings...
    kind: action
    default: ""
    effect: Opens Settings. ++ctrl+comma++ does the same.
  - name: Session Console
    kind: check
    default: "Off"
    effect: "**View > Session Console** shows or closes the console, docked at the bottom of the workspace."
    enabled: Always, on the Dashboard too.
    persistence: Off at every start.
  - name: User Guide
    kind: action
    default: ""
    effect: Opens this guide from the copy installed with AfterNight, else the online guide at `https://docs.afternightastro.org/`.
    enabled: Always. ++f1++ does the same from the main window, and from a process window it opens that process's page.
    tradeoff: The online guide opens in your browser and uses the network.
    note: "With the environment variable `AFTERNIGHT_DISABLE_ONLINE_USER_GUIDE` set to `1`, `true`, `yes` or `on`, the online guide is never opened, and a missing copy gives \"The offline AfterNight User Guide could not be opened, and online fallback is disabled by policy.\" When neither opens, a message titled \"Help\" says \"The AfterNight User Guide could not be opened locally or online.\""
  - name: About Qt
    kind: action
    default: ""
    effect: Opens the Qt library's own window about itself.
  - name: About AfterNight
    kind: action
    default: ""
    effect: Opens a window with the word Version and the version's number, or `dev` for a build that states none, and three tabs.
    range: "**About** describes the project. **Libraries** lists Qt, KDDockWidgets and SQLite, then the engine with its version and the libraries it reports - Exiv2, FFmpeg, FFTW, GNU Scientific Library, Highway, libjpeg-turbo, libpng, LibRaw, LibTIFF, JSON for Modern C++, oneTBB, OpenFITS and OpenXISF among them - each with the version it was built against, and the one loaded when they differ, then Crashpad. It notes that the extension host runs Python and that G'MIC is reached through an optional adapter. **License** gives the licences and the notes on derived work."
    note: The Dashboard's About button opens the same window. Nothing in it uses the network.
  - name: System Information
    kind: action
    default: ""
    effect: Opens a report of the application, its build and the computer. Its Dependencies section lists the engine and its libraries as **About AfterNight** does.
    tradeoff: The report names details of your system. It is made on your computer and sent nowhere; read it before you share it.
  - name: Shortcuts
    kind: readout
    default: ""
    effect: "The menus show ++ctrl+o++, ++ctrl+shift+s++, ++ctrl+z++, ++ctrl+y++, ++ctrl+comma++, ++f1++ and, under **Image > View**, ++ctrl+plus++, ++ctrl+minus++ and ++ctrl+0++. A view with the keyboard focus adds its own: ++f11++, ++ctrl+shift+f++, ++ctrl+h++, ++ctrl+shift+t++, ++ctrl+shift+x++, ++ctrl+shift+h++, ++ctrl+shift+m++, ++ctrl+shift+z++, ++ctrl+shift+p++, ++ctrl+p++ and ++ctrl+shift+l++, which Image Viewer describes."
    range: The shortcuts are fixed. A desktop or another program can take one of them first.
    note: A view's own shortcut can work while its menu entry is greyed, as ++ctrl+shift+x++ opens an empty EXIF window.
```

### The Image menu

```controls
window: The Image menu
controls:
  - name: Actions
    kind: action
    default: ""
    effect: "**Duplicate**, **Rename**, **Close** and **Save File...**."
    enabled: With an image.
    tradeoff: "**Duplicate** takes the memory of a second image."
  - name: Convert Format
    kind: action
    default: ""
    effect: "**Convert to 8 bit**, **Convert to signed 16 bit**, **Convert to unsigned 16 bit**, **Convert to signed 32 bit**, **Convert to unsigned 32 bit**, **Convert to 32 bit float** and **Convert to 64 bit float**, each writing a copy in that type."
    enabled: With an image. The image's own type is greyed, **Convert to 8 bit** for a signed or unsigned 8-bit image. A SER or video sequence also greys the signed 16-bit, both 32-bit integer and both floating-point entries.
    tradeoff: A narrower type can clip or round the samples, and a wider one takes more memory.
  - name: Extract Channel
    kind: action
    default: ""
    effect: "Opens one channel or component of the image as a new image, or all of a model's components: **Extract Red Channel**, **Extract Green Channel**, **Extract Blue Channel**, **Extract RGB Channels**; **Extract CIE L***, **Extract CIE a***, **Extract CIE b***, **Extract CIE c***, **Extract CIE h***, **Extract CIE a*=R / b*=G**, **Extract CIE L*a*b***, **Extract CIE L*c*h***; **Extract CIE X**, **Extract CIE Y**, **Extract CIE Z**, **Extract CIE XYZ**; **Extract HSL Hue**, **Extract HSL Saturation**, **Extract HSL Lightness**, **Extract HSL**; **Extract HSV Hue**, **Extract HSV Saturation**, **Extract HSV Value**, **Extract HSV**; **Extract HSI Hue**, **Extract HSI Saturation**, **Extract HSI Intensity**, **Extract HSI**; **Extract YCbCr Luma**, **Extract YCbCr Blue Chroma**, **Extract YCbCr Red Chroma** and **Extract YCbCr**."
    enabled: For an RGB image.
    tradeoff: Each new image takes memory of its own.
  - name: Fast Geometry
    kind: action
    default: ""
    effect: "**Horizontal Mirror**, **Vertical Mirror**, **Rotate 180º**, **Rotate 90º Left** and **Rotate 90º Right**, written with the ordinal sign º."
    enabled: With an image.
    tradeoff: Each replaces the image, with one step in its history, and drops its plate solution.
  - name: Information
    kind: action
    default: ""
    effect: "**Statistics**, **Histogram**, **EXIF Viewer**, **FITS Header**, **Astrometry Info**, **Plate Solve**, **Annotate Image...** and **Summary**, the windows the view's analysis and astrometry buttons open."
    enabled: "With an image. **Statistics** and **Histogram** as the view's buttons, **EXIF Viewer** and **FITS Header** when the file stated that record, **Plate Solve** for a still image, **Annotate Image...** for a solved still image. **Astrometry Info** and **Summary** for any image."
    note: "**Plate Solve** is ++ctrl+p++ in a view with the keyboard focus."
  - name: Mask
    kind: action
    default: ""
    effect: "**Select Mask**, **Remove Mask**, **Invert Mask**, **Enable Mask**, **Show Mask** and **Rendering Mode**, which offers **Overlay Red**, **Overlay Green**, **Overlay Blue**, **Overlay Yellow**, **Overlay Magenta**, **Overlay Cyan**, **Replace** and **Multiply**, one at a time."
    enabled: "**Select Mask** with an image. The others when the image has a mask, with their checks showing its state. **Overlay Red** is the first rendering."
    note: Showing a mask changes only what you see. Masks describes them.
  - name: Sequence
    kind: action
    default: ""
    effect: "**Convert**, with **Convert to SER**, **Convert to Image List** and **Convert to FITS**, then **Extract Frame**, **Extract Sequence** and **Export...**."
    enabled: For a sequence. The sequence's own kind is greyed. **Convert to FITS** is always greyed; **Convert to Image List** writes each frame as a FITS file.
    tradeoff: A conversion or an export writes a copy of every frame to disk.
  - name: View
    kind: action
    default: ""
    effect: "**Zoom In**, **Zoom Out**, **Zoom 100%**, **Fit To Window** and **Maximize View**, which reads **Restore View** while the view is maximized."
    enabled: With an image. **Maximize View** is greyed for a view in a floating window.
    note: Only the view changes, never the image.
  - name: Show in File Browser
    kind: action
    default: ""
    effect: Opens your file manager with the image's file selected, the file it was read from or last saved to, or the sequence's file.
    enabled: While the image is a file. An image a process made is greyed until you save it.
    range: A file moved or deleted since gives "File Not Found" with "The file does not exist on disk:" and its path.
```

### The Process menu

```controls
window: The Process menu
controls:
  - name: Favorite Processes
    kind: action
    default: ""
    effect: Lists the processes you chose, by name in alphabetical order without regard to case, then **Manage Favorites...** and **Clear favorites list**. Each entry's tooltip is the process's identifier.
    range: A process no longer installed is left out.
    enabled: Always, empty or not, for **Manage Favorites...**.
    persistence: Kept between sessions as `interface/favoriteProcessIds`, the identifiers only, never values or images.
    note: An entry opens its process as the category's entry does, with the same checks of the active image. So do the entries of **Recent Processes** and of a view's right-click menu.
  - name: Clear favorites list
    kind: action
    default: ""
    effect: Asks "Are you sure you want to delete the favorite processes list?", with "The list of favorite processes will be lost", and empties the list on **Yes**.
    enabled: While the list holds a process.
    tradeoff: "**Yes** is the default answer. Nothing brings the list back but choosing the processes again."
  - name: Recent Processes
    kind: action
    default: ""
    effect: Lists the processes you started last, the last one first and each once, then **Clear recent processes list**.
    range: "**Recent Processes Limit**, on the General page of Settings, sets how many: from 1 to 100, 10 unless you change it."
    enabled: Greyed while the list is empty. An extension's process still loading is greyed, with "This recent extension process is still loading and cannot be launched yet."
    persistence: Kept between sessions as `interface/recentProcessIds`, the limit as `general/recentProcessesLimit`. A process enters it only when it starts, and one no longer installed leaves it once the extensions are found.
  - name: Clear recent processes list
    kind: action
    default: ""
    effect: Asks "Are you sure you want to delete the recent used processes?", with "The history of used processes will be lost", and empties the list on **Yes**.
    tradeoff: "**Yes** is the default answer."
  - name: Adjustments
    kind: action
    default: ""
    effect: "**Brightness/Contrast**, **Gamma Correction**, **Exposure**, **Hue/Saturation/Lightness**, **Lightness/Chroma/Hue**, **Negative Transformation**, **Convert to RGB** and **Convert to Grayscale**."
    enabled: With an image. **Hue/Saturation/Lightness**, **Lightness/Chroma/Hue** and **Convert to Grayscale** need an RGB image, **Convert to RGB** a grayscale one.
  - name: Background Modelization
    kind: action
    default: ""
    effect: "**Background Extraction** and **Auto Gradient Removal**."
    enabled: With an image.
  - name: Color Calibration
    kind: action
    default: ""
    effect: "**Background Neutralization**, **SCNR Filter**, **Color Calibration**, **Photometric Color Calibration**, **Spectrophotometric Color Calibration** and **Linear Match**."
    enabled: With an image. All but **Linear Match** need an RGB image.
  - name: Color Spaces
    kind: action
    default: ""
    effect: "**Channel Composition**, **Colorize**, **Debayer**, **LRGB Combination**, **NBRGB Combination**, **NB Color Mapper**, **Narrowband Palette Composer**, **Merge CFA** and **Split CFA**."
    enabled: Always. **LRGB Combination** and **NBRGB Combination** need an RGB image.
  - name: Details
    kind: action
    default: ""
    effect: "**Gaussian Blur**, **High Pass**, **Local Contrast Enhancement**, **Dark Structure Enhance**, **Multiscale Alpha Sharpen** and **Unsharp Mask**."
    enabled: With an image.
  - name: Filters
    kind: action
    default: ""
    effect: "**Gaussian Blur**, the same as in **Details**, **Median Filter**, **Bilateral Filter**, **Banding Reduction Filter** and **Cosmetic Correction Filter**."
    enabled: With an image.
  - name: Geometry
    kind: action
    default: ""
    effect: "**Resize**, **Crop**, **Rotate**, **Integer Resample** and **CFA Integer Resample**."
    enabled: With an image.
  - name: Intensity Transformations
    kind: action
    default: ""
    effect: "**ArcSinh Stretch**, **Binarize**, **CLA Histogram Equalization**, **Color Saturation**, **Curves Transformation**, **Generalized Hyperbolic Transformation**, **Histogram Transformation**, **Local Histogram Equalization** and **Statistical Stretch**."
    enabled: With an image. **Color Saturation** needs an RGB image.
  - name: Mask Generation
    kind: action
    default: ""
    effect: "**Mask Drawing**, **Range Selection**, **Star Mask** and **Starnet++ Star Mask**."
    enabled: With an image.
  - name: Morphology
    kind: action
    default: ""
    effect: "**Morphological Transformation** and **Tighten Edges**."
    enabled: With an image.
  - name: Multiscale
    kind: action
    default: ""
    effect: "**Multiscale Linear Transform**."
    enabled: With an image.
  - name: Noise Reduction
    kind: action
    default: ""
    effect: "**DeepSNR**, **GRAYCstoration Filter**, **Non-Local Bayes Filter**, **Patch PCA Denoise Filter**, **Total Variation Denoise Filter** and **Wavelet Denoise Filter**."
    enabled: With an image.
  - name: Pixel Math
    kind: action
    default: ""
    effect: "**Pixel Math**."
    enabled: With an image.
  - name: Preprocess
    kind: action
    default: ""
    effect: "**Image Calibration**, **Image Registration**, **Image Stacking**, **Image Integration** and **Image Integration Lite**."
    enabled: Always. These windows choose their images themselves.
  - name: Star Processing
    kind: action
    default: ""
    effect: "**Profiled Starmask Recombination**, **Starmask Recombination**, **Star Reduction**, **Starnet++**, **Star Profiler v1** and **Star Profiler v2**."
    enabled: With an image.
  - name: Utilities
    kind: action
    default: ""
    effect: "**Image Blend**, **Image Compare**, **Noise Generator** and **Image List Analyzer**."
    enabled: The submenu always. **Image List Analyzer** always, the other three with an image.
  - name: Extension processes
    kind: action
    default: ""
    effect: A process an extension adds is listed after a line at the end of the category its package names - Adjustments, Background Modelization, Color Calibration, Details, Filters, Geometry, Intensity Transformations, Mask Generation, Noise Reduction, Preprocess or Star Processing.
    enabled: "Greyed, with the reason as its tooltip, while it loads (\"This extension action is still loading and cannot be launched yet.\"), when it is unavailable or does not suit this version of AfterNight, and when it does not fit the active image. One whose environment must still be installed or repaired stays available: its tooltip says so, and choosing it prepares the environment first."
    note: A package of a category none of these names is not listed.
```

### Extensions

```controls
window: Extensions
controls:
  - name: Manage Extensions...
    kind: action
    default: ""
    effect: Opens the Extensions window on its **Browse** tab, to install, update, remove and configure extensions, as its tooltip says.
    tradeoff: Installing and updating use the network and disk space.
  - name: Extension submenu
    kind: action
    default: ""
    effect: One for each installed extension, by its name, in alphabetical order without regard to case. It holds **Manage...**, which opens the Extensions window on its **Installed** tab, and **Homepage**, **Repository** and **Support** when the package names them.
    range: A package that cannot be read has none. The processes of an extension are in the **Process** menu, never here.
    tradeoff: "**Homepage**, **Repository** and **Support** open the address in your browser, and so use the network. Their tooltip shows the address."
```

### Manage Favorite Processes

```controls
window: Manage Favorite Processes
controls:
  - name: Search processes...
    kind: field
    default: ""
    effect: Shows only the processes whose name or identifier holds what you type, without regard to case.
  - name: Process list
    kind: check
    default: Your favorites ticked
    effect: Every process AfterNight registers, the commands of the Image menu that run as processes among them, by name in alphabetical order and ticked when it is a favorite. The tooltip of each is its identifier.
  - name: OK
    kind: button
    default: ""
    effect: Keeps the ticked processes as your favorites.
  - name: Cancel
    kind: button
    default: ""
    effect: Closes the window and leaves the favorites as they were.
```

## When it goes wrong

Most of the Image and Process menus are greyed
:   **Cause.** No view is active, or the Dashboard is showing.
:   **Fix.** Open an image, or click its view.

A color command is greyed
:   **Cause.** The active image is grayscale.
:   **Fix.** Click the view of an RGB image, or use **Process > Adjustments > Convert to RGB** first.

A command acted on the wrong image
:   **Cause.** Another view was active when you chose it.
:   **Fix.** Undo the step in that view with ++ctrl+z++, click the view you mean, and choose the command again, or
    right-click that view.

A shortcut does nothing
:   **Cause.** Its command does not fit the active image, or the shortcut belongs to a view that does not have the
    keyboard focus.
:   **Fix.** Click the view you mean, and look at the command in its menu: a greyed entry says the image does not
    fit it.

A process of an extension is greyed or missing
:   **Cause.** It is still loading, cannot run, does not fit the image, or names a category the menu does not have.
:   **Fix.** Read its tooltip, wait for the extensions to load, or open **Extensions > Manage Extensions...** to
    repair or update it.

++f1++ opens the guide in the browser from the network, or says it cannot open it
:   **Cause.** The copy installed with AfterNight is missing or cannot be opened.
:   **Fix.** Install a package that holds the guide, or unset `AFTERNIGHT_DISABLE_ONLINE_USER_GUIDE` to allow the
    online guide.

## Related

- [Interface Overview](overview.md), for the main window the menus sit in.
- [Docking Panels](docking-panels.md), for the **Window** menu, the layout buttons and **Current Window**.
- [Non-Interactive Processes](../processes/utility/non-interactive-processes.md), for the commands of the **Image**
  menu that act at once.
- [Process Windows](process-windows.md), for the window a process opens and the messages it gives.
- [Using Extensions](../extensions/using-extensions.md), for the **Extensions** menu and the processes it adds.
- [Keyboard Shortcuts](../reference/keyboard-shortcuts.md), for every key by window.
