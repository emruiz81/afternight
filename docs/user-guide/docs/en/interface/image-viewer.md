---
kind: interface
summary: >-
  An image view shows one open image or sequence with a toolbar for zoom, channel, display mode, history, analysis
  and astrometry. What it shows never changes the image.
menu: Image > View
rewritten: true
---
# Image Viewer

Every image or [sequence](../reference/glossary.md#sequence) you open gets a view: a tab in the workspace with a
toolbar above the picture. The view decides how you see the image - how large, which channel, linear or stretched -
and none of that changes the image, its [history](../reference/glossary.md#history) or whether it needs saving. A process window shows its preview in the same kind of view, with a shorter
toolbar.

```figure
image: ../assets/images/interface/image-viewer.webp
alt: >-
  The AfterNight main window with one image view. Its tab is named after the Carina Nebula master. Under the tab, a
  toolbar of icons: four zoom buttons and a maximize button, a box reading RGB, a box reading Autostretch with a gear
  beside it, undo, redo and history arrows, five analysis icons and five astrometry icons. Below, the whole nebula fills
  the view, red and white clouds and dark lanes among many stars, with the word AUTOSTRETCH in a dark box at the top
  left. The status bar at the bottom shows the image's name, its size, color, sample type and memory, the zoom, and
  the sky coordinates under the pointer.
caption: >-
  The linear Carina master in its view, fitted to the window and shown in the Autostretch display mode.
pins:
  - {n: 1, x: 6.5, y: 9.8}
  - {n: 2, x: 4.5, y: 12.6}
  - {n: 3, x: 9.8, y: 12.6}
  - {n: 4, x: 23.8, y: 12.6}
  - {n: 5, x: 34.6, y: 12.6}
  - {n: 6, x: 40.5, y: 12.6}
  - {n: 7, x: 49.9, y: 12.6}
  - {n: 8, x: 12.5, y: 17.9}
  - {n: 9, x: 16.2, y: 97.6}
```

## What you see

1. **The tab.** The image's name: its file's name without the extension, with `_1`, `_2` added when another view
   has that name already, and an asterisk while it has unsaved changes. Drag the tab to move the view, as
   [Docking Panels](docking-panels.md) shows.
2. **The zoom buttons.** **Fit To Window**, **Zoom In**, **Zoom Out**, **Zoom 100%** and **Maximize image view**.
3. **View Channel.** What the view shows of a color image: all of it, one color, or a component such as CIE L\* or
   [HSV](../reference/glossary.md#hsv) Hue.
4. **Display Mode.** **Linear** shows the samples as they are, and [**Autostretch**](../reference/glossary.md#autostretch)
   [stretches](../reference/glossary.md#stretch) what you see. The gear beside it opens **Autostretch Settings**.
5. **The history buttons.** **Undo**, **Redo** and **Image History**, which move the image back or forward through
   its steps.
6. **The analysis buttons.** Windows about the image: its statistics, its histogram, its
   [EXIF](../reference/glossary.md#exif) record, its [FITS](../reference/glossary.md#fits) header and a summary. They
   open from **Statistics**, **Histogram**, **EXIF Viewer**, **FITS Header** and **Summary**.
7. **The astrometry buttons.** The overlays, the solution and an annotated picture of a plate-solved image, and
   **Plate Solve** for an image you want to solve.
8. **The picture.** The image at the view's zoom. In Autostretch, the word AUTOSTRETCH stands in its corner. A
   sequence adds its player under the picture: see [Sequences](sequences.md).
9. **The status bar.** The active view's size, color, sample type, memory and zoom, and on a solved image the sky
   coordinates under the pointer.

## What you can do

### Move around the image

- Choose **Fit To Window** to see the whole image. A new view opens fitted.
- Turn the mouse wheel to zoom around the pointer. The toolbar's buttons zoom around the middle of the view.
- Drag with the middle button to move a picture larger than the view.
- Press ++f11++ to give the view the whole workspace, and again to return.
- Right-click the picture for the **Image** menu's submenus, **Process** among them.

### See a linear image

A linear [master](../reference/glossary.md#master) looks nearly black, because its signal sits near the bottom of the
range. Choose **Autostretch** in **Display Mode**. The view measures the image and stretches what you see until the
background is a dark gray and the faint signal shows. The image, its history and the asterisk on its tab stay as
they were.

To tune the stretch, choose the gear beside **Display Mode**. The settings change the view only, and the picture
follows each change.

```figure
image: ../assets/images/interface/image-viewer-autostretch-settings.webp
alt: >-
  The Autostretch Settings window. A Pre Processing group holds Target Background and Shadows Clipping, each a value
  box and a slider, and the Linked channels, Neutralize Background and Color Calibration check boxes. A Post Processing
  group holds Saturation, Sat Bg., Brightness and Contrast. Under them, the Print Autostretch Banner check box and the
  Apply Stretch button, and Reset and Close at the bottom.
caption: >-
  Autostretch Settings for the Carina master's view, at its defaults.
```

!!! warning "Apply Stretch changes the image"
    **Apply Stretch**, in Autostretch Settings, is no display choice. After its question it writes the stretch into
    the image as a step of its history, and the view returns to **Linear**. Prefer Histogram Transformation or
    Generalized Hyperbolic Transformation for the stretch you keep.

### Look at one channel

Choose an entry of **View Channel**, such as **Red** or **CIE L\* (Lightness)**. The view shows that component in
gray, and the image stays RGB. Choose **RGB** to see the whole image again. A grayscale image offers only
**Grayscale**.

### Read the values under the pointer

Press and hold the left button over the picture. After about a third of a second, a lens opens beside the pointer:
the region around it magnified, a small histogram of that region, the position and the values. Move with the button
held to follow the pointer, and release it to close the lens.

In **Linear** the values are the image's own samples. In **Autostretch** they are what you see, after the stretch:
switch to **Linear** before you read a value you want to use. With a [mask](../reference/glossary.md#mask) on the
image, the lens shows the mask's value too.

### Use a preview

A process window's [real-time preview](../reference/glossary.md#real-time-preview) is a view without the history,
analysis and astrometry buttons. Its own buttons compare the result with the original, limit the preview to an area
and make a snapshot: [Process Windows](process-windows.md) describes them. Some windows, such as Image Integration's
list of frames, show a still preview of a file instead, whose buttons are in the last block below.

## Controls

### The view's toolbar

```controls
window: The view's toolbar
controls:
  - name: Fit To Window
    kind: toolbutton
    default: ""
    effect: Scales the image to fit the view and centers it. A small image is enlarged.
    enabled: With an image. ++ctrl+shift+f++ does the same, and **Image > View > Fit To Window** too.
    persistence: Zoom and position last while the view is open.
  - name: Zoom In
    kind: toolbutton
    default: ""
    effect: Enlarges the picture by a quarter, around the middle of the view.
    range: Up to 6400 %.
    enabled: With an image. **Image > View > Zoom In**, ++ctrl+plus++, does the same.
  - name: Zoom Out
    kind: toolbutton
    default: ""
    effect: Reduces the picture to four fifths, around the middle of the view.
    range: Down to 1 %.
    enabled: With an image. **Image > View > Zoom Out**, ++ctrl+minus++, does the same.
  - name: Zoom 100%
    kind: toolbutton
    default: ""
    effect: Shows the image at 100 %, one image pixel for one screen pixel when the system's display scaling is 100 %.
    enabled: With an image. **Image > View > Zoom 100%**, ++ctrl+0++, does the same.
  - name: Maximize image view
    kind: toolbutton
    default: "Off"
    effect: Gives the view the whole workspace and hides the rest of it. On, its tooltip reads **Restore image view**, and choosing it again puts the view back in its place.
    enabled: In a docked view of the workspace, not in a floating one. ++f11++ does the same, and **Image > View > Maximize View**.
    note: Docking Panels says what a maximized view locks.
  - name: View Channel
    kind: combo
    default: RGB
    options: [RGB, Red, Green, Blue, "CIE L* (Lightness)", "CIE a*", "CIE b*", "CIE a*=R / b*=G (Chrominance)", "CIE c*", "CIE h*", CIE X, CIE Y (Luminance), CIE Z, HSL Hue, HSL Saturation, HSL Lightness, HSV Hue, HSV Saturation, HSV Value, HSI Hue, HSI Saturation, HSI Intensity, YCbCr Luma, YCbCr Blue Chroma, YCbCr Red Chroma]
    effect: Shows one color or one component of a color image, in gray. **CIE a*=R / b*=G (Chrominance)** shows a* as red and b* as green over black.
    range: The components are computed from the image's red, green and blue - CIE through the exact inverse of the XYZ conversion, YCbCr with the unrounded BT.601 coefficients - and the component of an integer image is computed in floating point, not rounded to the image's codes.
    enabled: A grayscale image shows **Grayscale**, disabled, as its only entry.
    tradeoff: Each choice presents the image again, and measures it again in Autostretch.
    persistence: Kept while the view is open, and after a change to the image when the entry still applies. Otherwise the view returns to RGB or Grayscale.
  - name: Display Mode
    kind: combo
    default: Linear
    options: [Linear, Autostretch]
    effect: "**Linear** shows each sample as a fraction of full scale. **Autostretch** places the black point below each channel's median, by **Shadows Clipping** times its normalized median absolute deviation, and brings the median to **Target Background** with a midtones transfer. The AUTOSTRETCH banner appears on the picture."
    range: Every sample is held to the range 0 to 1 before anything else, so a signed integer below zero, and anything below zero, is black. A sample that is not a number, or that the image marks as invalid, is shown black and left out of the measure. The screen gets 8 bits per channel, each rounded to the nearest code.
    enabled: Disabled while the view's Autostretch Settings window is open.
    tradeoff: Autostretch measures the median and the deviation of every valid sample of the whole image, at full resolution, never of the reduced copy the view paints. The measure is kept for the image as it stands and its channel, and reused while you zoom, pan or change most of the settings. A change of the image, of **View Channel**, of **Neutralize Background** or of **Color Calibration** measures again.
    scope: The view only. The image, its history, its header and the asterisk on the tab are never changed.
    persistence: Every view opens in Linear and keeps its mode while it is open.
    note: The view paints what fits on the screen. Zoomed out, it presents a copy reduced by averaging blocks of pixels to about the size shown. When only part of the image shows, it presents that part with a margin, and panning presents the rest. Each presentation is computed in the background, a newer request replaces one still being computed, and the view keeps its picture until the new one is ready.
  - name: Autostretch Settings
    kind: toolbutton
    default: ""
    effect: Opens the Autostretch Settings window of this view.
    enabled: In Autostretch, while no Autostretch Settings window of this view is open. ++ctrl+shift+a++ does the same, and does nothing in Linear.
  - name: Undo
    kind: toolbutton
    default: ""
    effect: Takes the image back one step of its history. Its tooltip names the step.
    enabled: When the image has a step to take back. ++ctrl+z++ in the view does the same.
    note: Zoom, channel, display mode, overlays and the lens make no step, so Undo never takes them back.
  - name: Redo
    kind: toolbutton
    default: ""
    effect: Does again the step Undo took back. Its tooltip names the step.
    enabled: After an Undo. ++ctrl+y++ does the same.
  - name: Image History
    kind: toolbutton
    default: ""
    effect: Opens the image's history, with every step and the image it came from. See Image History.
    enabled: With an image. ++ctrl+shift+z++ does the same.
  - name: Statistics
    kind: toolbutton
    default: ""
    effect: Opens Image Statistics for the image. ++ctrl+shift+t++ does the same.
    note: Statistics, Histogram, EXIF Viewer, FITS Header and Summary open a new window at each choice.
  - name: Histogram
    kind: toolbutton
    default: ""
    effect: Opens Histogram Analysis for the image. ++ctrl+h++ does the same.
  - name: EXIF Viewer
    kind: toolbutton
    default: ""
    effect: Opens the camera fields the image's file stated.
    enabled: When the file stated an EXIF field. ++ctrl+shift+x++ opens the window even when the button is disabled, then empty.
  - name: FITS Header
    kind: toolbutton
    default: ""
    effect: Opens the FITS header of the image, keyword, value and comment.
    enabled: When the image carries FITS cards. ++ctrl+shift+h++ opens the window even when the button is disabled, then empty.
  - name: Summary
    kind: toolbutton
    default: ""
    effect: Opens the summary of the image, or of the sequence. ++ctrl+shift+m++ does the same.
  - name: Show Astrometry
    kind: toolbutton
    default: "Off"
    effect: Shows the overlays Astrometry Settings selects over a plate-solved image, or hides them all.
    enabled: Shown only for a solved still image, never for a sequence. ++ctrl+shift+w++ does the same.
    persistence: Off in every new view.
  - name: Astrometry Settings
    kind: toolbutton
    default: ""
    effect: "Opens the overlays' menu: **Equatorial Grid** (++ctrl+1++), **Deep-Sky Annotations** (++ctrl+2++), **Star Labels** (++ctrl+3++), **SIP Distortion Map** (++ctrl+5++) and **Detected Stars** (++ctrl+4++), then **Level of Detail**, **Labels**, **Palette** and **Text Size** for the annotations."
    enabled: While **Show Astrometry** is on. The shortcuts work only then.
    persistence: Every choice of the menu is kept between sessions for every view. The grid is on and the other four overlays off on a new profile.
    note: Detected Stars marks stars; they are not a selection you can edit. Astrometry Overlays describes each overlay.
  - name: Astrometry Info
    kind: toolbutton
    default: ""
    effect: Opens the solution's details. One window per view; choosing it again brings that window forward.
    enabled: Shown for a still image, enabled when it is solved. ++ctrl+shift+p++ does the same. **Image > Information > Astrometry Info** is enabled for any image, and says "No astrometric solution" for one not solved.
  - name: Annotate Image
    kind: toolbutton
    default: ""
    effect: Opens Annotate Image, to export a picture of the solved image with its objects, stars and coordinates named. One window per view.
    enabled: Shown for a solved still image. ++ctrl+shift+l++ does the same.
  - name: Plate Solve
    kind: toolbutton
    default: ""
    effect: Opens Plate Solve for this image. One window per view.
    enabled: Shown for every still image, never for a sequence. ++ctrl+p++ does the same.
  - name: More
    kind: toolbutton
    default: ""
    effect: Holds the buttons that no longer fit when the view is narrow, rightmost first. Its tooltip reads **More tools**.
    enabled: Shown only while some buttons do not fit.
    note: A button or box in the menu keeps its state and works as on the toolbar.
```

### The picture

```controls
window: The picture
controls:
  - name: Mouse wheel
    kind: action
    default: ""
    effect: Zooms in or out around the pointer, by a quarter for each notch at the default sensitivity.
    range: From 1 % to 6400 %.
    enabled: "With **Hold Ctrl key to zoom with mouse wheel** on, only while ++ctrl++ is held."
    persistence: "**Mouse Wheel Zoom Sensitivity**, from 1 to 100 with 50 by default, and the Ctrl setting are on the Interface page of Settings, under Image Viewing."
  - name: Middle-button drag
    kind: action
    default: ""
    effect: Moves the picture when it is larger than the view.
  - name: Hold the left button
    kind: action
    default: ""
    effect: After 300 ms, opens the lens beside the pointer. Release the button to close it.
    tradeoff: A sequence's playback pauses while the button is held.
  - name: Lens
    kind: readout
    default: ""
    effect: Shows the 24 by 24 pixels around the pointer magnified, a histogram of them, the position as **X** and **Y** to three decimals, and the values as **R**, **G** and **B**, or **K** for gray, to five decimals. With a mask on the view it adds the mask's value, **M**, or **M0** to **M2** for a color mask.
    range: In Linear a value is the image's own sample as a fraction of full scale. In Autostretch it is the stretched value the view shows, and a component shown in **View Channel** reads as **K**.
    tradeoff: Do not take Autostretch values for measurements.
    note: In a real-time preview the lens reads the image shown, the original when you switch to it. Right-clicking while the lens is open shows the original in it.
  - name: Right-click
    kind: action
    default: ""
    effect: Opens the view's menu, with the submenus of **Image**, **Process**, **Current Window** and **Show in File Browser**.
    note: It pauses a playing sequence. In a real-time preview it shows the original instead, while you hold the button.
  - name: Hover
    kind: action
    default: ""
    effect: Shows the right ascension and declination under the pointer in the status bar, over a plate-solved image.
  - name: Selection rectangles
    kind: readout
    default: ""
    effect: A process that asks for a part of the image draws it on the picture. A crop is an amber dashed rectangle with lines at its thirds and a cross at its center, an area a dashed rectangle in the accent color, and a preview area a red one with its label.
    range: Each is in the image's own pixels, stays inside the image and follows the zoom. The thirds and the cross show once the rectangle is larger than 50 screen pixels.
    note: The view has no selection of its own. The process's page says what its rectangle does.
  - name: Mask overlay
    kind: readout
    default: ""
    effect: Shows the image's mask over the picture, as **Image > Mask > Show Mask** and **Rendering Mode** choose - a red, green, blue, yellow, magenta or cyan tint, the mask itself with **Replace**, or the picture multiplied by the mask with **Multiply**.
    tradeoff: "**Multiply** multiplies the picture as the view shows it, stretched or not, so it is no measure of the mask's values."
    note: Showing the mask does not make a process use it. Masks describes both.
```

### Autostretch Settings

```controls
window: Autostretch Settings
controls:
  - name: Target Background
    kind: spinbox
    default: "0.25"
    widget: {min: 0, max: 1, step: 0.02, decimals: 2}
    effect: Sets where the median lands. Higher brightens the background and everything above it, lower darkens them.
    scope: The view only, as every setting of this window.
    persistence: Kept by the view while it is open, and shown again when the window reopens. Nothing is kept between sessions.
    note: The window's title names the view, `Autostretch Settings <name>`. Its first group is **Pre Processing**.
  - name: Shadows Clipping
    kind: spinbox
    default: "-2.80"
    widget: {min: -10, max: 0, step: 0.2, decimals: 2}
    effect: Places the black point this many normalized deviations from the median. Further from zero keeps more of the shadows, nearer to zero clips more of them to black.
    note: When a deviation measures zero, a small one is used instead. An image whose medians lie in the upper half is stretched the other way round, from its highlights.
  - name: Linked channels
    kind: check
    default: "Off"
    effect: "On, the three channels get one stretch from their pooled statistics, which keeps the image's color balance. Off, each channel gets its own stretch, which neutralizes the background's color."
    enabled: For an RGB image.
  - name: Neutralize Background
    kind: check
    default: "Off"
    effect: Moves each channel's background to the mean of the three before the stretch.
    enabled: For an RGB image, with **Linked channels** on. A sequence opens the window with it disabled, but a change of the frame can enable it again; leave it off for a sequence.
    tradeoff: Turning it on or off measures the image again.
  - name: Color Calibration
    kind: check
    default: "Off"
    effect: Scales each channel about its background so that its whites stand as high above it as the strongest channel's, before the stretch.
    enabled: As **Neutralize Background**.
    tradeoff: Turning it on or off measures the image again.
  - name: Saturation
    kind: spinbox
    default: "0.00"
    widget: {min: -1, max: 2, step: 0.05, decimals: 2}
    effect: Above zero, saturates the colors of the stretched picture, and 1 doubles their saturation. Below zero, mutes them. Its group, **Post Processing**, acts after the stretch.
    enabled: For an RGB image.
  - name: Sat Bg.
    kind: spinbox
    default: "1.00"
    widget: {min: 0, max: 2, step: 0.02, decimals: 2}
    effect: Sets the lightness below which **Saturation** leaves pixels alone, as a multiple of the stretched background's level plus its deviation. Higher protects more of the dark background.
    enabled: For an RGB image.
  - name: Brightness
    kind: spinbox
    default: "0"
    widget: {min: -150, max: 150, step: 1}
    effect: Adds the level over 255 to every value of the stretched picture.
  - name: Contrast
    kind: spinbox
    default: "0"
    widget: {min: -100, max: 100, step: 1}
    effect: Spreads the values of the stretched picture away from the middle gray, or above zero, draws them toward it below zero.
  - name: Print Autostretch Banner
    kind: check
    default: "On"
    effect: Shows or hides the AUTOSTRETCH banner on the picture.
    persistence: Kept by the view while it is open.
  - name: Apply Stretch
    kind: button
    default: ""
    effect: Asks "Do you want to apply the current autostretch preview settings to this image permanently?" and, on Yes, writes the stretch shown into the image. The image keeps its type, size, FITS header and plate solution, and is recorded as nonlinear with the operation `autostretch`.
    enabled: In a standard view only. A preview's window has no such button.
    tradeoff: It is computed in the background on a copy, recorded in the history as **Autostretch**, and only then shown. When the history cannot keep it, nothing changes, a message says why and the window stays open.
    scope: A still image, or the frame of a sequence shown, never the whole sequence.
    persistence: After it, the view returns to Linear, the settings to their defaults, and the window closes. Undo takes the step back.
  - name: Reset
    kind: button
    default: ""
    effect: Returns every value and check box of the window to its default, except **Print Autostretch Banner**.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window and keeps the settings in the view. Nothing is written to the image.
```

### The status bar

```controls
window: The status bar
controls:
  - name: Image information
    kind: readout
    default: ""
    effect: Shows the active image's width and height, RGB or Grayscale, sample type, and memory, such as `[7246x5429 (RGB)] - [32 bit floating point] - [450.19 MB]`.
    range: A sequence shows its kind first, such as `SER SEQUENCE`, then the frame's size, the frame shown and their count, the frame rate, the sample type and the memory of every frame together.
    note: The sample types read 8, 16 or 32 bit signed or unsigned integer, and 32 or 64 bit floating point.
  - name: Zoom
    kind: readout
    default: ""
    effect: Shows the active view's zoom with two decimals, such as `25.36 %`.
  - name: RA and Dec
    kind: readout
    default: ""
    effect: Shows the sky coordinates under the pointer in hours and degrees. Their tooltips give them in degrees with six decimals.
    enabled: Over a plate-solved image in a standard view. They disappear when the pointer leaves it.
```

### Still preview

```controls
window: Still preview
controls:
  - name: Calibrated
    kind: check
    default: "Off"
    effect: Shows the light frame calibrated by its masters instead of as it was recorded. Its tooltip names the masters and says whether it is on.
    enabled: Shown only in a window that offers it, and enabled when the window found masters for the frame.
  - name: Display Mode
    kind: combo
    default: Linear
    options: [Linear, Autostretch]
    effect: As in a view, with its Autostretch Settings beside it.
    enabled: Unless the window hides it, as it may hide **Show statistics**. The window chooses the mode it starts in.
  - name: Show statistics
    kind: toolbutton
    default: "On"
    effect: Shows or hides the overlay of the picture's minimum, maximum, mean, median and standard deviation.
    note: The window reads a file into its preview when you choose one in its list. A newer choice replaces a file still being read, and a file that cannot be read is reported and shows nothing. A mosaic is demosaiced by the fast method, without a question, when the window shows color.
```

## When it goes wrong

The image is almost black
:   **Cause.** It is linear, and **Linear** shows its samples as they are.
:   **Fix.** Choose **Autostretch** in **Display Mode** to look at it, and a stretch process to change it.

The gear of Autostretch Settings is greyed
:   **Cause.** **Display Mode** is **Linear**, or the view's settings window is already open.
:   **Fix.** Choose **Autostretch**, or bring the open window forward.

The mouse wheel scrolls instead of zooming
:   **Cause.** **Hold Ctrl key to zoom with mouse wheel** is on in Settings.
:   **Fix.** Hold ++ctrl++ while you turn the wheel, or turn the setting off.

**Neutralize Background** and **Color Calibration** are greyed
:   **Cause.** The image is grayscale, the view shows a sequence, or **Linked channels** is off.
:   **Fix.** Turn **Linked channels** on for an RGB image. Leave both off for a sequence.

The astrometry buttons are missing
:   **Cause.** The image is not plate-solved, or it is a sequence.
:   **Fix.** Solve a still image with **Plate Solve**, then turn **Show Astrometry** on before you open its settings.
    An image mirrored or turned has lost its solution: solve it again.

Buttons disappear from the toolbar
:   **Cause.** The view is too narrow, and they moved into **More**.
:   **Fix.** Open **More**, or widen the view.

## Related

- [Interface Overview](overview.md), for the main window around the views.
- [Process Windows](process-windows.md), for the real-time preview at work and what Apply does.
- [Astrometry Overlays](astrometry-overlays.md), for the grid, the annotations and the stars a solved image shows.
- [Masks](masks.md), for the mask shown over a view and the way it renders.
- [Image History](image-history.md), for the steps Undo and Redo move through.
- [Histogram Transformation](../processes/stretching/histogram-transformation.md), to stretch the image itself.
