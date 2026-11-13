---
kind: process
stage: color
menu: Process > Color Calibration > Spectrophotometric Color Calibration
summary: >-
  Balances the colors of a plate-solved linear RGB image against star colors computed from Gaia DR3 XP spectra through
  your camera's sensor and filter curves, and can make the background neutral.
data_in: linear
data_out: linear
destructive: true
preview: none
sequences: true
masks: post-blend
external: A Gaia DR3 XP spectra catalog and camera response curves, downloaded from the settings or when a run needs them
glance:
  enabled: >-
    An RGB image or sequence view is active and no other process window is open on it: the window needs the image to
    itself. Apply is enabled once the banner's checks pass: the catalog, the response data, a chosen response profile
    and a linear image.
  inputs: >-
    One three-channel RGB image or sequence of any pixel type with a plate solution in its header, the spectra catalog
    and response data the settings point to, and an optional mask on its view.
  outputs: >-
    The same view, replaced, with one history step, recorded as color calibrated. Its size, pixel type, plate solution
    and acquisition data are kept. No file, preset or report is written.
  state: >-
    A stacked, gradient-corrected and plate-solved linear RGB master of broadband filters, before any stretch and
    before any other color calibration.
derived: true
---
# Spectrophotometric Color Calibration

Spectrophotometric Color Calibration balances the colors of a [linear image](../../reference/glossary.md#linear-image)
by its stars. It finds the stars in the green channel, places them on the sky through the image's
[plate solution](../../reference/glossary.md#plate-solution) and pairs them with stars of a
[Gaia](../../reference/glossary.md#gaia) catalog that holds each star's spectrum. Each spectrum, integrated through
your camera's [response profile](../../reference/glossary.md#response-profile), predicts the star's red and blue
against its green. A robust fit of the measured colors against the predicted ones, read at a
[white reference](../../reference/glossary.md#white-reference), gives the three channel gains.

**Apply** replaces the image, keeps its size, [pixel type](../../reference/glossary.md#pixel-type) and plate solution,
adds one step to its [history](../../reference/glossary.md#history) and records it as color calibrated. The window has
no preview.

```figure
image: ../../assets/images/processes/color-calibration/spectrophotometric-cc.png
alt: >-
  The Spectrophotometric Color Calibration window. Under its title, a line saying it calibrates linear RGB images from
  Gaia XP sampled spectra and response profiles, then a status line saying it is ready to run, with a long profile
  name. In a scrolling panel, a green banner saying the prerequisites are ready, the Sensor box at Mono sensor: Sony
  IMX492, the Red, Green and Blue filter boxes at the ZWO filter set, the White reference box at Average Spiral Galaxy
  and a line that sums up the response. Below them, the folded Catalog and WCS Preflight and Photometry and Fit
  sections. Target image names RGB, and Reset, Close and Apply run along the bottom.
caption: >-
  Spectrophotometric Color Calibration on the plate-solved Carina master after Assign Image State, with the Sony
  IMX492 sensor and the ZWO filter set chosen: the banner and the status line say a run can start.
pins:
  - {n: 3, x: 2.5, y: 32.8}
  - {n: 5, x: 2.5, y: 40.4}
  - {n: 6, x: 2.5, y: 45.6}
  - {n: 7, x: 2.5, y: 60.3}
  - {n: 8, x: 2.5, y: 72.8}
  - {n: 10, x: 2.5, y: 88.7}
```

## Before you start

- The image is a stacked linear RGB image of broadband filters, with three channels. Nothing may have stretched or
  color calibrated it, Photometric Color Calibration included. A raw color-filter mosaic has one channel:
  [debayer](../../reference/glossary.md#debayer) it first.
- Remove gradients and crop the stacking borders first: the sky is measured over the whole image.
- The image needs a plate solution in its header. The window checks it only when you press **Apply**.
- The run needs a catalog of Gaia spectra and the response data of cameras and filters, which step 1 installs. The
  Gaia pack that plate solving and Photometric Color Calibration read is not used here.
- Know your camera's sensor and the filters it imaged through: the colors are only as right as the response you
  choose.
- **Apply** replaces the image, and the window has no inverse. Its history keeps the step before; keep the linear
  [master](../../reference/glossary.md#master) file too.

## Steps

1. **Install the calibration data once.** Choose **Edit > Settings...**, open **Catalogs & Data** and, under
   **SPCC Calibration Data**, press **Download Full Offline Catalog...**, which installs the response data with it.
   Without it, the window's buttons and the first run download what the image needs.
2. **Plate solve the image.** An image whose header carries a plate solution, as the example's master does, needs
   nothing. Otherwise solve it with Plate Solving.
3. **Open the window.** Make the linear RGB master the [active image](../../reference/glossary.md#active-image) and
   choose **Process > Color Calibration > Spectrophotometric Color Calibration**. The banner and the status line under
   the title name the first thing that keeps a run from starting, or say that it can start.
4. **Clear what the banner names.** Press the button it offers: **Download Response Profiles** for the response data,
   **Assign Image State** for an image whose state is unknown, or a catalog button. The banner is decided again after
   each.
5. **Choose the sensor.** In **Sensor**, choose your camera's sensor. The boxes of its filters appear under it.
6. **Choose the filters.** For a mono camera, choose the same filter set in **Red filter**, **Green filter** and
   **Blue filter**. For a color camera, choose **OSC filter** and, for a DSLR, **DSLR LPF**. The line under the boxes
   then names the response and reads `Selection: manual`.
7. **Check the white reference.** Leave **White reference** at the profile's own unless you mean another balance.
8. **Leave the two sections folded for a first run.** **Catalog and WCS Preflight** and **Photometry and Fit** open
   with their defaults. Change them when a run fails or **Diagnostics** gives guidance.
9. **Check the target.** **Target image** names the image that **Apply** replaces. Disable the view's
   [mask](../../reference/glossary.md#mask) unless you mean to calibrate part of the image.
10. **Apply.** Press **Apply**. A progress window names each stage, then a message gives the number of samples and the
    three gains, as `SPCC completed. Samples: <count>. Gains R=<red> G=<green> B=<blue>`. Press **OK**, and the view
    shows the calibrated image.
11. **Read the diagnostics.** **Diagnostics** gives the quality score, the matches and their distances, the samples,
    the gains and the offsets. A guidance line means the fit had few stars.
12. **Compare.** Look at the star colors and the background in the view's
    [Autostretch](../../reference/glossary.md#autostretch) display with **Linked channels** on, so that one
    [stretch](../../reference/glossary.md#stretch) serves the three channels. The history lists
    **Spectrophotometric Color Calibration** with the profile you chose.

## Controls

The window has no preview, no histogram and no **Channel**, and its messages call the process
[SPCC](../../reference/glossary.md#spcc). **Target image**, the mask, the progress window and the messages every window
shares work as in [Process Windows](../../interface/process-windows.md). The boxes from **Sensor** to
**White reference** appear once response data is installed.

```controls
window: Spectrophotometric Color Calibration
controls:
  - name: Status line
    object: spccProcessStatusLabel
    kind: readout
    default: ""
    effect: >-
      Says what a run would do now. Ready, it reads "Ready to run SPCC with response profile <profile> and white
      reference <reference>.", naming both by their identifiers, or "Ready to run SPCC; AfterNight will download needed
      catalog chunks first." Blocked, it repeats the banner.
    range: >-
      During a run it reads "Preparing SPCC workflow..." and then "Running spectrophotometric color calibration...".
      Afterwards it reads "SPCC completed. Samples: <count>. Gains R=<red> G=<green> B=<blue>", "SPCC failed." with the
      reason, "SPCC cancelled." or "SPCC completed, but the target image was closed or changed before the result could
      be applied." Over an image that is not RGB it reads "Select an RGB target image to evaluate SPCC readiness."
    enabled: During a download it counts the files and bytes, as `Downloading SPCC catalog chunks in parallel... 3/12`.
    persistence: The result of a run stays while the view shows the image the run calibrated. An edit of the image or another target replaces it.
  - name: Download progress
    object: spccProcessProgressBar
    kind: readout
    default: ""
    effect: Shows how far a download of response data or catalog chunks has come.
    enabled: Shown only while the window downloads. A run shows a progress window of its own.
  - name: Readiness banner
    object: spccProcessReadinessBanner
    kind: readout
    default: ""
    effect: >-
      Names the first thing that keeps a run from starting, in the order of **What the banner asks for** below, or says
      that a run can start. It is green when a run can start, amber when only the choice of a response profile is
      missing or the image is already color calibrated, and red otherwise.
    range: >-
      Ready, it reads "SPCC prerequisites are ready." with the profile and white reference it resolved, or "SPCC can
      start; needed catalog chunks will download first."
    enabled: Hidden while the view shows the image this window has just calibrated.
    tradeoff: >-
      It is decided away from the window each time the target, its state, the settings, **Match radius (arcsec)**,
      **Choose magnitude limit automatically** or a choice of response changes, and after **Reset** and a run. Only the
      latest decision is shown, and **Apply** waits for it. Each decision reads the catalog's list of files, without
      checking them, and builds every profile of the response data, so a large set of response data makes it slower.
      It says whether a run can start, not how good its colors will be.
  - name: Sensor
    kind: combo
    default: Select sensor
    options: [Select sensor, "Mono sensor: <name>", "OSC sensor: <name>"]
    effect: >-
      Chooses the camera sensor whose response the star colors are computed through. A mono sensor shows the **Red
      filter**, **Green filter** and **Blue filter** boxes, a color sensor **OSC filter** and **DSLR LPF**.
    range: >-
      It lists every sensor of the installed response data. Choosing one selects its profile at once, with the first
      choice of each filter box.
    enabled: >-
      Shown once response data is installed whose profiles name their sensor and filters, as the AfterNight SPCC
      Response Baseline 2026a does. Disabled while a run or a download works.
    tradeoff: The wrong sensor biases every predicted color, and so every gain, without a message.
    scope: Apply, all three channels.
    persistence: >-
      A choice made here is saved in the settings for the camera and the filter the image's header names, and selects
      itself for the next image of that camera and filter. Choosing again replaces it. An image whose header names no
      camera keeps nothing.
    note: It opens on the sensor of a profile that the header or a saved choice resolved.
  - name: OSC filter
    kind: combo
    default: No OSC filter
    options: [No OSC filter, "OSC filter: <name>"]
    effect: Chooses the filter in front of a color sensor, whose curve then multiplies the sensor's.
    enabled: For a color sensor that has a filter to choose.
    tradeoff: A curve that is not the one in your light path, such as a light-pollution filter left out, shifts the predicted colors.
    scope: Apply, all three channels.
    persistence: Saved with the choice of the sensor.
  - name: DSLR LPF
    kind: combo
    default: No DSLR LPF
    options: [No DSLR LPF, "DSLR LPF: <name>"]
    effect: Chooses the low-pass filter or window in front of a color sensor, as a DSLR body has.
    range: >-
      A sensor, filter and low-pass filter that the data holds as no single profile are combined from three profiles:
      the sensor alone, the sensor with the filter and the sensor with the low-pass filter. All three must name one
      white reference.
    enabled: For a color sensor that has a low-pass filter to choose.
    tradeoff: As **OSC filter**.
    scope: Apply, all three channels.
    persistence: Saved with the choice of the sensor.
  - name: Red filter
    kind: combo
    default: "Red filter:"
    options: ["Red filter:", "Red filter: <name>"]
    effect: Chooses the red filter of a mono camera's set.
    range: >-
      The first item, with no name after it, is the sensor without a filter. A profile is selected only when the three
      boxes name a set the data holds; until then the profile chosen last stays.
    enabled: For a mono sensor.
    tradeoff: A wrong curve biases red against green, and so the red gain.
    scope: Apply, the red channel's curve.
    persistence: Saved with the choice of the sensor.
  - name: Green filter
    kind: combo
    default: "Green filter:"
    options: ["Green filter:", "Green filter: <name>"]
    effect: Chooses the green filter of a mono camera's set.
    range: As **Red filter**.
    enabled: For a mono sensor.
    tradeoff: Green is what both colors are measured against, so a wrong curve moves both gains.
    scope: Apply, the green channel's curve.
    persistence: Saved with the choice of the sensor.
  - name: Blue filter
    kind: combo
    default: "Blue filter:"
    options: ["Blue filter:", "Blue filter: <name>"]
    effect: Chooses the blue filter of a mono camera's set.
    range: As **Red filter**.
    enabled: For a mono sensor.
    tradeoff: A wrong curve biases blue against green, and so the blue gain.
    scope: Apply, the blue channel's curve.
    persistence: Saved with the choice of the sensor.
  - name: Response profile
    kind: combo
    default: Select response profile
    options: [Select response profile, "<profile name>"]
    effect: Chooses a whole profile by its name, for response data whose profiles do not name their sensor and filters.
    enabled: In place of the sensor and filter boxes, for such data only. The AfterNight SPCC Response Baseline 2026a never shows it.
    tradeoff: A profile is a measurement of a light path, not a look. Choose the one of your camera and filters.
    scope: Apply, all three channels.
    persistence: As **Sensor**.
  - name: White reference
    kind: combo
    default: Select white reference
    options: [Select white reference, Average Spiral Galaxy, Every other white reference of the response data]
    effect: >-
      Chooses the spectrum that counts as white: a star with that spectrum comes out with equal red, green and blue.
      Each profile names its own, which a choice of sensor fills in.
    range: >-
      The 2026a baseline holds 145 spectra: galaxies, among them Average Spiral Galaxy, which every profile names, and
      stars of many spectral types, among them `Star, type G2(v)`, a star like the Sun.
    enabled: Shown once response data is installed. A run needs one.
    tradeoff: Another legitimate reference gives another balance, warmer or cooler. Neither is an error.
    scope: Apply, the gains. The history records it.
    persistence: Saved with the choice of the sensor. **Reset** clears a choice made in this window.
  - name: Response details
    object: spccProcessResponseProfileDetailsLabel
    kind: readout
    default: ""
    effect: >-
      Sums up what a run would use, as `Response: Mono sensor: Sony IMX492 / RGB filters: <set> | White reference:
      Average Spiral Galaxy | Selection: manual`.
    range: >-
      `Selection` reads `manual` for a choice made in this window, `saved mapping` for one saved earlier and
      `image metadata` for a profile that the header's camera and filter named.
    enabled: Shown once a profile is selected.
  - name: Open Catalogs Data...
    kind: button
    default: ""
    effect: >-
      Opens the settings at their **Catalogs & Data** page, where the catalogs are installed, chosen, repaired and
      deleted. The banner is decided again when the settings close.
    enabled: >-
      Shown when the catalog needs you: none installed and none the run can download, a catalog whose rights review is
      not approved, or one that does not cover the field and cannot download what it lacks.
    tradeoff: What you change there holds for every later window.
  - name: Download Offline Catalog
    kind: button
    default: ""
    effect: Opens the same page, where **Download Full Offline Catalog...** installs the whole catalog. It starts no download itself.
    enabled: Shown with **Open Catalogs Data...**.
    tradeoff: The whole catalog is a download of about 45 GB and takes about 48 GB on disk. It lets every field run without the network.
  - name: Download Needed Region
    kind: button
    default: ""
    effect: >-
      Saves the image's field, with **Magnitude cap** as its depth, as a pending region, and opens **Catalogs & Data**,
      which offers **Download Matching Region...** when an approved regional pack covers it.
    enabled: Shown when the installed catalog does not cover the field and cannot download what it lacks.
    tradeoff: A regional pack is far smaller than the whole catalog. When none covers the field, the run stays blocked.
    persistence: The pending region stays in the settings until a pack that covers it is installed.
    note: >-
      An image without a plate solution is refused with "Load a plate-solved image before requesting a regional SPCC
      catalog cache.", and settings that cannot be saved with "AfterNight could not save the regional SPCC catalog
      request in global settings."
  - name: Download Response Profiles
    kind: button
    default: ""
    effect: >-
      Downloads the approved response data, the AfterNight SPCC Response Baseline 2026a of about 26 MB, checks it
      against its checksums and installs it. The sensor and filter boxes then appear, and the status line reads "SPCC
      response profiles downloaded. Choose the sensor and filter response data."
    enabled: Shown when no response data, or only part of it, is installed and an approved download exists.
    tradeoff: It needs the network once. A failed or cancelled download installs nothing and keeps what was there.
    persistence: The data's folder is kept in the settings for every later window. The choices made in this window are cleared.
  - name: Assign Image State
    kind: button
    default: ""
    effect: >-
      Asks you to confirm that the image is linear RGB and, on **Yes**, records it so in the image, as a step of its
      history named **Assign Linear Image State**.
    enabled: >-
      Shown when the image's state stops a run and is not a color calibration: linearity unknown or nonlinear, a
      channel basis unknown or not RGB, an incomplete earlier confirmation, or a narrowband filter name, which
      confirming does not change.
    tradeoff: Confirm only what you know. A stretched or color-calibrated image marked linear gets wrong gains without a warning.
    scope: The image in the view, and its header when it is saved.
    persistence: Undo the step in the image's history to take it back.
  - name: Choose magnitude limit automatically
    kind: check
    default: "On"
    effect: >-
      On, the catalog is read to G magnitude 8.0, then 8.5 and deeper by 0.5, until the circle holds twice as many
      catalog stars as the image gave sources, a target held between 64 and 4,096, or until the limit reaches
      **Magnitude cap**. Off, it is read to **Magnitude cap** at once.
    range: An image that gave no source at all aims at 512 catalog stars.
    enabled: While no run or download works. On disables **Magnitude cap**, which still caps the search.
    tradeoff: A deeper read brings more stars to match and more work, and in a crowded field more wrong neighbours.
    scope: Apply, the catalog stars that are read.
    persistence: Until the window closes. **Reset** turns it on, and Apply keeps it.
    note: >-
      It is the first control of **Catalog and WCS Preflight**, a section that is folded when the window opens: click
      its title to show its seven controls. A change decides the banner again.
  - name: Magnitude cap
    kind: spinbox
    default: "17.6"
    widget: {min: 6, max: 20, step: 0.1, decimals: 1}
    effect: Sets the faintest Gaia G magnitude read. Higher brings fainter stars into the match, lower keeps it to the brighter ones.
    range: >-
      With the automatic limit on, a cap below 8.0 stops the run with "the automatic magnitude floor is finite,
      positive and at or below the ceiling".
    enabled: With **Choose magnitude limit automatically** off, while no run or download works.
    tradeoff: Fainter stars help a sparse field, cost reading, matching and measuring time, and add faint, noisy stars.
    scope: Apply. It also sets the depth of the region that **Download Needed Region** saves.
    persistence: Until the window closes. **Reset** returns it to its default.
  - name: Match radius (arcsec)
    kind: spinbox
    default: "5.0"
    widget: {min: 0.5, max: 30, step: 0.5, decimals: 1}
    effect: >-
      Sets how far apart on the sky a detected star and a Gaia star may lie and still be paired. Larger forgives a
      coarser plate solution, smaller keeps wrong neighbours out. It also widens the circle of catalog stars read.
    range: >-
      The pairs are taken closest first, ties by the order of the detected stars and then by Gaia number, one Gaia star
      for each detected star. A field with more than 4,000,000 candidate pairs inside the radius is refused.
    enabled: While no run or download works. A change decides the banner again, since a wider circle can need other catalog chunks.
    tradeoff: Too small loses true pairs. Too large pairs stars with the wrong neighbour in a crowded field.
    scope: Apply, the match.
    persistence: Until the window closes. **Reset** returns it to its default.
  - name: Max detected stars
    kind: spinbox
    default: "4096"
    widget: {min: 64, max: 20000, step: 256}
    effect: Sets how many stars are kept from the green channel, brightest first, and how many of them go on to the match.
    enabled: While no run or download works.
    tradeoff: More helps a dense or wide field and costs detection and matching time and memory. Fewer can starve the fit.
    scope: Apply, the stars of the green channel.
    persistence: Until the window closes. **Reset** returns it to its default.
  - name: Detected star SNR
    kind: spinbox
    default: "5.0"
    widget: {min: 1, max: 100, step: 0.5, decimals: 1}
    effect: Sets the signal-to-noise ratio a detected star must reach to be matched. Higher keeps cleaner stars, lower admits faint and false ones.
    range: A star with a clipped core is left out at any value. The stars are taken by decreasing signal-to-noise ratio.
    enabled: While no run or download works.
    tradeoff: Too high can leave fewer than the 20 matches the quality rule needs, or fewer stars than **Minimum samples**.
    scope: Apply, the stars that go on to the match.
    persistence: Until the window closes. **Reset** returns it to its default.
  - name: Max residual (px)
    kind: spinbox
    default: "3.0"
    widget: {min: 0.1, max: 20, step: 0.1, decimals: 1}
    effect: >-
      Sets the root mean square distance, in pixels, that the matched stars may lie from where the plate solution puts
      their Gaia stars. The largest distance may be twice this.
    range: The match must also hold 20 stars, in at least three of the image's four quadrants.
    enabled: While no run or download works.
    tradeoff: Tight refuses a weak plate solution. Loose lets apertures land on the wrong stars.
    scope: Apply, the quality rule of the match.
    persistence: Until the window closes. **Reset** returns it to its default.
  - name: Max residual (arcsec)
    kind: spinbox
    default: "3.0"
    widget: {min: 0.1, max: 30, step: 0.1, decimals: 1}
    effect: The same limit in arcseconds. Both limits must hold.
    enabled: While no run or download works.
    tradeoff: Weigh it with the image's pixel scale and **Max residual (px)**. Loosening it lets wrong pairs through.
    scope: Apply, the quality rule of the match.
    persistence: Until the window closes. **Reset** returns it to its default.
  - name: Minimum samples
    kind: spinbox
    default: "35"
    widget: {min: 5, max: 500, step: 1}
    effect: Sets the fewest usable stars each color fit needs, before and after its outliers are left out.
    range: >-
      An accepted fit kept by fewer than 60 stars, or fewer than this value when it is higher, is reported as low
      support, with a sparse-field line in **Diagnostics**. For a stack Image Integration wrote, it is also how many of
      the hand-off's stars the run needs before it works on them alone.
    enabled: While no run or download works.
    tradeoff: Lower lets an unstable fit through. Higher needs a wider or deeper field.
    scope: Apply, the fit.
    persistence: Until the window closes. **Reset** returns it to its default.
    note: >-
      It is the first control of **Photometry and Fit**, a section that is folded when the window opens: click its
      title to show its seven controls.
  - name: FWHM aperture scale
    kind: spinbox
    default: "2.0"
    widget: {min: 0.5, max: 5, step: 0.1, decimals: 1}
    effect: >-
      Sets the radius of the aperture each star is measured in, as a multiple of its width. Higher takes in more of its
      light and more sky and neighbours, lower less.
    range: >-
      The radius is held between 2 and 10 pixels. The ring the sky is measured in starts at 3 times the star's width, 7
      pixels at least, and is 10 pixels wide, out to 64 pixels at most.
    enabled: While no run or download works.
    tradeoff: Too small misses the light of the wings. Too large adds sky and neighbours, and a ring that reaches the image's edge leaves the star out.
    scope: Apply, the measurement of each star in all three channels.
    persistence: Until the window closes. **Reset** returns it to its default.
  - name: Annulus sigma clip
    kind: spinbox
    default: "3.0"
    widget: {min: 1, max: 8, step: 0.1, decimals: 1}
    effect: >-
      Sets how far from the mean of the ring, in standard deviations, a sky sample may lie and still count, in one
      pass. The sky under the star is the median of what is kept.
    range: At least 8 sky samples must be kept, or the star is left out. The other matched stars are masked out of each ring.
    enabled: While no run or download works.
    tradeoff: Lower leaves out more and can leave too few. Higher lets faint stars in the ring raise the sky.
    scope: Apply, the sky of each star, in each channel.
    persistence: Until the window closes. **Reset** returns it to its default.
  - name: Outlier policy
    kind: combo
    default: AfterNight iterative
    options: [AfterNight iterative, Single-pass]
    effect: >-
      **AfterNight iterative** fits the line, leaves out the stars too far from it, fits again over the rest and
      repeats until nothing changes or **Fit iterations** is reached, then fits once more. **Single-pass** fits once,
      leaves out the outliers once and keeps that first line.
    enabled: While no run or download works.
    tradeoff: The iterative fit follows the stars it keeps. Single-pass is simpler and still needs **Minimum samples** stars left.
    scope: Apply, the red and blue fits.
    persistence: Until the window closes. **Reset** returns it to **AfterNight iterative**.
  - name: Fit outlier sigma
    kind: spinbox
    default: "3.0"
    widget: {min: 1, max: 8, step: 0.1, decimals: 1}
    effect: >-
      Sets how far from the line a star may lie and be kept, in multiples of the residuals' robust deviation. Lower
      leaves out more stars, higher keeps larger color differences.
    enabled: While no run or download works.
    tradeoff: Either way a fit can fail for too few stars kept or a gain too uncertain.
    scope: Apply, the red and blue fits.
    persistence: Until the window closes. **Reset** returns it to its default.
  - name: Fit iterations
    kind: spinbox
    default: "4"
    widget: {min: 1, max: 20, step: 1}
    effect: Sets how many rounds of leaving out and fitting again **AfterNight iterative** may run.
    enabled: While no run or download works. It stays enabled for **Single-pass**, which makes one round whatever it says.
    tradeoff: More rounds can settle the outliers and cost fitting time. The rounds stop early when nothing changes.
    scope: Apply, the red and blue fits.
    persistence: Until the window closes. **Reset** returns it to its default.
  - name: Apply background neutralization
    kind: check
    default: "On"
    effect: >-
      On, each channel's sky is measured over the whole image and offset so that the three skies meet at their mean
      after the gains. Off, the channels are only multiplied by their gains.
    enabled: While no run or download works. There is no rectangle to measure the sky in.
    tradeoff: A clean sky loses its cast. Nebula, a galaxy or a gradient across the frame gives wrong offsets and a new cast.
    scope: Apply, all three channels, before the mask is blended. The history records on or off.
    persistence: Until the window closes. **Reset** turns it on, and Apply keeps it.
  - name: Diagnostics
    object: spccDiagnosticsText
    kind: readout
    default: ""
    effect: >-
      Sums up the last run: `Quality score: <n>/100`, the number of matches and their RMS distance in pixels and
      arcseconds, the samples accepted, left out and usable, the gains to three decimals and the offsets to five, a
      guidance line when the fit had few stars or the catalog did not cover the field, the Gaia numbers of the stars
      the fit left out, and what the hand-off from Image Integration found.
    range: >-
      A calibrated run scores 50, plus 25 for 80 accepted samples or more (15 for 35 or more, 5 for 20 or more), 15 for
      an RMS distance of 1 pixel or less (8 within **Max residual (px)**) and 10 for no outlier (5 for up to five). A
      refused run shows no figures: `Result:` with its reason, a catalog-coverage line when the catalog did not cover
      the field, and what the hand-off found.
    enabled: Emptied when you press **Apply**, and filled when the run ends.
    tradeoff: The score is a rough guide, not a measure of color accuracy. No report file is written, so copy what you want to keep.
    persistence: Until the next run or the window closes.
  - name: Target image
    kind: combo
    default: The active image
    options: [The active image, Every other open RGB image or sequence]
    effect: Chooses the image that **Apply** replaces. The banner is decided again for it.
    enabled: Disabled while a run or a download works.
    tradeoff: Another image needs its own plate solution, catalog coverage and perhaps another response.
    persistence: >-
      Until the window closes. Another target clears the last result. When the target becomes an image that is not RGB,
      the window closes.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window. While a run or a download works it reads **Cancel**, and pressing it stops the work as it closes the window.
    enabled: Always, also during a run.
    persistence: >-
      A stopped run writes nothing, adds no history step and shows no message. Catalog chunks a download had already
      checked can stay on disk for the next run.
  - name: Reset
    kind: button
    default: ""
    effect: >-
      Returns every control of the two sections to its default, clears the response and white reference chosen in this
      window and decides the banner again.
    range: >-
      It leaves **Target image** as it is and changes no image. A choice saved for the camera stays in the settings and
      can select itself again at once.
    enabled: Disabled while a run or a download works.
  - name: Apply
    kind: button
    default: ""
    effect: >-
      Downloads the catalog chunks the field lacks, when the banner says so, then calibrates the whole target with the
      values as they are and replaces it. The values stay after Apply.
    range: A download that Apply makes saves the catalog's folder and pack in the settings, for every later run.
    enabled: >-
      When the latest banner decision allows a run, the target is RGB and nothing works. The plate solution is not part
      of the banner: an image without one is refused only when you press Apply, with "SPCC requires a plate-solved
      image." in a message titled **Spectrophotometric Color Calibration**. A calibrated image keeps it disabled.
    tradeoff: >-
      Every run first reads each file of the installed catalog to check it against its checksum, about 47 GB for the
      whole catalog, and then reads only the chunks of the field. It holds a floating-point working copy of the image
      beside the result, and the history keeps a copy of the step. A run shows a progress window with **Cancel**. Try
      new settings on a cropped copy first, since Crop keeps the plate solution.
    scope: The whole image, all three channels, with the mask blended afterwards. On a sequence, every frame.
    persistence: >-
      The history records **Spectrophotometric Color Calibration** as `Response profile: <id>, White reference: <id>,
      Background neutralization: on`, without the gains. The header cards record `ANCCAL` as `spcc`, the gains in
      `ANCCGN`, the offsets as fractions of full scale in `ANCCOF`, the profile and the white reference with their
      versions in `ANRSPID`, `ANRSPVR`, `ANWREF` and `ANWREFV`, the catalog in `ANCATF`, `ANCATM` and `ANCATP`, the
      recipe `spcc-sampled-xp-color-calibration-v3` in `ANALGOV`, the linearity and channel basis the image had in
      `ANINLIN` and `ANINBAS`, and the whole record again, with its checksum, in the `ANCJ` cards.
```

```controls
window: What the banner asks for
controls:
  - name: A catalog
    kind: readout
    default: ""
    effect: >-
      A Gaia XP catalog is installed where the settings point, complete or with missing chunks the approved download
      can fill, or none is installed and the approved catalog can be downloaded. In the last two cases the run
      downloads the catalog's metadata and the field's chunks when it starts.
    range: >-
      Otherwise the banner says that no catalog is selected, as "No installed Gaia XP sampled SPCC catalog is
      selected.", that the catalog is incomplete, or that the folder holds no valid catalog manifest, and offers **Open
      Catalogs Data...** and **Download Offline Catalog**.
    note: The catalog is the one that **Catalog root:** and **Catalog pack:** choose under **SPCC Calibration Data** on the **Catalogs & Data** page.
  - name: Approved rights
    kind: readout
    default: ""
    effect: The installed catalog's rights review allows managed download or bundling.
    range: Otherwise the banner begins "The selected SPCC catalog is installed, but its rights/provenance review status is" and offers the catalog buttons.
  - name: The field covered
    kind: readout
    default: ""
    effect: >-
      The catalog's chunks cover the circle of equation (1) about the field's center. A whole-sky catalog always does.
      Chunks the catalog lacks count as covered when the approved download can fill them, and the run downloads them
      first.
    range: >-
      Otherwise the banner says that the installed regional cache does not cover the field, with the field's center
      and radius, and offers **Download Needed Region**.
    note: An image without a plate solution is not checked here.
  - name: Response data
    kind: readout
    default: ""
    effect: Complete response data is installed where the settings point.
    range: >-
      Otherwise the banner asks you to "Download SPCC response profiles to choose the camera/sensor/filter response
      before running", with the data's name and size, and offers **Download Response Profiles**, or names what is wrong
      with the data it found.
  - name: A response profile
    kind: readout
    default: ""
    effect: >-
      A profile and a white reference are chosen: in this window, by a choice saved for the image's camera and filter,
      or by the one profile whose alias names exactly that camera and filter. A saved choice wins over an alias.
    range: >-
      Otherwise the banner ends with "No exact SPCC response profile alias matches this camera/filter metadata.", "SPCC
      response profile metadata is ambiguous. Choose one response profile before calibration." or "SPCC response profile
      is unresolved. Select an image or choose a response profile before calibration."
    tradeoff: >-
      A profile must also be reviewed and fit the image's state, or the banner reads "The selected SPCC response profile
      uses private or unreviewed response assets." or "The selected SPCC response profile is incompatible with this
      image state:" with the reason: the channel basis, an applied RAW white balance or an applied camera matrix that
      the profile does not model.
  - name: A linear image
    kind: readout
    default: ""
    effect: >-
      The image is recorded as linear, in a linear RGB or camera RGB channel basis, and its filter name does not read as
      narrowband. A meaning that states a transfer function decides over the cards, and the cards decide where it
      states none.
    range: >-
      Otherwise the banner ends with "SPCC input image state is not compatible:" and "SPCC input has unknown linearity
      state.", "SPCC input is marked nonlinear.", "SPCC input has incompatible or unknown channel basis." or "SPCC input
      has incomplete user-attested state provenance.", or says that narrowband filter metadata is not supported, and
      offers **Assign Image State**.
    tradeoff: >-
      There is no atmosphere model and no narrowband passband, so a narrowband filter name is refused. A name reads as
      narrowband as it does for Photometric Color Calibration, `Ha`, `OIII` or `narrow` among others.
  - name: Not yet color calibrated
    kind: readout
    default: ""
    effect: The image records no color calibration, from this window, Photometric Color Calibration or anything else.
    range: Otherwise the banner reads "SPCC input image is already color calibrated. Choose a linear RGB image to run SPCC again." and offers no button.
```

```controls
window: Assign Linear Image State
controls:
  - name: Question
    kind: readout
    default: ""
    effect: Reads "Mark the current RGB image as linear for SPCC. Use this only when the data has not been stretched or color calibrated."
  - name: "Yes"
    kind: button
    default: ""
    effect: >-
      Records the image as linear in a linear RGB basis, as a history step named **Assign Linear Image State**
      described `Linear RGB attested for SPCC`, and the banner is decided again.
    range: >-
      The header cards record `ANLIN` as `linear`, `ANBASIS` as `linear-rgb` and `ANASRC` as `user-attestation`, the
      file or `view:` and the view's name in `ANASFILE`, the revision of the image you looked at in `ANASHSH`,
      `AfterNight user` in `ANASBY`, the time in UTC in `ANASUTC`, and a warning text in `ANASWARN`. The lineage gains
      an entry.
    tradeoff: >-
      An image another edit changed before the step lands is refused, and a message titled **Assign Linear Image
      State** reads "The image was not marked linear: it changed after it was attested, or it was closed. Attest it
      again."
  - name: "No"
    kind: button
    default: ""
    effect: Changes nothing. It is the default answer.
```

```controls
window: Calibration progress
controls:
  - name: Stage
    kind: readout
    default: ""
    effect: >-
      Reads "Preparing execution..." while the catalog is checked and read, then the stage at work, one eighth of the
      bar each: "detecting stars in the green channel", "selecting preflight sources", "re-matching the sources against
      the catalog", "predicting the matched stars' catalog colours", "measuring the matched stars", "fitting the channel
      gains", "measuring the sky and writing the calibrated frame" and "assembling the calibration report".
    enabled: For every run, on a single image too. Its title names the process and the image.
    note: Its **Cancel** stops the run at the next stage or inside one, and the window stays open with "SPCC cancelled." in its status line.
```

```controls
window: Messages
controls:
  - name: SPCC completed
    kind: readout
    default: ""
    effect: >-
      In a message titled **Spectrophotometric Color Calibration**, reads "SPCC completed. Samples: <count>. Gains
      R=<red> G=<green> B=<blue>", the accepted samples and the gains to three decimals. You press **OK** to go on.
    enabled: After a run that the view shows, unless the window was closed during it.
  - name: Not applied
    kind: readout
    default: ""
    effect: >-
      A warning with the same title reads "SPCC completed, but the target image was closed or changed before the result
      could be applied." Nothing was written.
    enabled: After a run whose result the view could not take, unless the window was closed during it.
  - name: Refused before the run
    kind: readout
    default: ""
    effect: >-
      An error with the same title gives the reason **Apply** did not start: "No target image is available.", "SPCC
      only supports RGB images.", "SPCC requires a plate-solved image." or the banner's text.
  - name: Download messages
    kind: readout
    default: ""
    effect: >-
      A failed download gives its reason, under **SPCC Response Profiles** for response data, as "No approved managed
      SPCC response profile repository is available for download.", and under **Spectrophotometric Color Calibration**
      for catalog chunks, as "SPCC catalog chunk download failed.". "Another SPCC data download is already in
      progress." asks you to wait. A cancelled download shows nothing.
```

## How it works

1. The window decides away from itself, after every change, whether a run can start: the catalog, the response data,
   the profile the image's camera and filter resolve to, and the image's state. It shows only the latest decision.
2. **Apply** downloads the catalog chunks the field lacks when the banner says so. The run then checks every file of
   the installed catalog against its checksum, reads the plate solution from the image's
   [header cards](../../reference/glossary.md#header-card), reads the Gaia stars in the field's circle and lays the
   profile's three curves on the catalog's wavelengths.
3. A working copy holds the image as [fractions of full scale](../../reference/glossary.md#fraction-of-full-scale), in
   32-bit floating point, or 64-bit for a 64-bit or 32-bit integer image. The star finder searches the green channel
   at its defaults. The Gaia stars are moved by their proper motions from J2016.0 to the exposure start the header
   records, taken as a Julian year, and projected through the plate solution, a tangent plane with or without
   [SIP](../../reference/glossary.md#sip) terms. They are then paired one to one with the stars found. For a stack that
   [Image Integration](../stacking-integration/image-integration.md) wrote, the run reads the Gaia stars its
   registration matched and keeps those still within 3 pixels and 5 arcseconds under this plate solution and epoch.
   With at least **Minimum samples** of them it calibrates on them alone, and over the whole field when that fails or
   when there are fewer.
4. Each pair gives two points: its Gaia spectrum's red and blue against green, and the star's measured
   [flux](../../reference/glossary.md#flux) ratios in an aperture. A robust line through the points of each color,
   read at the white reference, gives that color's gain.
5. The sky is measured and offset, every valid sample is multiplied and offset, and the result is written back in the
   image's pixel type. A mask on the view then blends it with the original. The history records the step, the
   [processing state](../../reference/glossary.md#processing-state) records the image as color calibrated, and its
   [lineage](../../reference/glossary.md#lineage) gains an entry with the recipe, the profile, the white reference, the
   catalog, the gains and the offsets.

### Model

With \(w\) and \(h\) the width and height of the field on the sky, and \(\rho\) the value of **Match radius
(arcsec)**, the catalog is read within

\[ r = \tfrac{1}{2}\sqrt{w^{2} + h^{2}} + \rho \tag{1} \]

of the field's center. A Gaia star's spectrum \(F(\lambda)\) is sampled every 2 nm from 336 to 1020 nm. With
\(T_c(\lambda)\) the throughput of channel \(c\), the product of the curves of the sensor and of its filters, the
star's count in that channel is

\[ S_c = \int F(\lambda)\,\lambda\,T_c(\lambda)\,d\lambda \tag{2} \]

The factor \(\lambda\) counts photons rather than energy. Each curve is resampled onto the spectrum's wavelengths with
Akima's interpolation, or with straight lines for a curve of fewer than five points, and counts as zero outside its own
range. The integral is Akima's piecewise cubic one. The white reference gives its counts \(W_c\) the same way, without
the factor \(\lambda\) when its spectrum already counts photons.

The image measures each matched star in an aperture of radius \(\min(10, \max(2, k\,\phi))\) pixels, with \(k\) the
value of **FWHM aperture scale** and \(\phi\) the star's [FWHM](../../reference/glossary.md#fwhm), centred where the
star was found, with the first pixel's center at 0.5. With \(I_c(p)\) the samples of channel \(c\), \(a_p\) the share
of pixel \(p\) inside the circle and \(L_c\) the sky of the ring around the star, its flux is the aperture's sum, not a
fitted profile:

\[ A_c = \sum_p a_p\,I_c(p) - L_c \sum_p a_p \tag{3} \]

For red and then for blue, each star gives a point: the catalog's ratio \(x = S_c / S_G\) and the image's ratio
\(y = A_c / A_G\). A star counts when its three measured and three predicted ratios to green are finite and above
zero. The window fits the line

\[ y = \alpha_c + \beta_c\,x \tag{4} \]

by repeated medians: the slope is the median over the points of each point's median slope to the others, and the
intercept is the median of the points' intercepts. Each round keeps the points whose distance from the line is within
**Fit outlier sigma** times 1.4826 times the median distance. Above 1,024 points, an evenly spaced subset in order of
\(x\) is fitted, and the answer does not depend on the number of threads. The line's value at the white reference's own ratio \(x_w = W_c / W_G\) is what a white star
measures, so with green's raw gain at 1:

\[ g'_c = \frac{1}{\alpha_c + \beta_c\,x_w}, \qquad g_c = \frac{g'_c}{\max(g'_R,\ 1,\ g'_B)} \tag{5} \]

The gains are divided by the largest, so none is above 1 and one of them is exactly 1.

With **Apply background neutralization** on, each channel's sky level \(B_c\) is the median of its valid samples
between \(M_c - 2.8\,\sigma_c\) and \(M_c + 2.0\,\sigma_c\), with \(M_c\) the channel's median and \(\sigma_c\) 1.4826
times its [median absolute deviation](../../reference/glossary.md#median-absolute-deviation). The window is not cut at
0, so a sky near zero keeps its negative samples. With \(P\) the mean of the three sky levels and \(v\) a sample as a
fraction of full scale, the result is

\[ v' = P + g_c\,(v - B_c) \tag{6} \]

Each sky lands at \(P\), and the rest of the channel is scaled about it by its gain. With neutralization off, the
result is \(g_c\,v\).

### Boundaries

- A run fails and changes nothing when the green channel gives no star, no star reaches **Detected star SNR**, the
  match breaks the quality rule, no star pairs with a Gaia star, or the fit is refused. The fit is refused for fewer
  usable stars than **Minimum samples**, catalog ratios of a color spanning less than 0.15, too few stars kept, a gain
  whose relative uncertainty is above 5 %, or a gain that is not finite. Nothing is written before every stage has
  passed.
- A floating-point image keeps what the gains and offsets make, below 0 and above 1 included. An integer image is held
  to its range when it is written back. A floating-point image on another scale, in
  [ADU](../../reference/glossary.md#adu) for instance, is read as it is stored. Pixels the image marks as invalid are
  left unchanged.
- There is no rectangle for the sky, no saturation level, no signal-to-noise floor for the measurement and no check
  for blended stars. A star whose aperture holds an invalid sample or a sample that is not a number, or whose ring
  reaches the edge, is left out.
- The mask blends pixels only: the stars, the fit and the sky are measured on the whole image.
- On a [sequence](../../reference/glossary.md#sequence), every frame is calibrated as an image of its own, with its own
  plate solution, and held to the same checks as the frame the banner judged. A frame that fails ends the run, and the
  sequence is replaced only when every frame is done. The catalog, the response data and the hand-off are read once
  for all the frames.

### Worked example

The master in the picture above is a linear 32-bit floating-point RGB image of the Carina Nebula, 7246 by 5429 pixels,
whose three channels have their median at 0.0277. It was taken with a ZWO ASI294MM Pro through red, green and blue
filters. Its header carries a plate solution at 1.095 arcseconds per pixel, its
[pixel scale](../../reference/glossary.md#pixel-scale), and names the camera, but no filter, no exposure start and no
processing state. With the whole catalog and the response data installed, the banner asks for a response profile,
and **Assign Image State** shows under it, since nothing records the image as linear:

1. Press **Assign Image State** and answer **Yes**.
2. In **Sensor**, choose **Mono sensor: Sony IMX492**, the sensor of that camera. No profile names the ASI294MM, so
   the choice is yours, and the window saves it for this camera.
3. In the three filter boxes, choose the ZWO set, which reads **Red filter: Zwo New Lrgb Filters Optimized For CMOS
   Camera** in the first and the same name after **Green filter:** and **Blue filter:** in the others.
4. Leave **White reference** at **Average Spiral Galaxy** and the two sections at their defaults, and press **Apply**.

The field is 132.2 by 99.2 arcminutes, so equation (1) reads the Gaia stars within 82.7 arcminutes of its center. They
stay at the catalog's epoch, since the header has no exposure start. Equation (6) puts the sky of red, green and blue,
at 0.0271, 0.0273 and 0.0272, at \(P = 0.0272\) in every channel, whatever the gains. With a gain of 0.90, for
instance, a red sample of 0.100 becomes \(0.0272 + 0.90 \times (0.100 - 0.0271) = 0.0928\). The message and the status
line give the gains the fit found and the number of samples.

```figure
images:
  - image: ../../assets/images/processes/color-calibration/spectrophotometric-cc-before.webp
    alt: The whole frame before the calibration, the red and pink clouds of the nebula and a dense field of stars on a dark background.
    label: Before
  - image: ../../assets/images/processes/color-calibration/spectrophotometric-cc-after.webp
    alt: The whole frame after the calibration, the same nebula and stars with their colors balanced by the fitted gains, on a background of the same level.
    label: After
caption: >-
  The Carina Nebula master before and after Apply with the settings above. Both are the whole frame as the view shows
  it in the Autostretch display with Linked channels on, which stretches the three channels with one curve and so
  keeps their balance.
```

## Starting points

| Situation | Start | Then adjust |
| --- | --- | --- |
| A plate-solved linear master from a mono camera | Your sensor, one filter set in the three boxes, the rest at the defaults | Read **Diagnostics**, then compare under one stretch. |
| A one-shot color camera or a DSLR | Its sensor, **OSC filter** for a filter in the light path, **DSLR LPF** for the body's low-pass filter | **White reference** only to choose another balance on purpose. |
| Sparse-field guidance, or too few samples | **Magnitude cap** deeper, with **Choose magnitude limit automatically** off | **Max detected stars** up for a dense field. Leave **Minimum samples** as it is. |
| Nebula or a galaxy across the frame | **Apply background neutralization** off | Make the sky neutral afterwards over a clean area, with Background Neutralization. |

## When it goes wrong

!!! danger "Mark an image linear only when you know it is"
    **Assign Image State** records your word, not a measurement. A stretched or color-calibrated image marked linear
    is calibrated without a warning, and its colors come out wrong.

!!! danger "The response must be your camera's"
    The window cannot tell a wrong sensor or filter set from the right one. Choose your camera's sensor and the filters
    you imaged through: a wrong curve moves every gain.

!!! warning "Use one of the two catalog calibrations"
    An image that [Photometric Color Calibration](photometric-cc.md) calibrated is refused here, and so is an image
    this window calibrated. To try again, undo the earlier calibration in the image's history.

!!! warning "A floating-point result is not clipped"
    The gains and offsets can leave samples below 0 or above 1 in a floating-point image. Check the range in
    [Image Statistics](../../interface/image-statistics.md) before you stretch.

"SPCC input image state is not compatible: SPCC input has unknown linearity state."
:   **Cause.** The header states no processing state, as a file from another program or a combination of masters may.
:   **Fix.** Press **Assign Image State** and **Yes**, but only for an image you know is linear and not color
    calibrated.

"SPCC input image is already color calibrated. Choose a linear RGB image to run SPCC again."
:   **Cause.** The image records a color calibration, by Photometric Color Calibration or an earlier run.
:   **Fix.** Undo that step in the image's history, or open the linear master again.

"No exact SPCC response profile alias matches this camera/filter metadata."
:   **Cause.** No profile names the camera and filter the header gives, or the header names none.
:   **Fix.** Choose the **Sensor** and the filters yourself. The choice is saved for that camera and filter.

"No installed Gaia XP sampled SPCC catalog is selected." or "The selected SPCC catalog is incomplete."
:   **Cause.** No catalog is installed where the settings point and no approved download is offered, or the catalog
    there lacks some of its files.
:   **Fix.** Press **Open Catalogs Data...**, then choose a complete catalog, repair or reinstall this one, or press
    **Download Full Offline Catalog...**.

**Apply** stays disabled
:   **Cause.** The banner is being decided again after a change, it names something missing, or the image is the one
    this window just calibrated.
:   **Fix.** Wait for the banner, clear what it names, or undo the calibration to run again.

"SPCC requires a plate-solved image."
:   **Cause.** The header holds no plate solution the window can read. The banner does not check it.
:   **Fix.** Solve the image with Plate Solving, then press **Apply** again.

"Error executing the process" naming "the world-coordinate quality gate refused:" and a reason
:   **Cause.** The matched stars are fewer than 20, cover fewer than three quadrants, or lie farther from their Gaia
    stars than **Max residual (px)** or **Max residual (arcsec)** allow: `TooFewMatchedStars`, `PoorSkyCoverage`,
    `RmsResidualExceeded` and `MaximumResidualExceeded`. `UnsupportedDistortion` and `IncompleteProjection` name a
    plate solution the window cannot project.
:   **Fix.** Solve the image again and check the solution. Loosen the residual limits only for a solution you know is
    coarse.

"Error executing the process" naming "the green channel yielded no star to calibrate on" or "the preflight matched no source to a catalog record"
:   **Cause.** No star was found in green, none reached **Detected star SNR**, or none lay within **Match radius
    (arcsec)** of a Gaia star.
:   **Fix.** Look at the green channel, lower **Detected star SNR**, and check the plate solution and the catalog's
    coverage.

"Error executing the process" naming "the spectrophotometric fit was refused:" and a reason
:   **Cause.** `InsufficientSamples`, `InsufficientCatalogSpan`, `InsufficientInliers`, `ExcessiveGainUncertainty` or
    `NonFiniteFit`: too few stars, too narrow a range of colors, too few kept, a gain too uncertain, or a fit that is not
    a number.
:   **Fix.** Read deeper with **Magnitude cap**, keep more stars with **Max detected stars**, and check the response
    before you lower **Minimum samples**.

"Error executing the process" naming "SPCC catalog readiness rejected the installed catalog:" and a reason
:   **Cause.** At **Apply**, a catalog file did not match its checksum (`ChecksumInvalidOrStale`), or the catalog is not
    on this computer, names no identity, lacks an approved rights review or does not cover the field.
:   **Fix.** Repair or reinstall the catalog on the **Catalogs & Data** page.

"Sparse-field guidance: use a wider plate-solved field, confirm the local SPCC region covers this target, or increase the magnitude limit."
:   **Cause.** The fit was accepted with fewer than 60 stars kept in a color.
:   **Fix.** Read deeper with **Magnitude cap**, or calibrate a wider field, and compare the gains of the two runs.

The background takes a cast, or the nebula's color shifts
:   **Cause.** The sky measured over the whole image held nebula, a galaxy, a gradient or a stacking border.
:   **Fix.** Undo, remove gradients and crop the borders, or turn **Apply background neutralization** off and make the
    sky neutral afterwards.

## Related

- [Photometric Color Calibration](photometric-cc.md), the catalog calibration without response curves. Use one of the
  two on an image.
- [Plate Solving](../registration-alignment/plate-solving.md), which writes the plate solution the run needs.
- [Settings](../../interface/settings-dialog.md), for the catalog and the response data on the **Catalogs & Data**
  page.
- [Auto Gradient Removal](../background-extraction/auto-gradient-removal.md) and
  [Background Extraction](../background-extraction/background-extraction.md), to remove gradients first.
- [Histogram Transformation](../stretching/histogram-transformation.md), the stretch that comes after.
- [Process Windows](../../interface/process-windows.md), for **Target image**, masks, the progress window and the
  messages every process window shares.

## Credits

- [Gaia Data Release 3](https://gea.esac.esa.int/archive/documentation/GDR3/Miscellaneous/sec_credit_and_citation_instructions/),
  by the European Space Agency's Gaia mission, processed by the Gaia Data Processing and Analysis Consortium
  (ESA/Gaia/DPAC): the externally calibrated, sampled BP and RP spectra, positions, proper motions and G magnitudes the
  catalog is made from. Gaia data are open and free to use with credit to ESA/Gaia/DPAC, as its credit and citation
  instructions state; the [Gaia DR3 documentation](https://gea.esac.esa.int/archive/documentation/GDR3/) describes
  the products.
- Gaia Collaboration, T. Prusti et al., [The Gaia mission](https://doi.org/10.1051/0004-6361/201629272), Astronomy &
  Astrophysics 595, A1 (2016), and A. Vallenari et al.,
  [Gaia Data Release 3: Summary of the content and survey properties](https://doi.org/10.1051/0004-6361/202243940),
  Astronomy & Astrophysics 674, A1 (2023).
- Gaia Collaboration, P. Montegriffo et al.,
  [Gaia Data Release 3: The Galaxy in your preferred colours: Synthetic photometry from Gaia low-resolution spectra](https://doi.org/10.1051/0004-6361/202243709),
  Astronomy & Astrophysics 674, A33 (2023), whose photon-counting synthetic photometry equation (2) follows.
- F. De Angeli et al., [Gaia Data Release 3, on the processing of the BP/RP low-resolution spectra](https://doi.org/10.1051/0004-6361/202243680),
  Astronomy & Astrophysics 674, A2 (2023), and P. Montegriffo et al.,
  [Gaia Data Release 3: External calibration of BP/RP low-resolution spectroscopic data](https://doi.org/10.1051/0004-6361/202243880),
  Astronomy & Astrophysics 674, A3 (2023), which describe how the spectra were made and calibrated.
- The [Siril SPCC Database](https://gitlab.com/free-astro/siril-spcc-database), by the Siril SPCC Database
  contributors of the Free-Astro/Siril project, under the GNU General Public License version 3: the sensor, filter and
  low-pass filter curves and the white-reference spectra of the AfterNight SPCC Response Baseline 2026a are derived
  from it, converted to AfterNight's format and distributed under the same licence.
- H. Akima,
  [A New Method of Interpolation and Smooth Curve Fitting Based on Local Procedures](https://doi.org/10.1145/321607.321609),
  Journal of the ACM 17, 589 (1970), the interpolation and integral of equation (2), computed to the same numbers as
  the [GNU Scientific Library](https://www.gnu.org/software/gsl/) computes them.
- A. F. Siegel, [Robust regression using repeated medians](https://doi.org/10.1093/biomet/69.1.242), Biometrika 69,
  242 (1982), the line of equation (4).
- K. M. Górski et al.,
  [HEALPix: A Framework for High-Resolution Discretization and Fast Analysis of Data Distributed on the Sphere](https://doi.org/10.1086/427976),
  The Astrophysical Journal 622, 759 (2005), whose numbering of the sky's tiles the catalog's chunks follow.
