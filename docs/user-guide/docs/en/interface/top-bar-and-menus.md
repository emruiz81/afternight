---
kind: interface
summary: >-
  The top bar of the main window - the project switcher, where you are, the command palette's field, Night, User
  Guide, Information and All menus - and the menu bar that shows on Alt, menu by menu.
---
# Top Bar and Menus

The top bar runs along the top of the main window. It says which [project](../reference/glossary.md#project) is open and where you are, holds the field
that opens the [command palette](command-palette.md), and at its right the buttons of the whole window. The menu bar
is hidden: it shows over the top bar while you use it, and **All menus** holds the same menus at any time.

A command of the menus acts on the [active image](../reference/glossary.md#active-image), the one the status line's
box names. A command that does not fit that image is greyed, or says why when you choose it.

```figure
image: ../assets/images/interface/top-bar-and-menus.webp
alt: >-
  The top of the main window with an image open. Along the very top, the menu bar reads File, Edit, View, Image,
  Process, Extensions, Window, Help and About, with Window greyed. Under it, the top bar: the AfterNight mark, a
  folder, a wide field reading Search or run a command with the keys Ctrl K, and at the right a lock and three
  layout icons, a moon, a question mark, an i and three dots. The View menu is open: Session Console, then Home,
  Integration, Command Palette with Ctrl+K, Readout ticked, Night with Ctrl+Shift+N, Density, Next Region and Previous
  Region. Density is highlighted, and its submenu beside it offers Compact, chosen, and Comfortable.
caption: >-
  The menu bar shown with Alt, its View menu open on Density, over the top bar of a window with the Carina master open.
pins:
  - {n: 1, x: 32.0, y: 4.8}
  - {n: 2, x: 2.6, y: 8.4}
  - {n: 3, x: 37.6, y: 8.4}
  - {n: 4, x: 80.8, y: 8.4}
  - {n: 5, x: 91.2, y: 11.2}
  - {n: 6, x: 93.5, y: 11.2}
  - {n: 7, x: 95.7, y: 11.2}
  - {n: 8, x: 98.0, y: 11.2}
  - {n: 9, x: 6.0, y: 27.2}
owed: true
```

## What you see

1. **The menu bar.** **File**, **Edit**, **View**, **Image**, **Process**, **Extensions**, **Window**, **Help** and
   **About**, shown while you press ++alt++.
2. **The project switcher and where you are.** **No project** until a project is open, then its name. Beside it, the
   path of what the centre shows: **Home**, **Integration**, a [workspace](../reference/glossary.md#workspace) and its active image, or a [task](../reference/glossary.md#task) and its target.
3. **The command palette's field.** **Search or run a command**, which ++ctrl+k++ opens too.
4. **The layout icons.** They lock the layout of a workspace and group, split or fit its images, and show only while a
   workspace does. See [Workspaces](workspaces.md).
5. **Night.** Everything in red, for eyes adapted to the dark.
6. **User Guide.** This guide.
7. **Information.** **About AfterNight**, **About Qt** and **System Information**.
8. **All menus.** Every menu of the menu bar, in one menu.
9. **A menu.** Here **View**, open on **Density**, which sets how large the window's bars and controls are.

## What you can do

### Show the menu bar

Press and release ++alt++: the menu bar shows and takes the keyboard focus. ++alt++ and a menu's letter, such as
++alt+f++ for **File**, opens that menu at once. The bar hides again once you choose an entry, press ++esc++ or click
elsewhere. **Always show the menu bar**, on the Interface page of [Settings](settings-dialog.md), keeps it shown.

### Find a command

| Menu | Holds |
| --- | --- |
| **File** | Opening, recent files, a new [sequence](../reference/glossary.md#sequence), saving, closing, the [projects](projects.md) and **Exit**. |
| **Edit** | **Undo** and **Redo**, which name the step, **Image History** and **Settings...**. |
| **View** | **Session Console**, the **Home** and **Integration** places, **Command Palette...**, **Readout**, **Night**, **Density**, and the keys that move between regions. |
| **Image** | What acts on the active image: **Actions**, **Convert Format**, **Extract Channel**, **Fast Geometry**, **Information**, **Mask**, **Sequence** and **View**, then **Show in File Browser**. |
| **Process** | The processes, by category, and **Apply**, which applies the task shown. |
| **Extensions** | **Manage Extensions...**, then a submenu for each installed extension. |
| **Window** | The layout commands of a workspace, the workspaces and the task shown. |
| **Help** | **User Guide**. |
| **About** | **About Qt**, **About AfterNight** and **System Information**. |

The commands of **Image** that act at once are on
[Non-Interactive Processes](../processes/utility/non-interactive-processes.md), and the windows of **Information**
are those of the [readout](../reference/glossary.md#readout)'s cards. The [Process Catalog](../processes/index.md) lists every process by the stage it
belongs to.

### Know why a command is greyed

- Without an image, most of **Image** and **Process** is greyed. **Color Spaces**, **Preprocess** and **Image List
  Analyzer** stay available, as do the commands of **File**, **Edit**, **View**, **Extensions**, **Help** and **About**
  that need no image.
- A grayscale image greys what needs color, such as **Extract Channel** and the color calibrations, and offers
  **Convert to RGB** instead.
- **Image > Sequence** needs a sequence, **EXIF Viewer** and **FITS Header** a file that stated their record, and
  **Plate Solve** a still image.
- A built-in process is greyed for no other reason. When it cannot open on the image, it says why in a message box, as
  [Tasks](tasks.md) describes.
- A process an extension adds is greyed while it loads, while it cannot run, or when it does not fit the image. Its
  tooltip says why.

### Act on the image you point at

Right-click a view. The click makes its image the active one first, so every command of the menu acts on it. The menu
holds **Actions**, **Process**, **Convert Format**, **Extract Channel**, **Fast Geometry**, **Information**, **Mask**,
**Sequence**, **View**, **Current Window** and, after a line, **Show in File Browser**. All but **Current Window** are
the menu bar's own, in the same state. **Current Window** groups, splits or fits the images of the view's workspace.

### Keep processes at hand

1. Choose **Process > Favorite Processes > Manage Favorites...**.
2. Type part of a name in **Search processes...**, tick the processes you want and choose **OK**.
3. Open **Process > Favorite Processes**. Your processes are listed by name, above **Manage Favorites...**.

**Recent Processes** fills itself, the last one first. The [command palette](../reference/glossary.md#command-palette) lists your favourites first, and its star
makes a process one.

### Change how the window looks

Choose **Night**, press ++ctrl+shift+n++ or choose **View > Night**. Choose **View > Density > Compact** or
**Comfortable** for smaller or larger bars and controls; the choice is kept. Presets, colours and tint strength are on
the Interface page of Settings.

### Get help and information

Press ++f1++ or choose **User Guide** for this guide. On a task's page, ++f1++ opens the guide at that process's page.
**About AfterNight** shows the version and the libraries AfterNight uses, and **System Information** a report of the
application and the computer.

## Controls

### The top bar

```controls
window: Top bar
controls:
  - name: No project
    kind: button
    default: ""
    effect: The project switcher. Opens its menu - the open project, ticked, then the projects used last, then **New project...**, **Open project...** and **Close project**.
    range: It reads the open project's name, cut in its middle when long, and **No project** while none is open.
    note: "**File > Switch Project...** opens the same menu. Projects describes what each entry does."
  - name: Where you are
    kind: readout
    default: Home
    effect: The path of what the centre shows - **Home**, **Integration**, a workspace and its active image, or a task's process and its target.
    range: A long name is cut in its middle, and the whole path is then its tooltip.
  - name: Night
    kind: toolbutton
    default: "Off"
    effect: Shows everything in red, pictures included, so that your eyes stay adapted to the dark.
    enabled: Always. ++ctrl+shift+n++ and **View > Night** do the same.
    persistence: Not kept. AfterNight starts out of it unless **Start in Night**, on the Interface page of Settings, is on.
    note: On Windows 11 the window's title bar turns dark too.
  - name: User Guide
    kind: toolbutton
    default: ""
    effect: Opens this guide, as **Help > User Guide** and ++f1++ do.
  - name: Information
    kind: toolbutton
    default: ""
    effect: Opens a menu of **About AfterNight**, **About Qt** and **System Information**, the entries of the **About** menu.
  - name: All menus
    kind: toolbutton
    default: ""
    effect: Opens every menu of the menu bar, from **File** to **About**, in one menu, whether the menu bar shows or not.
    note: Its tooltip reads "More". The command palette's **all menus** opens the same menu.
  - name: Search or run a command
    kind: button
    default: ""
    effect: Opens the command palette, as ++ctrl+k++ and **View > Command Palette...** do.
    range: A narrower window shows a shorter field, and a narrow one only its magnifier.
```

### The menu bar

```controls
window: The menu bar
controls:
  - name: Open
    kind: action
    default: ""
    effect: "**File > Open**: opens one or more files, each in a view of its own. Home says what the open shows and asks."
    enabled: Always. ++ctrl+o++ does the same.
  - name: Open Recent
    kind: action
    default: ""
    effect: Lists the recent files by their full path, the last one first, then **Clear recent files list**, which asks before it empties the list.
    enabled: Greyed while the list is empty.
    tradeoff: A picture of this menu shows your folders.
    persistence: The same list as Home's recent files, up to the **Recent Files Limit** of the General page of Settings.
    note: A file that can no longer be opened leaves the list after its "File open error".
  - name: New Sequence...
    kind: action
    default: ""
    effect: Opens New Sequence, which makes a sequence of files.
    enabled: Always. It has no shortcut.
  - name: Save as...
    kind: action
    default: ""
    effect: Saves the active image or sequence to a file you name. **Image > Actions > Save File...** and a tab's menu do the same.
    enabled: With an image. ++ctrl+shift+s++ does the same.
    tradeoff: The format you choose decides how the samples are stored. Saving And Exporting says what each keeps.
  - name: Close, Close All and Close All Others
    kind: action
    default: ""
    effect: Close the active view, every view, or every view but the active one.
    enabled: With an image. ++ctrl+w++ closes the active view. On a task's page **Close** and **Close All Others** are greyed.
    tradeoff: A view with unsaved changes asks "The image '...' has unsaved changes, are you sure you want to close it?". One with tasks open on it asks too, in a message titled "Close image with process attached" that names them, and the tasks close with it. **Close All** and **Close All Others** go on to the next view after a No.
  - name: New Project..., Open Project..., Switch Project..., Close Project and Project Settings...
    kind: action
    default: ""
    effect: Make a project, open one, open the project switcher's menu, close the open project, and open the Projects page of Settings.
    enabled: "**Close Project** while a project is open."
    note: Projects describes them.
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
    effect: Opens Settings. ++ctrl+comma++ and the rail's **Settings** do the same.
  - name: Session Console
    kind: check
    default: "Off"
    effect: "**View > Session Console** opens or closes the console's drawer above the status line."
    enabled: Always.
    persistence: Whether it is open, its height and whether it floats are kept between sessions.
  - name: Home and Integration
    kind: action
    default: ""
    effect: Show those places, as the rail's items do.
  - name: Command Palette...
    kind: action
    default: ""
    effect: Opens the command palette. ++ctrl+k++ does the same.
  - name: Readout
    kind: check
    default: "On"
    effect: Opens the readout column beside a workspace, or collapses it to its edge.
    persistence: Kept between sessions.
  - name: Density
    kind: radio
    default: Compact
    options: [Compact, Comfortable]
    effect: "**Comfortable** makes the window's bars, rows and controls larger, **Compact** smaller."
    persistence: Kept between sessions. The Interface page of Settings offers the same choice.
  - name: Next Region and Previous Region
    kind: action
    default: ""
    effect: Move the keyboard focus to the rail, the centre or the column, in that order or back.
    enabled: Always. ++f6++ and ++shift+f6++ do the same.
  - name: Window
    kind: action
    default: ""
    effect: "**Lock Layout**, **Unlock Layout**, **Group All Images**, **Split All Images** and **Fit Images To Window** for the workspace shown; **New Workspace**, **Rename Workspace...**, **Move Workspace to New Window** and **Close Workspace**; then **Float as Window**, **Attach** and **Close Task** for the task shown."
    enabled: The menu bar and **All menus** offer the whole menu greyed. Each command is in the command palette, and in the layout icons or the rail's menus.
    note: Workspaces and Tasks describe them.
  - name: User Guide
    kind: action
    default: ""
    effect: Opens this guide from the copy installed with AfterNight, else the online guide at `https://docs.afternightastro.org/`.
    enabled: Always. ++f1++ does the same, and on a task's page it opens that process's page.
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
    note: The top bar's **Information** opens the same window. Nothing in it uses the network.
  - name: System Information
    kind: action
    default: ""
    effect: Opens a report of the application, its build and the computer. Its Dependencies section lists the engine and its libraries as **About AfterNight** does.
    tradeoff: The report names details of your system. It is made on your computer and sent nowhere; read it before you share it.
  - name: Shortcuts
    kind: readout
    default: ""
    effect: "The menus show ++ctrl+o++, ++ctrl+shift+s++, ++ctrl+w++, ++ctrl+z++, ++ctrl+y++, ++ctrl+comma++, ++ctrl+k++, ++ctrl+shift+n++, ++f6++, ++shift+f6++, ++ctrl+enter++, ++f1++ and, under **Image > View**, ++ctrl+plus++, ++ctrl+minus++ and ++ctrl+0++. A view with the keyboard focus adds its own: ++f11++, ++ctrl+shift+f++, ++ctrl+h++, ++ctrl+shift+t++, ++ctrl+shift+x++, ++ctrl+shift+h++, ++ctrl+shift+m++, ++ctrl+shift+z++, ++ctrl+shift+p++, ++ctrl+p++ and ++ctrl+shift+l++, which Image Viewer describes."
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
    effect: "**Statistics**, **Histogram**, **EXIF Viewer**, **FITS Header**, **Astrometry Info**, **Plate Solve**, **Annotate Image...** and **Summary**, the windows the readout's cards open."
    enabled: "With an image. **EXIF Viewer** and **FITS Header** when the file stated that record, **Plate Solve** for a still image, **Annotate Image...** for a solved still image. **Astrometry Info** and **Summary** for any image."
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
    persistence: Kept between sessions as `interface/favoriteProcessIds`, the identifiers only, never values or images. The command palette's star changes the same list.
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
    enabled: Always. These tasks choose their images themselves.
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
  - name: Apply
    kind: action
    default: ""
    effect: Applies the task shown, as its **Apply** button does.
    enabled: ++ctrl+enter++ does the same, on either Enter key.
```

### Extensions

```controls
window: Extensions
controls:
  - name: Manage Extensions...
    kind: action
    default: ""
    effect: Opens the Extensions window on its Browse tab, to install, update, remove and configure extensions.
    tradeoff: Installing and updating use the network and disk space.
  - name: Extension submenu
    kind: action
    default: ""
    effect: One for each installed extension, by its name. It lists the extension's processes, the same entries as in their **Process** categories, then **Manage...**, which opens the Extensions window, and **Homepage**, **Repository** and **Support** when the package names them. Processes the extension adds while it runs follow, a group of them in a submenu of its own.
    range: A package that cannot be read has none.
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

The menu bar is not there
:   **Cause.** It shows only while you use it, after ++alt++.
:   **Fix.** Press and release ++alt++, or open **All menus**. **Always show the menu bar**, on the Interface page of
    Settings, keeps it shown.

The **Window** menu is greyed
:   **Cause.** The menu bar and **All menus** offer the whole **Window** menu greyed.
:   **Fix.** Press ++ctrl+k++ and type the command's name, or use the layout icons and the menus of a workspace's or a
    task's item in the [rail](../reference/glossary.md#rail).

Most of the Image and Process menus are greyed
:   **Cause.** No image is open, or none is active.
:   **Fix.** Open an image, or click its view.

A color command is greyed
:   **Cause.** The active image is grayscale.
:   **Fix.** Click the view of an RGB image, or use **Process > Adjustments > Convert to RGB** first.

A process of an extension is greyed or missing
:   **Cause.** It is still loading, cannot run, does not fit the image, or names a category the menu does not have.
:   **Fix.** Read its tooltip, wait for the extensions to load, or open **Extensions > Manage Extensions...** to repair
    or update it.

++f1++ opens the guide in the browser from the network, or says it cannot open it
:   **Cause.** The copy installed with AfterNight is missing or cannot be opened.
:   **Fix.** Install a package that holds the guide, or unset `AFTERNIGHT_DISABLE_ONLINE_USER_GUIDE` to allow the
    online guide.

## Related

- [The Main Window](overview.md), for the regions under the top bar.
- [Command Palette](command-palette.md), to run any command by name instead of finding it in a menu.
- [Workspaces](workspaces.md), for the layout icons and the **Window** menu's layout commands.
- [Projects](projects.md), for the project switcher and the project entries of **File**.
- [Using Extensions](../extensions/using-extensions.md), for the **Extensions** menu and the processes it adds.
- [Keyboard Shortcuts](../reference/keyboard-shortcuts.md), for every key by window.
