---
kind: process
stage: color
menu: Process > Color Calibration > Photometric Color Calibration
summary: >-
  Balances the red and blue of a plate-solved linear RGB image against its green by comparing its stars with their
  Gaia DR3 colors, and can make the background neutral.
data_in: linear
data_out: linear
destructive: true
preview: none
sequences: false
masks: post-blend
external: A Gaia DR3 catalog pack, downloaded once from the settings
glance:
  enabled: >-
    An RGB image view is active and no other process window is open on it: the window needs the image to itself.
    Apply is enabled once the image is broadband, plate solved and not marked nonlinear, and a Gaia catalog pack is
    installed.
  inputs: >-
    One three-channel RGB image of any pixel type with a plate solution in its header, the Gaia pack chosen in the
    settings, and an optional mask on its view. Over a sequence view, the frame it shows.
  outputs: >-
    The same view, replaced, with one history step. Its size, pixel type, plate solution, acquisition data and
    invalid pixels are kept, and it is recorded as color calibrated. Circles mark the stars the fit used. No file, preset or report is
    written.
  state: >-
    A stacked, gradient-corrected, cropped and plate-solved linear RGB master of broadband filters, before any
    stretch.
derived: true
rewritten: true
---
# Photometric Color Calibration

Photometric Color Calibration balances the colors of a [linear image](../../reference/glossary.md#linear-image) by its
stars. It finds the stars in each channel, places them on the sky through the image's
[plate solution](../../reference/glossary.md#plate-solution) and looks them up in a
[Gaia](../../reference/glossary.md#gaia) catalog. A star's Gaia color says how bright its red and its blue should be
beside its green. The window derives one gain for red and one for blue that bring the stars there, green keeping a
gain of 1, and can then make the background neutral.

**Apply** replaces the image, keeps its size, [pixel type](../../reference/glossary.md#pixel-type) and plate
solution, adds one step to its [history](../../reference/glossary.md#history) and records it as color calibrated.
The window has no preview.

```figure
image: ../../assets/images/processes/color-calibration/photometric-cc.webp
alt: >-
  The Photometric Color Calibration window. Under its title, a line saying it calibrates the RGB channel balance from
  Gaia DR3 star colors, shown twice. Then the Magnitude limit box at 14.0, the Match radius (arcsec) box at 5.0, the
  Apply background neutralization check box ticked, and the Background ROI Override group, unticked, whose Left, Top,
  Width and Height boxes read 0, 0, 7246 and 5429 beside a From Selection button, all greyed. Under the group a
  status line, then Target image naming RGB, the Show matched stars check box unticked, and Reset, Close and Apply
  along the bottom.
caption: >-
  Photometric Color Calibration on the plate-solved Carina master with a Gaia catalog installed, at its defaults and
  with Show matched stars off: the status line says the window is ready to calibrate.
pins:
  - {n: 3, x: 2.2, y: 67.8}
  - {n: 5, x: 2.2, y: 25.6}
  - {n: 6, x: 22.5, y: 36.4}
  - {n: 7, x: 2.2, y: 82.0}
  - {n: 8, x: 89.5, y: 90.3}
  - {n: 9, x: 2.0, y: 87.9}
```

## Before you start

- The image is a stacked linear RGB image of broadband filters, with three channels. A raw color-filter mosaic has
  one channel: [debayer](../../reference/glossary.md#debayer) it first.
- Remove gradients and crop the stacking borders first. Gradients, borders and a
  [stretch](../../reference/glossary.md#stretch) that nothing recorded bias the stars and the sky, and the window cannot
  tell such a stretch.
- The image needs a plate solution in its header, and the computer needs a Gaia catalog pack: steps 1 and 2.
  **Apply** makes no network request.
- Keep the exposure start in the header, as `DATE-OBS`: the catalog's stars are moved to that date by their proper
  motions.
- Bring floating-point samples to the range 0 to 1. They are read as they are stored, so a floating-point image on
  another scale is cut at 1.
- **Apply** replaces the image, and the window has no inverse. Its history keeps the step before.

## Steps

1. **Install a Gaia catalog once.** Choose **Edit > Settings...**, open the **Catalogs & Data** page, press
   **Download Catalog...** under **Gaia DR3 Astrometric Catalogs**, choose a pack and confirm the settings. The page
   then reads "Selected Gaia catalog: Gaia DR3 G<=14 (gaia_dr3_g14)" for the recommended pack.
2. **Plate solve the image.** An image whose header carries a plate solution, as the
   [master](../../reference/glossary.md#master) of the example below does, needs nothing. Otherwise solve it with
   Plate Solving, or with the window's own button in step 4.
3. **Open the window.** Make the linear RGB master the
   [active image](../../reference/glossary.md#active-image) and choose **Process > Color Calibration > Photometric Color
   Calibration**. The status line under the rectangle's boxes reads "Ready to calibrate photometric gains from
   Gaia-matched stars."
4. **Clear the banner, if one shows.** A banner names the first thing that is missing, and **Apply** stays disabled.
   For a missing plate solution or catalog, its button opens Plate Solving or the settings, and the banner clears
   when that is done.
5. **Set the catalog depth.** Leave **Magnitude limit** and **Match radius (arcsec)** at their defaults for a first
   run. Change them when a run says it found too few stars.
6. **Choose where the sky is measured.** Leave **Apply background neutralization** ticked. When nebula or a galaxy
   fills most of the frame, tick **Background ROI Override**, press **From Selection** and draw a rectangle over the
   emptiest sky on the image's view.
7. **Check the target.** **Target image** names the image that **Apply** replaces. Disable the view's
   [mask](../../reference/glossary.md#mask) unless you mean to calibrate part of the image.
8. **Apply.** Press **Apply**. The controls are disabled and the status line names each stage. A message then gives
   the number of stars the fit used and the red and blue gains, and the view shows the calibrated image.
9. **Check the matched stars.** With **Show matched stars** ticked, circles mark the stars the fit used. Most should
   be green and sit on stars: a yellow circle marks a star without a Gaia color. Note the count of stars, which only
   the status line and the message give, and untick the box to judge the image.
10. **Compare.** Look at the star colors, the background and the brightest stars at 100 %, in the view's
    [Autostretch](../../reference/glossary.md#autostretch) display with **Linked channels** on, so that one stretch
    serves the three channels, and compare the channels' statistics. The history lists **Photometric Color
    Calibration** with the values you set.

## Controls

The window has no preview, no histogram and no **Channel**. **Target image**, the mask and the busy message work as in
[Process Windows](../../interface/process-windows.md).

```controls
window: Photometric Color Calibration
controls:
  - name: Readiness banner
    object: warningBanner
    kind: readout
    default: ""
    effect: Names the first thing that keeps the window from calibrating the target. While it shows, every control but the banner's button, **Target image**, **Reset** and **Close** is disabled.
    range: It gives the message of the first check that fails, in the order of **The checks of the banner** below.
    enabled: >-
      Hidden while the target can be calibrated. It is checked again when the target changes or is edited, when you
      press **Reset**, when the settings opened from its button close, and after a run. While another edit holds the
      target, it keeps what it showed.
    tradeoff: It names what is missing, not how good a calibration would be.
  - name: Plate Solve...
    kind: button
    default: ""
    effect: >-
      Opens Plate Solving on the target, while the banner says the image is not plate-solved. When the banner names the
      catalog instead, the same button reads **Configure Gaia Catalog...** and opens the settings at their **Plate
      Solving** page, whose note sends you on to **Catalogs & Data**, where the Gaia packs are installed.
    enabled: >-
      Shown only with those two banners: the plate solve for a broadband RGB image without a plate solution, the
      catalog for an image that passes every other check. Disabled while a run works.
    tradeoff: >-
      A plate solve needs its own catalog and time, and writes the plate solution into the target's header. A pack is
      downloaded once and read from disk afterwards.
    persistence: The plate solution stays with the image and is saved with it. The settings are kept for every session.
    note: The banner is checked again when the plate solve writes its solution and when the settings close.
  - name: Magnitude limit
    kind: spinbox
    default: "14.0"
    widget: {min: 6, max: 20, step: 0.5, decimals: 1}
    effect: Sets the faintest Gaia G magnitude read from the catalog. Higher brings fainter stars into the match, lower keeps it to the brighter ones.
    range: >-
      A Gaia star without a G magnitude is never read. A limit fainter than the installed pack adds no star: the G<=14
      pack holds none fainter than 14. A very wide field read to a faint limit can hold more catalog stars than one run
      reads, and is refused.
    enabled: While the banner is hidden and no run works.
    tradeoff: >-
      Fainter stars help a sparse field, and add catalog reading, matching and faint, noisy measurements. Detection
      keeps at most 2,000 stars in each channel, brightest first, whatever the limit.
    scope: Apply, through the catalog stars it reads and matches.
    persistence: Until the window closes. **Reset** returns it to its default, Apply keeps it, and a window opened again starts at the default.
  - name: Match radius (arcsec)
    kind: spinbox
    default: "5.0"
    widget: {min: 0.5, max: 20, step: 0.5, decimals: 1}
    effect: Sets how far apart on the sky a green star and a Gaia star may lie and still be paired. Larger forgives a coarser plate solution, smaller keeps wrong neighbours out. It also widens the circle of catalog stars read by the same amount.
    range: Stars and Gaia stars are paired one to one, the closest pairs first. A red or blue star joins its green star within 3 pixels, a distance no control changes.
    enabled: While the banner is hidden and no run works.
    tradeoff: Too small loses true pairs where the solution is slightly off. Too large pairs stars with the wrong neighbour in a crowded field.
    scope: Apply, through the match of the green stars to the catalog.
    persistence: Until the window closes. **Reset** returns it to its default, and Apply keeps it.
    note: Change it only when the image's pixel scale or the accuracy of its plate solution asks for it.
  - name: Apply background neutralization
    kind: check
    default: "On"
    effect: On, each channel's sky is measured and offset so that the three skies meet at their mean level after the gains. Off, the channels are only multiplied by their gains, and the background takes the cast the gains give it.
    enabled: While the banner is hidden and no run works. Off disables **Background ROI Override** and ends **From Selection**.
    tradeoff: A clean sky loses its color cast. A sky full of nebula, a galaxy, a gradient or a border gives wrong offsets, a new cast and more clipping.
    scope: Apply, on the three channels, before the mask is blended.
    persistence: Until the window closes. **Reset** turns it on, and Apply keeps it.
    note: A channel without a valid sample where the sky is measured stops the run. The offsets are never set to zero in its place.
  - name: Background ROI Override
    kind: check
    default: "Off"
    effect: On, the sky is measured inside the rectangle of **Left**, **Top**, **Width** and **Height**. Off, over the whole image.
    enabled: With **Apply background neutralization** on, while the banner is hidden and no run works.
    tradeoff: A small clean rectangle keeps the nebula out of the measure, and is less representative and noisier than the whole frame.
    scope: Only where the sky is measured. The stars, the fit and the mask are not limited by it.
    persistence: Until the window closes. Turning it on starts from the whole image, turning it off empties the four boxes, and **Reset** turns it off.
  - name: Left
    kind: spinbox
    default: "0"
    widget: {min: 0, step: 1}
    effect: Sets the rectangle's left edge, in pixels of the image.
    range: Up to the image's width less one. Moving it lowers the largest **Width**.
    enabled: With **Background ROI Override** and **Apply background neutralization** on, while the banner is hidden and no run works.
    scope: The sky measured in all three channels.
    persistence: Until the window closes. **Reset**, another target and turning **Background ROI Override** off clear it.
  - name: Top
    kind: spinbox
    default: "0"
    widget: {min: 0, step: 1}
    effect: Sets the rectangle's top edge, in pixels of the image.
    range: Up to the image's height less one. Moving it lowers the largest **Height**.
    enabled: As **Left**.
    scope: The sky measured in all three channels.
    persistence: As **Left**.
  - name: Width
    kind: spinbox
    default: The image's width
    widget: {min: 0, step: 1}
    effect: Sets the rectangle's width.
    range: Up to the image's width less **Left**. Apply refuses a width of 0.
    enabled: As **Left**.
    tradeoff: A narrow rectangle holds few samples, and one structure in it can decide the sky.
    persistence: As **Left**. **Reset** and another target return it to the image's width.
  - name: Height
    kind: spinbox
    default: The image's height
    widget: {min: 0, step: 1}
    effect: Sets the rectangle's height.
    range: Up to the image's height less **Top**. Apply refuses a height of 0.
    enabled: As **Left**.
    tradeoff: A short rectangle holds few samples, and one structure in it can decide the sky.
    persistence: As **Left**. **Reset** and another target return it to the image's height.
  - name: From Selection
    kind: button
    default: "Off"
    effect: >-
      Pressed in, it empties the four boxes and lets you draw the rectangle on the target's view, and the boxes follow
      the rectangle as you draw or move it. Pressed again, it ends the drawing and keeps the rectangle, or returns to
      the whole image when none was drawn.
    enabled: As **Left**.
    tradeoff: It only places the rectangle. It shows no result and does not choose which stars are used.
    scope: The target's view, while it is pressed in.
    persistence: The rectangle stays until the window closes. Turning **Apply background neutralization** or **Background ROI Override** off ends the drawing.
  - name: Status line
    object: statusLabel
    kind: readout
    default: ""
    effect: >-
      Reads "Ready to calibrate photometric gains from Gaia-matched stars." when the window can calibrate. During a run
      it reads "Calibrating photometric gains from Gaia-matched stars..." and then names each stage with how far the
      run has come, as `matching green stars to the catalog`. After a run it reads "Calibrated with the Gaia color
      model." or "Calibrated with the neutral color default for most stars.", the number of stars the fit used and
      the gains to three decimals, as `Matched stars: <count>. Gains R=<red> G=1.000 B=<blue>`.
    range: The Gaia colors are named when more than half the stars the fit used have one.
    enabled: Empty while the banner shows.
    tradeoff: It leaves out the fit's residual, the offsets, the separations and each star's flux. The header records the offsets.
    persistence: The result stays until the next run, **Reset** or another target.
  - name: Target image
    kind: combo
    default: The active image
    options: [The active image, Every other open three-channel RGB image or sequence]
    effect: Chooses the image that **Apply** replaces. The banner, the bounds of the rectangle and the circles follow it.
    enabled: Disabled while a run works.
    tradeoff: Another field needs its own plate solution and catalog coverage, and may need another limit or rectangle.
    persistence: Until the window closes. Another target returns the rectangle to its whole image and removes the last result and its circles. The other values are kept.
  - name: Show matched stars
    kind: check
    default: "On"
    effect: Shows or hides the circles over the stars the last finished run used, on the target's view. Hiding them keeps them.
    enabled: While the banner is hidden and no run works.
    tradeoff: Many circles can hide the image. They change no pixel and no file.
    scope: The target's view only.
    persistence: Until the window closes. **Reset** ticks it and removes the circles.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window, and the circles leave the view with it.
    enabled: >-
      Stays enabled while a run works. Closing then stops the run: nothing is written, no step is added and no message
      shows. A run whose work had already ended is still shown and recorded, without its message.
    note: Closing the target's view also stops a run.
  - name: Reset
    kind: button
    default: ""
    effect: >-
      Returns **Magnitude limit**, **Match radius (arcsec)**, **Apply background neutralization**, **Background ROI
      Override**, the rectangle and **Show matched stars** to their defaults, ends **From Selection**, removes the
      circles and the result of the status line, and checks the target again.
    range: It leaves **Target image** as it is, and changes neither the image nor the settings.
    enabled: Disabled while a run works.
  - name: Apply
    kind: button
    default: ""
    effect: Calibrates the whole target with the values as they are when you press it, and replaces it. Nothing else can change the image while the run works.
    enabled: >-
      While the banner is hidden and no run works, at the defaults too. A rectangle of no size or outside the image is
      refused with "The PCC background ROI must be fully inside the target image and have a non-zero size." A target
      that changed since the banner was checked is refused with its reason, in a message titled **Photometric Color
      Calibration**.
    tradeoff: >-
      The run holds a floating-point working copy of the whole image beside the result, and one more copy with a mask,
      and the history keeps a copy of the step. The working copy alone is twice the image for 16-bit and 32-bit integer
      data and four times for 8-bit. The time grows with the pixels, the stars found and the catalog stars in the
      circle: the pack's files are mapped from disk and only the tiles the circle touches are read. It uses no graphics
      card, shows no progress window and has no **Cancel**. Try new settings on a cropped copy first, since Crop keeps
      the plate solution.
    scope: The whole image, all three channels, with the mask blended afterwards. On a sequence view, the frame it shows.
    persistence: >-
      The history records **Photometric Color Calibration** with `Magnitude limit: 14.0, Match radius: 5.0 arcsec,
      Background neutralization: on` for the defaults, without the gains or the count of stars. The header cards
      record `ANCCAL` as photometric, the gains in `ANCCGN`, the offsets in `ANCCOF`, the catalog's name in `ANCATF`,
      the method `pcc-gaia-bp-rp-v1` in `ANALGOV`, and the linearity and channel basis the image had in `ANINLIN` and
      `ANINBAS`, unknown where it stated none, with the whole record again in the `ANCJ` cards. Neither records the
      rectangle, the header records no limit or radius, and nothing keeps each star's flux, color or separation.
    note: >-
      The window keeps its values after Apply, and a second Apply calibrates the result again: it is not refused.
      Nothing of the window is kept between sessions.
```

```controls
window: The checks of the banner
controls:
  - name: RGB channels
    kind: readout
    default: ""
    effect: The target has three channels in the RGB layout.
    range: Otherwise the banner reads "Photometric color calibration only supports RGB images.", as an error, and offers no button.
    note: A grayscale image greys the menu command, and **Target image** lists only RGB images.
  - name: Broadband filter
    kind: readout
    default: ""
    effect: >-
      The filter name in the header does not read as narrowband. Ignoring case and the spaces around it, `Ha` or `SII`
      as the whole name or its first word, and `Halpha`, `H-alpha`, `OIII`, `S-II`, `narrow`, `dualband` or `triad`
      anywhere in it, read as narrowband.
    range: Otherwise the banner reads "Photometric color calibration is not applicable to narrowband data." and offers no button.
    tradeoff: A name it does not recognize, or no name, passes. A misleading name can let a narrowband image through or stop a broadband one.
  - name: Plate solution
    kind: readout
    default: ""
    effect: The header cards hold a plate solution the window can read, as the image's view reads it.
    range: Otherwise the banner reads "The image is not plate-solved. A valid WCS solution is required for photometric calibration." and the button reads **Plate Solve...**.
  - name: Linear data
    kind: readout
    default: ""
    effect: >-
      The image is not marked nonlinear. It is when its description says its samples are nonlinearly encoded, when
      its meaning states an sRGB, Rec. 709 or power-law transfer, or, where it states none, when its `ANLIN` card says
      nonlinear, as an AfterNight stretch records it.
    range: Otherwise the banner reads "Photometric color calibration requires linear data; this image is marked nonlinear." and offers no button.
    tradeoff: An image whose linearity nothing records passes, stretched or not.
  - name: Gaia catalog
    kind: readout
    default: ""
    effect: >-
      A complete Gaia pack is installed: the pack chosen on the **Catalogs & Data** page of the settings, in its
      **Catalog root:** folder, or in AfterNight's own data folder when that is empty. Its manifest and every chunk file
      the manifest lists must be there in full.
    range: Otherwise the banner reads "The Gaia DR3 catalog is not configured or not found. Configure it in the Plate Solving settings page." and the button reads **Configure Gaia Catalog...**.
    tradeoff: A download that did not finish, a catalog stored as tile files by an older version, or a folder without a manifest is not complete.
    note: The packs under **SPCC Calibration Data** on the same page are not used.
```

```controls
window: On the target's view
controls:
  - name: Matched star circles
    kind: readout
    default: ""
    effect: Draws a circle 8 screen pixels in radius where each star the fit used was measured in green. It is green where Gaia gave the star's BP - RP color, and yellow where the neutral color stood in.
    range: Every star with three usable fluxes has one, also a star whose proposed gain the fit then left out.
    enabled: After a run that finished, with **Show matched stars** ticked. The circles keep their size at every zoom.
    tradeoff: >-
      They are drawn over the view only: not into the image, its header or a file. Most should be green and sit on
      stars. Circles on empty sky, or beside the stars, point to a wrong plate solution.
    persistence: Until the next run, **Reset**, another target or the window closes.
  - name: Background rectangle
    kind: action
    default: ""
    effect: While **From Selection** is pressed in, drag on the target's view to draw the rectangle where the sky is measured, and drag it to move it. The four boxes follow it.
    scope: The sky measurement only.
```

```controls
window: Messages
controls:
  - name: Calibration completed successfully.
    kind: readout
    default: ""
    effect: "In a message titled **Photometric Color Calibration**, gives the number of stars the fit used and the gains, as `Matched stars: <count>` and `Gains: R=<red>  G=1.000  B=<blue>`."
    enabled: After a run that finished, unless the window was closed during it.
  - name: Error running photometric color calibration
    kind: readout
    default: ""
    effect: The title of the message that gives the reason a run failed, after "An error occurred when executing the photometric color calibration process:".
    enabled: After a failed run, unless it was stopped or the window was closed.
    persistence: The image and its history stay as they were.
  - name: OK
    kind: button
    default: ""
    effect: Closes either message. It neither undoes nor changes the result.
```

## How it works

1. When you press **Apply**, the window reads the image's plate solution from its
   [header cards](../../reference/glossary.md#header-card). It reads the Gaia stars in a circle about the field's
   center that reaches its corners and **Match radius (arcsec)** beyond, no fainter than **Magnitude limit**. Their
   proper motions move them from the catalog's epoch, J2016.0, to the exposure start the header records, taken as a
   Julian year from J2000.0. Without an exposure start they stay at J2016.0.
2. A working copy holds the image as [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale), in
   32-bit floating point, or 64-bit for a 64-bit or 32-bit integer image. A fixed star finder searches red, green and
   blue in turn and keeps at most 2,000 stars in each, brightest first. A star is the brightest point within 5 pixels
   around it, rises five times the noise above the sky and has a roundness of at least 0.4. It is fitted with a
   Gaussian profile, with the finder's optional shape and residual checks relaxed.
3. Each green star is placed on the sky and paired with a Gaia star within **Match radius (arcsec)**. It then takes
   the nearest free red and blue star within 3 pixels, and its [flux](../../reference/glossary.md#flux) is measured in
   all three channels.
4. Each star's red and blue against its green are compared with what its Gaia color predicts, and one red and one
   blue gain are drawn from all the stars. With **Apply background neutralization** on, each channel's sky is
   measured and given an offset.
5. Every valid sample is multiplied by its channel's gain, offset, held between 0 and 1 and written back in the
   image's pixel type, rounded to the nearest value. A mask on the view then blends the result with the original.
   The history records the step, the [processing state](../../reference/glossary.md#processing-state) records the
   image as color calibrated, and its [lineage](../../reference/glossary.md#lineage) gains an entry with the method,
   the gains and the offsets.

### Model

With \(w\) and \(h\) the width and height of the field on the sky, and \(\rho\) the value of **Match radius
(arcsec)**, the catalog is read within

\[ r = \tfrac{1}{2}\sqrt{w^{2} + h^{2}} + \rho \tag{1} \]

of the field's center. For a star found in one channel, with \(A\) the height of its fitted profile and \(s_x\) and
\(s_y\) its two widths in pixels, the flux is the volume under the profile:

\[ F = 2\pi\,\max(A, 0)\,\max(|s_x|, 0.5)\,\max(|s_y|, 0.5) \tag{2} \]

A star counts only when its three fluxes are finite and above \(10^{-9}\). With \(C\) the star's Gaia BP − RP color,
held between −0.5 and 3.5, or 0 where Gaia gives no BP or RP for it, the red and blue it should have beside a green
of 1 are

\[ E_R = e^{0.32\,C}, \qquad E_B = e^{-0.40\,C} \tag{3} \]

The relation is AfterNight's own, fitted over that range of colors. It uses no
[response profile](../../reference/glossary.md#response-profile) of a camera or a filter, and the G magnitude only
chooses which stars are read. A star of BP − RP 1.0 should read 1.38 in red and 0.67 in blue for every 1 in green.

With \(F_R\), \(F_G\) and \(F_B\) a star's three fluxes, the star proposes the gains

\[ q_R = \frac{E_R\,F_G}{F_R}, \qquad q_B = \frac{E_B\,F_G}{F_B} \tag{4} \]

For red, and then for blue, the gain is the median of the proposals that lie within three times \(\sigma\) of their
median. Here \(\sigma\) is 1.4826 times their
[median absolute deviation](../../reference/glossary.md#median-absolute-deviation), or 0.05 when that is smaller. When
no proposal lies there, the gain is the median itself. Green's gain is 1. The gains are not scaled to a largest of 1,
so either can be above 1.

With **Apply background neutralization** on, the window takes for each channel \(c\) the median \(M_c\) and the
deviation \(\sigma_c\), 1.4826 times the median absolute deviation, of the valid samples of the whole image or of the
rectangle. Its sky level \(B_c\) is the median of the samples between

\[ M_c - 2.8\,\sigma_c \quad\text{and}\quad M_c + 2.0\,\sigma_c \tag{5} \]

The range is not cut at 0, so a sky near zero keeps its negative samples. With \(P\) the mean of the three sky
levels, \(g_c\) the channel's gain and \(x\) one of its samples as a fraction of full scale, the result is

\[ x' = \min\Bigl(1,\ \max\bigl(0,\ P + g_c\,(x - B_c)\bigr)\Bigr) \tag{6} \]

Each channel's sky lands at \(P\), and the rest of the channel is scaled about it by the channel's gain. With
neutralization off, the result is \(g_c\,x\), held between 0 and 1.

### Boundaries

- A run fails and changes nothing when a channel yields no star, no green star matches a Gaia star, fewer than 10
  stars pair in all three channels, or the catalog holds no star in the circle. An image 28 pixels or less on a side
  cannot be searched, and a sample that is not a number, in a pixel the image counts as valid, stops the star finder.
  A catalog file that cannot be read fails the run with its name.
- Every result is held between 0 and 1 of full scale, in every pixel type. Gains above 1 and positive offsets clip
  the brightest stars, and negative offsets clip the shadows. Pixels the image marks as invalid are left unchanged. In
  an image whose zeros mean no data, a valid sample that would become 0 is written as the smallest positive value.
- The fit needs 10 stars and has no gate on its residual, on the spread of the stars' colors, on their coverage of
  the frame, on saturation and noise, or on Gaia's quality flags. It computes a residual that the window does not
  show. A star without a Gaia color takes the neutral color, \(C = 0\).
- The mask blends pixels only: the stars, the fit and the sky are measured on the whole image. The result is recorded
  as color calibrated even where the mask kept the original pixels.
- Over a [sequence](../../reference/glossary.md#sequence) view, the window calibrates the frame the view shows, with
  the plate solution that frame carries, as one step of the view's history. The other frames and the file are not
  changed: there is no run over every frame.

### Worked example

The master in the picture above is a linear 32-bit floating-point RGB image of the Carina Nebula, 7246 by 5429 pixels,
whose three channels have their median at 0.0277. Its header carries a plate solution at 1.095 arcseconds per pixel,
its [pixel scale](../../reference/glossary.md#pixel-scale), but no filter name and no exposure start. The window takes
it for broadband and keeps the catalog's epoch. With the G<=14 pack installed, leave the defaults, untick **Show
matched stars** so that no circle covers the picture, and press **Apply**.

The field is 132.2 by 99.2 arcminutes, so equation (1) reads the Gaia stars within 82.7 arcminutes of its center.
Equation (5) puts the sky of red, green and blue at 0.0271, 0.0273 and 0.0272, and \(P\) is 0.0272: equation (6)
holds the background there in every channel, whatever the gains. Above it, each channel is scaled by its gain. With a
gain of 1.10, for instance, a sample of 0.100 becomes \(0.0272 + 1.10 \times (0.100 - 0.0272) = 0.107\). The status
line gives the gains the fit found and the number of stars it used. Of the master's 118 million samples, 22,681 in
star cores lie above 1, and the result holds every one of them at 1 at most.

```figure
images:
  - image: ../../assets/images/processes/color-calibration/photometric-cc-before.webp
    alt: The whole frame before the calibration, the red and pink clouds of the nebula and a dense field of stars on a dark background.
    label: Before
  - image: ../../assets/images/processes/color-calibration/photometric-cc-after.webp
    alt: The whole frame after the calibration, the same nebula and stars with their colors balanced by the fitted gains, on a background of the same level.
    label: After
caption: >-
  The Carina Nebula master before and after Apply at the defaults, with Show matched stars off. Both are the whole
  frame as the view shows it in the Autostretch display with Linked channels on, which stretches the three channels
  with one curve and so keeps their balance.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A plate-solved linear master | The defaults | Check the circles and the gains, then compare at 100 % under one stretch. |
| A sparse or narrow field with too few stars | **Magnitude limit** 0.5 fainter at a time, within the pack's depth | **Match radius (arcsec)** only for a coarse plate solution. |
| Nebula or a galaxy across the frame | **Background ROI Override** with **From Selection** over the emptiest sky | Compare with **Apply background neutralization** off. |
| A correction meant for part of the image | A run without the mask | Undo, enable a soft-edged mask and apply once. A hard edge leaves a step in color. |

## When it goes wrong

!!! warning "Undo before you calibrate again"
    A second run is not refused, and it calibrates the calibrated image again, which adds to its clipping and color
    error. Undo the step in the image's history, change one setting, and apply again.

!!! warning "A finished run is not a check of its colors"
    The fit needs only 10 stars and shows no residual, no spread of the stars' colors and no coverage of the frame.
    Look at the circles and at the result before you go on to
    [Histogram Transformation](../stretching/histogram-transformation.md) or another stretch.

!!! warning "Use one of the two catalog calibrations"
    [Spectrophotometric Color Calibration](spectrophotometric-cc.md) takes the result for an image already color
    calibrated. Choose one of the two for an image, not both in turn.

"The image is not plate-solved. A valid WCS solution is required for photometric calibration."
:   **Cause.** The window found no plate solution in the image's header.
:   **Fix.** Press **Plate Solve...**, let the solve finish and check that the banner clears.

"The Gaia DR3 catalog is not configured or not found. Configure it in the Plate Solving settings page."
:   **Cause.** No complete Gaia pack is installed where the settings point.
:   **Fix.** Press **Configure Gaia Catalog...**, go on to the **Catalogs & Data** page, and press **Download
    Catalog...** or choose a **Catalog root:** that holds a complete pack.

The banner stays after you installed the catalog
:   **Cause.** The window checks the catalog when the settings opened from its own button close, not when you open
    them from the menu.
:   **Fix.** Press **Reset**, which checks the target and the catalog again.

"Photometric color calibration is not applicable to narrowband data." or "Photometric color calibration requires linear data; this image is marked nonlinear."
:   **Cause.** The header's filter name reads as narrowband, or the image is recorded as stretched.
:   **Fix.** Calibrate the linear broadband master: undo the stretch in its history or open the linear file. Correct a
    wrong filter name in the file before you open it.

"a channel yielded no star to calibrate on"
:   **Cause.** The star finder found no star in red, green or blue: the channel is empty, very faint or out of focus.
:   **Fix.** Look at each channel. An empty channel cannot be calibrated.

"no green star matched a catalog entry with a magnitude" or "the Gaia catalog holds no star in the region"
:   **Cause.** The plate solution is wrong, the pack does not cover the field, or **Magnitude limit** is brighter than
    the field's stars.
:   **Fix.** Solve the image again, then raise **Magnitude limit** by 0.5 at a time.

"fewer usable colour samples than the fit needs"
:   **Cause.** Fewer than 10 matched stars found a red and a blue star within 3 pixels with fluxes above zero.
:   **Fix.** Check the plate solution and that the channels are registered, then raise **Magnitude limit**. Widen
    **Match radius (arcsec)** only for a plate solution known to be coarse.

"Calibrated with the neutral color default for most stars." and many yellow circles
:   **Cause.** At most half the stars the fit used had a Gaia BP and RP color, so most took the neutral color.
:   **Fix.** Check that the installed pack carries colors for the field, and prefer a run whose circles are mostly
    green.

"The PCC background ROI must be fully inside the target image and have a non-zero size."
:   **Cause.** **Width** or **Height** is 0, as after **From Selection** with nothing drawn, or the rectangle leaves
    the image.
:   **Fix.** Draw the rectangle inside the image, or turn **Background ROI Override** off and on for the whole image.

The background keeps a cast, or gets one
:   **Cause.** Neutralization was off, or the sky it measured held nebula, a gradient or a border.
:   **Fix.** Undo, remove the gradients and crop the borders, measure the sky in a clean rectangle, and compare with
    neutralization off.

Stars or shadows clip after Apply
:   **Cause.** The gains and offsets took samples past 1 or below 0, where every pixel type holds them.
:   **Fix.** Undo, check the gains and the circles, bring a floating-point image to the range 0 to 1 first, and
    calibrate once.

"Calibration finished, but its result was not applied: the target image was closed or changed, or its history could not record the step."
:   **Cause.** The target closed or changed, or its history did not take the step, before the result could be shown.
:   **Fix.** Choose the target again and press **Apply**.

## Related

- [Plate Solving](../registration-alignment/plate-solving.md), which writes the plate solution the window needs.
- [Settings](../../interface/settings-dialog.md), for the Gaia packs on the **Catalogs & Data** page.
- [Background Extraction](../background-extraction/background-extraction.md), to remove gradients before the
  calibration.
- [Spectrophotometric Color Calibration](spectrophotometric-cc.md), which computes the stars' colors from their
  spectra through a model of your camera and filters.
- [Background Neutralization](background-neutralization.md) and [Color Calibration](color-calibration.md), to
  balance colors without a catalog.
- [Process Windows](../../interface/process-windows.md), for **Target image**, masks and the messages every process
  window shares.

## Credits

- [Gaia Data Release 3](https://gea.esac.esa.int/archive/documentation/GDR3/Miscellaneous/sec_credit_and_citation_instructions/),
  by the European Space Agency's Gaia mission, processed by the Gaia Data Processing and Analysis Consortium
  (ESA/Gaia/DPAC): the positions, proper motions, G magnitudes and BP and RP photometry the catalog packs are made
  from. Gaia data are open and free to use with credit to ESA/Gaia/DPAC, as its credit and citation instructions
  state.
- Gaia Collaboration, T. Prusti et al., [The Gaia mission](https://doi.org/10.1051/0004-6361/201629272), Astronomy &
  Astrophysics 595, A1 (2016).
- Gaia Collaboration, A. Vallenari et al.,
  [Gaia Data Release 3: Summary of the content and survey properties](https://doi.org/10.1051/0004-6361/202243940),
  Astronomy & Astrophysics 674, A1 (2023).
- ESA/Gaia/DPAC, [Gaia DR3 passbands](https://www.cosmos.esa.int/web/gaia/dr3-passbands), which define the G, BP and
  RP bands. The relation of equation (3) is AfterNight's own, not one of theirs.
- ESA/Gaia/DPAC, [Gaia DR3 documentation](https://gea.esac.esa.int/archive/documentation/GDR3/), for the catalog's
  reference epoch, J2016.0.
- K. M. Górski et al.,
  [HEALPix: A Framework for High-Resolution Discretization and Fast Analysis of Data Distributed on the Sphere](https://doi.org/10.1086/427976),
  The Astrophysical Journal 622, 759 (2005), whose numbering of the sky's tiles the catalog packs and their reader use.
