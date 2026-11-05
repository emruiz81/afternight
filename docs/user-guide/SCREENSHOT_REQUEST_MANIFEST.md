# Canonical User-Guide Media Manifest

This manifest is the sole capture brief for user-guide screenshots and
data-dependent example images. Stable Media IDs do not change when filenames or
consumer pages change. Canonical asset and consumer paths are repository-relative.

Only `Keep` rows count toward the current release capture plan. `Deferred` rows
remain recorded for later re-entry but are excluded from release counts and must
not be presented as verified release media. The historical 120-row reconciliation,
including four merged source requests and 57 not-needed requests, is preserved in
`docs/dev/USER_GUIDE_SCREENSHOT_TRIAGE.md` and checked with
`docs/user-guide/audits/user-guide-media-contract.json`.

Capture status uses `Planned`, `Ready`, `Blocked`, `Captured`, `Placed`, or
`Validated`. A missing asset stays as a `MEDIA_SLOT` on each eligible consumer
page and is never linked. A captured asset advances only with its file, reader-
focused placement, and matching provenance record. All 28 assets owned by
UG-66 through UG-68 are `Validated`.

## Start here: practical capture workflow

The tables later in this file are the canonical tracking record. The checklist
below translates every current-release row into actions a person can follow. It
does not replace the row's data/tool gate, privacy boundary, or replacement
trigger.

### What to capture now

- Capture only rows marked `Keep`. Twenty-eight retained assets are now
  validated, so 24 PNG files remain.
- Do not capture rows under **Deferred media outside the current release**. They
  intentionally lack approved data or a verified product workflow.
- `Approved data` means data the project owns or has explicit permission to
  redistribute as a screenshot. Personal data that merely happens to be on the
  capture machine is not approved data.
- `Isolated profile` means a dedicated capture/test profile or operating-system
  account. Do not erase or reset your everyday AfterNight settings just to make
  a screenshot.
- A `linear` image is the unstretched processing master. Viewer autostretch is
  fine for making it visible, but do not permanently stretch it unless the shot
  specifically asks for a nonlinear result.

### Prepare once before a capture session

1. Use the current deployed Release build from one platform for the whole set.
   Record its AfterNight version/revision and platform for provenance.
2. Put permitted sample data under a neutral capture root, for example
   `C:\AfterNightCapture\`. Rename files and projects to reader-friendly names
   such as `Light_001.fit`, `Master_Dark.fit`, and `M83_RGB.fit`. Do this on
   copies, not on original acquisitions.
3. Use a dedicated writable output folder under that neutral root. Check the
   files' FITS/EXIF metadata for observer, site, device serial, coordinates, and
   other identity before opening them.
4. Close email, chat, terminals, file browsers, and unrelated applications.
   Pause notifications. Use a clean desktop and keep the mouse pointer away from
   the control being explained.
5. Keep one theme, display scale, and application-window size throughout the
   set. Maximize AfterNight when a process has many controls. If the requested
   controls do not fit, enlarge the window or collapse unrelated sections; do
   not stitch several screenshots together.

### Take and hand off each screenshot

1. Follow the matching Media ID below. Wait for loading, previews, tables, and
   status text to finish updating.
2. Read the matching canonical row once more. Everything in **Distinctive
   required state** must be visible in the same capture unless the instruction
   explicitly identifies a paired view.
3. Inspect every visible title, table cell, path, tooltip, log line, and image
   header. If it exposes private information, replace the input with a sanitized
   copy and retake the shot. Cropping is not a substitute when the hidden item is
   part of the UI the screenshot must explain.
4. Capture the AfterNight window or the relevant AfterNight window group at its
   native resolution. Save as PNG, without JPEG conversion, decorative borders,
   annotations, or aggressive cropping. Keep enough surrounding UI to show where
   the control belongs.
5. Save to the exact canonical asset path shown below and in the table. Create
   the parent directory if it does not exist.
6. Set the row to `Captured`, compute its hash, and add a row to
   `docs/user-guide/docs/en/assets/MEDIA_PROVENANCE.md`. On PowerShell:

   ```powershell
   Get-FileHash -Algorithm SHA256 'docs/user-guide/docs/en/assets/images/interface/quick-start-step1.png'
   ```

   Replace the example path with the canonical path for the shot you captured.

7. After the image is added to every consumer page and checked in the built
   guide, advance it to `Placed` and then `Validated`. If a prerequisite cannot
   be met safely, use `Blocked` and write down the missing prerequisite instead
   of taking an approximate or private screenshot.

### Status words in plain language

| Status | Meaning |
| --- | --- |
| `Planned` | The shot is requested, but its build, data, tool, or UI state has not been prepared. |
| `Ready` | All prerequisites are available and the shot can be taken now. |
| `Blocked` | A named prerequisite is missing; do not substitute weaker evidence. |
| `Captured` | The PNG exists at the canonical path and has a provenance row. |
| `Placed` | Every intended guide page links and explains the PNG. |
| `Validated` | Privacy, required state, rendering, links, provenance, and the strict guide build all pass. |

## Plain-language shot list

Work down this list by phase. Shots that reuse the same prepared data are placed
next to each other so one application session can produce several assets.

### UG-66 — Onboarding and workspace orientation

#### UG-MEDIA-001 — `hero-screenshot.png`

No action is needed now. The fresh-profile Dashboard image is already placed.
Recapture it only when the Dashboard elements named in its replacement trigger
change.

Save as: `docs/user-guide/docs/en/assets/images/interface/hero-screenshot.png`

#### UG-MEDIA-002 — `quick-start-step1.png`

1. Open **Process > Preprocess > Image Integration Lite**.
2. Choose the neutral output folder and import one approved single-target set
   containing the lights and calibration frames needed for a valid run.
3. Wait for classification and matching to finish. Correct any wrongly assigned
   rows until the readiness banner says the workflow can run.
4. Arrange the window so the dataset summary, representative frame rows, and
   ready-to-run banner are readable together. Capture before starting the run.

Save as: `docs/user-guide/docs/en/assets/images/interface/quick-start-step1.png`

#### UG-MEDIA-003 — `quick-start-result.png`

1. Continue from UG-MEDIA-002 with **Open results when complete** enabled.
2. Run Lite and wait for a successful completion. Do not capture a running or
   partially successful state.
3. Keep the newly opened linear master visible and arrange the Lite completion
   or report context beside it. The image may use viewer autostretch for display,
   but must still be the linear result.
4. Make sure the project, output, and report names are neutral before capture.

Save as: `docs/user-guide/docs/en/assets/images/interface/quick-start-result.png`

#### UG-MEDIA-004 — `overview.png`

1. Open one approved RGB image with **File > Open...**.
2. Open **View > Session Console**.
3. Resize the image view and console so the main workspace, image-view toolbar,
   active image, dock boundary, and console are all easy to recognize.
4. Paths, image names, log messages, and public repository details from the
   designated demo PC are allowed. Do not expose credentials, access tokens,
   private repositories, unrelated personal data, or another machine's details.

Save as: `docs/user-guide/docs/en/assets/images/interface/overview.png`

#### UG-MEDIA-006 — `dashboard-recents.png`

1. In an isolated profile, open at least three approved or synthetic images with
   neutral names so safe thumbnails are added to Recents.
2. Close all image views and the Session Console to return to the Dashboard.
3. Clear the search field and choose a normal sort that shows a featured item,
   at least three thumbnails, the item count, quick actions, and the Dashboard
   hint. The first-run onboarding panel must not be visible.
4. Check thumbnail labels, paths, and timestamps for private information.

Save as: `docs/user-guide/docs/en/assets/images/interface/dashboard-recents.png`

#### UG-MEDIA-008 — `docking-panels.png`

1. Open three safe images. Group two image views into one tabbed dock and leave
   the third in a separate split so both layout ideas are obvious.
2. Select one tab without maximizing it.
3. Keep the dock-layout toolbar visible, including **Lock**, **Group**,
   **Split**, and **Fit**. Widen the main window if any action falls into an
   overflow menu.
4. Capture the whole AfterNight workspace; do not include the desktop behind a
   floating dock.

Save as: `docs/user-guide/docs/en/assets/images/interface/docking-panels.png`

#### UG-MEDIA-009 — `toolbar-and-menus.png`

1. Open and focus one safe image so image-dependent process actions have a
   target.
2. Click **Process** in the main menu bar and leave the menu open.
3. Position the pointer outside the menu text. Check that the main toolbar and
   enough of the active image remain visible to establish context.
4. Capture while the Process categories are readable.

Save as: `docs/user-guide/docs/en/assets/images/interface/toolbar-and-menus.png`

### UG-67 — Viewer and application components

#### UG-MEDIA-007 — `image-viewer.png`

1. Open a representative approved still and fit it in the view.
2. Widen the view until its toolbar clearly shows **View Channel**, **Display
   Mode**, and **More**, along with the normal viewer controls.
3. Select a useful display mode, but leave the image recognizable and avoid a
   clipped or all-black view. Open **More** only if doing so does not cover the
   channel and display-mode controls the shot must explain.
4. Capture the complete image view, including its approved title and toolbar.

Save as: `docs/user-guide/docs/en/assets/images/interface/image-viewer.png`

#### UG-MEDIA-010 — `process-windows.png`

1. Focus a disposable safe image and open a representative target-bound process,
   such as **Process > Noise Reduction > Wavelet Denoise Filter**.
2. Move or resize the modeless process so it sits beside its target instead of
   covering it.
3. Turn on Preview if it is quick and stable. Keep target identity, several
   parameter controls, preview controls, and **Apply**, **Reset**, and **Close**
   visible.
4. This shot explains the common shell, not output quality; defaults are fine.

Save as: `docs/user-guide/docs/en/assets/images/interface/process-windows.png`

#### UG-MEDIA-011 — `log-viewer.png`

1. Enable file logging, then use the approved documentation capture session to
   create messages at several severities. Maintainer-owned project paths,
   machine-local directories, image names, and public repository endpoints may
   remain visible when they are part of that approved session. Do not expose
   credentials, access tokens, private repositories, or unrelated personal data.
2. Open **View > Session Console** and choose a visible-level filter that leaves
   more than one severity on screen.
3. Make the file-log indicator, **Search**, **ANSI Colors**, and **Settings**
   controls visible. Use a harmless search term if it helps demonstrate search
   state without hiding all messages.
4. If the current build has no approved way to create representative messages,
   mark this item `Blocked` instead of using unrelated logs.

Save as: `docs/user-guide/docs/en/assets/images/interface/log-viewer.png`

#### UG-MEDIA-012 — `image-history.png`

1. Open a disposable image, make a duplicate to create lineage, and apply
   several quick, deterministic operations to the duplicate so it has retained
   history steps.
2. Open **Edit > Image History** on the duplicate.
3. Select an older row so the historical preview differs from the current row.
   Keep both row states, the preview, histogram, navigation, lineage, **Save**,
   **Refresh**, and **Close** visible.
4. Use approved image names. A machine-local path fragment from the designated
   capture PC is acceptable; do not open a save dialog for the screenshot.

Save as: `docs/user-guide/docs/en/assets/images/interface/image-history.png`

#### UG-MEDIA-014 — `image-histogram.png`

1. Open an RGB image with a meaningful tonal range.
2. Choose **Image > Information > Histogram**.
3. Show all RGB channels and choose a normal histogram resolution. Resize the
   dialog so the plot toolbar, channel controls, clipping information, and
   resolution selector are legible.
4. Avoid a flat synthetic image whose histogram is only one spike.

Save as: `docs/user-guide/docs/en/assets/images/interface/image-histogram.png`

#### UG-MEDIA-016 — `fits-header-viewer.png`

1. Open an approved solved FITS file that contains a celestial WCS.
2. Choose **Image > Information > FITS Header**.
3. Resize the table so **Key**, **Value**, and **Comment** are readable. Scroll
   to representative WCS records; include SIP order/coefficient rows when the
   selected file contains them.
4. Recheck visible header values for credentials, device serials, or unrelated
   personal data. Approved target names and celestial coordinates may remain.

Save as: `docs/user-guide/docs/en/assets/images/interface/fits-header-viewer.png`

#### UG-MEDIA-018 — `astrometry-info.png`

1. Reuse the approved solved FITS from UG-MEDIA-016.
2. Choose **Image > Information > Astrometry Info**.
3. Wait for parsing to finish. Resize the window so the available/success banner
   and the complete solution summary are visible without scrolling.
4. Capture the complete summary. A **SIP distortion: None** value is valid when
   the selected solved file has no accepted SIP coefficients; approved target
   identity and celestial coordinates may remain visible.

Save as: `docs/user-guide/docs/en/assets/images/interface/astrometry-info.png`

#### UG-MEDIA-019 — `settings-dialog.png`

1. Use an isolated profile and open **Edit > Settings...**.
2. Select **General** and clear the settings search field.
3. Resize the dialog so every registered navigation page, the search box, page
   heading, **Reset Page**, **Apply**, **Cancel**, and **OK** are visible.
4. The designated maintainer capture PC's local folders and username-bearing
   path segments may remain visible. Do not expose credentials, tokens, private
   repositories, or unrelated personal data.

Save as: `docs/user-guide/docs/en/assets/images/interface/settings-dialog.png`

#### UG-MEDIA-024 — `masks.png`

1. Open a safe target and a same-size grayscale mask with neutral view names.
2. Activate the target and choose **Image > Mask > Select Mask**. Assign the
   grayscale view, then reopen **Select Mask** so the modeless Mask Selector can
   remain beside the already masked target.
3. Open the target's **Image > Mask** menu and choose **Overlay Red** as the
   rendering mode. Make the red overlay visible without hiding all target
   detail.
4. Capture the target, open Mask menu, selector, and recognizable overlay in one
   arranged AfterNight window group.

Save as: `docs/user-guide/docs/en/assets/images/interface/masks.png`

#### UG-MEDIA-025 — `astrometry-overlays.png`

1. Open a sanitized solved still with WCS and SIP data. Make sure the required
   local catalog data is already installed; do not download during capture.
2. Use the image toolbar's **Show Astrometry** and **Astrometry Settings**.
3. Enable the equatorial grid, deep-sky labels, bright-star labels, detected
   stars, and SIP distortion vectors when the file supports them.
4. Adjust label magnitude/detail and zoom until the layers are all recognizable
   without covering one another. Close the settings popup if it obscures the
   overlays, but keep the astrometry toolbar controls visible.

Save as: `docs/user-guide/docs/en/assets/images/interface/astrometry-overlays.png`

#### UG-MEDIA-026 — `sequences.png`

1. Open an approved SER/video sequence, or create a synthetic sequence with
   **File > New Sequence...** from at least two compatible frames.
2. Pause on a representative frame away from the first frame so the position
   readout is meaningful.
3. Keep playback, previous/next navigation, frame position, and the sequence
   actions in **Image > Sequence** visible. If the menu covers the player,
   arrange the window larger before opening it.
4. Approved sequence/view names and capture-machine paths may remain visible;
   remove credentials, device serials, and unrelated personal data.

Save as: `docs/user-guide/docs/en/assets/images/interface/sequences.png`

#### UG-MEDIA-027 — `saving-and-exporting.png`

1. Open a safe CFA sequence and choose **Image > Sequence > Export...**.
2. Select a deployed video format and codec. Enter an approved output name and
   folder, then set a visible FPS value. A maintainer capture-machine directory
   may remain visible.
3. Enable the CFA/debayer, autostretch, and resize options so their dependent
   controls are shown. Choose dimensions and a fill mode that are visibly valid.
4. Capture the configured dialog before starting the export; no output file is
   required for this state screenshot.

Save as: `docs/user-guide/docs/en/assets/images/interface/saving-and-exporting.png`

#### UG-MEDIA-028 — `catalogs-data-settings.png`

1. In an isolated profile, configure approved local Gaia and SPCC packs. The
   designated maintainer capture PC's catalog root may remain visible.
2. Open **Edit > Settings... > Catalogs & Data**. Let pack discovery and
   integrity checks finish; do not start a download.
3. Select a representative pack so its readiness/status and management actions
   are visible together.
4. Cache roots and username-bearing path segments from the designated capture
   PC are acceptable. Do not expose credentials, account identifiers, private
   endpoints, or unrelated personal data.

Save as: `docs/user-guide/docs/en/assets/images/interface/catalogs-data-settings.png`

#### UG-MEDIA-029 — `external-tools-settings.png`

1. Install supported StarNet2 and DeepSNR versions in the designated capture
   environment; normal machine-local installation paths are acceptable.
2. Open **Edit > Settings... > External Tools** and configure both tools.
3. Wait for version/model checks to show supported ready states for both tools.
4. Capture the complete ready-state page. Executable, model, weights, and
   username-bearing path segments from the designated capture PC may remain;
   license secrets, credentials, tokens, and unrelated environment data may not.

Save as: `docs/user-guide/docs/en/assets/images/interface/external-tools-settings.png`

### UG-68 — Calibration, registration, solving, stacking, and integration

#### UG-MEDIA-031 — `image-calibration.png`

1. Open **Process > Preprocess > Image Calibration** and add an approved set of
   light frames.
2. Enable and assign compatible master bias, dark, and flat files from the
   approved capture root.
3. Expand **Advanced Dark Settings** and choose the neutral writable output
   folder. Collapse unrelated preview areas if more vertical room is needed.
4. Capture when the lights, three master relationships, expanded dark settings,
   and **Output & Execution** area are visible together and Calibrate is in a
   valid state. Do not run solely for this screenshot.

Save as: `docs/user-guide/docs/en/assets/images/processes/calibration/image-calibration.png`

#### UG-MEDIA-037 — `image-registration.png`

1. Open **Process > Preprocess > Image Registration** and add at least two
   compatible calibrated lights from the same field.
2. Choose an approved writable output, select a reference, and configure normal
   registration/alignment settings.
3. Run **Register** and wait for all rows to finish so **Stars**, **FWHM**,
   **SNR**, **Eccentricity**, and **RMS** contain real values. Keep the chosen
   reference visibly marked.
4. Resize columns rather than truncating metric names. Capture the populated
   table together with the main registration/alignment and output controls.

Save as: `docs/user-guide/docs/en/assets/images/processes/registration-alignment/image-registration.png`

#### UG-MEDIA-038 — `plate-solving.png`

1. Open the approved M83 test field or another permitted reproducible field and
   choose **Image > Information > Plate Solve**.
2. Select the prepared Internal Gaia configuration and expand **Acquisition
   Hints**. Use the recorded hints that are accurate enough for the approved
   field.
3. Run **Solve** and wait for a successful status. Do not capture a dialog that
   merely has a configured backend.
4. Resize the dialog so the solver status, expanded hints, and every **Solved
   Field** value are visible. Check coordinates and target identity against the
   data permission before capture.

Save as: `docs/user-guide/docs/en/assets/images/processes/registration-alignment/plate-solving.png`

#### UG-MEDIA-039 — `star-profiler.png`

1. Prepare two byte-identical copies of one approved linear star field with
   distinct filenames (for example, `Profiler_v1.fit` and
   `Profiler_v2.fit`). Open both as separate image targets and set the same
   zoom and field of view before launching either profiler. This is required:
   each profiler holds exclusive access to its target.
2. Open **Process > Star Processing > Star Profiler v1** on the first target
   and **Star Profiler v2** on the second. Run detection in both if their tables
   do not populate automatically.
3. Arrange the two windows side by side in one native-resolution capture. Keep
   both detected-star tables and the controls that differ between v1 and v2,
   including PSF-related controls, visible. Do not stitch separate captures.
4. Confirm that each window is bound to its intended duplicate and that the
   underlying pixels, zoom, and displayed field match; the distinct filenames
   are only an exclusive-access workaround, not a comparison of two datasets.

Save as: `docs/user-guide/docs/en/assets/images/processes/registration-alignment/star-profiler.png`

#### UG-MEDIA-040 — `image-integration.png`

1. Open **Process > Preprocess > Image Integration**.
2. Select an approved writable output and add approved lights until at least one
   light group is populated. Add other frame types only if they help the group
   state without crowding the shot.
3. Select **Frame Sources > Lights**. Resize the window so all source tabs and
   workflow tabs remain visible along with the light group and output selection.
4. Capture this organization/readiness state before execution; the Summary
   result belongs in UG-MEDIA-044.

Save as: `docs/user-guide/docs/en/assets/images/processes/stacking-integration/image-integration.png`

#### UG-MEDIA-044 — `image-integration-summary.png`

1. Continue from an approved Image Integration project and complete a successful
   reproducible workflow run.
2. Open **Summary** after completion and wait for all status and history data to
   load.
3. Arrange the page so the completed status, **Integration Summary**, **Run
   History**, resource estimate, and report action are readable together.
4. Capture-machine paths, filenames, project/run names, and hardware text are
   approved for this maintained set. Do not expose credentials, tokens, secrets,
   or unrelated account data. Keep the report as retained evidence, but do not
   open it over the Summary page.

Save as: `docs/user-guide/docs/en/assets/images/processes/stacking-integration/image-integration-summary.png`

#### UG-MEDIA-047 — `image-list-analyzer.png`

1. Open **Process > Utilities > Image List Analyzer** and add an approved list
   of representative stills.
2. Run the analysis and any normal optimization step needed to populate its
   status. Wait for every row to finish.
3. Select a useful row. Resize the table so all nine columns are identifiable
   and keep the selected row's metadata, histogram, and optimization status
   visible.
4. Capture-machine file and output paths are approved for this maintained set.
   Do not expose credentials, tokens, secrets, or unrelated account data.

Save as: `docs/user-guide/docs/en/assets/images/processes/stacking-integration/image-list-analyzer.png`

### UG-69 — Color calibration, background extraction, and stretching

#### UG-MEDIA-050 — `spectrophotometric-cc.png`

1. Prepare a solved, linear broadband RGB target and make the approved Gaia/SPCC
   data ready before opening the process.
2. Focus the target and open **Process > Color Calibration > Spectrophotometric
   Color Calibration**.
3. Select the target, then expand **Catalog Preflight** and **WCS Preflight**.
   Wait for readiness checks to complete.
4. Capture when the overall readiness panel, both expanded preflights,
   **Photometry**, **Fit**, and target selector are visible. Do not expose private
   coordinates, catalog roots, paths, or network/account details.

Save as: `docs/user-guide/docs/en/assets/images/processes/color-calibration/spectrophotometric-cc.png`

#### UG-MEDIA-054 — `histogram-transformation.png`

1. Open a meaningful linear RGB target and choose **Process > Intensity
   Transformations > Histogram Transformation**.
2. Turn on Preview and keep RGB linked unless a deliberate channel state is
   required. Make a small, non-destructive-looking midtones adjustment so the
   handles and clipping readouts are meaningful.
3. Resize the window so both histograms, graph toolbar, channel selector,
   auto-stretch actions, tonal controls, handles, and clipping readouts are
   visible.
4. Capture before Apply; this is an interaction screenshot, not a claimed final
   stretch.

Save as: `docs/user-guide/docs/en/assets/images/processes/stretching/histogram-transformation.png`

#### UG-MEDIA-055 — `curves-transformation.png`

1. Open an approved nonlinear RGB target and choose **Process > Intensity
   Transformations > Curves Transformation**.
2. Add a few well-spaced control points that form an easy-to-read gentle curve.
   Select an interpolation mode and turn on Preview.
3. Keep the curve editor, points, clipping readout, graph toolbar, interpolation
   control, and enough of the target visible to show context.
4. Capture before Apply and avoid a curve that visibly crushes shadows or clips
   highlights.

Save as: `docs/user-guide/docs/en/assets/images/processes/stretching/curves-transformation.png`

#### UG-MEDIA-056 — `gh-transformation.png`

1. Open a linear RGB target and choose **Process > Intensity Transformations >
   Generalized Hyperbolic Transformation**.
2. Select one transformation type and a modest non-default strength so its
   mode-dependent controls are active. Turn on Preview.
3. Resize the process so the histograms, transformation selector, active-mode
   controls, and all three symmetry-point pickers are visible together.
4. Capture before Apply; verify that none of the pickers or controls is hidden
   below the fold.

Save as: `docs/user-guide/docs/en/assets/images/processes/stretching/gh-transformation.png`

#### UG-MEDIA-077 — `background-extraction.png`

1. Open an approved linear image with a visible but representative gradient and
   choose **Process > Background Modelization > Background Extraction**.
2. Generate the sample grid, then remove samples that lie on obvious target
   structure. Select RBF or a low-order Polynomial model.
3. Turn on the full-image Preview and choose clear correction/output settings.
   Wait for the fitted model to finish.
4. Capture the populated sample grid on the target together with the model,
   correction, output, and preview controls.

Save as: `docs/user-guide/docs/en/assets/images/processes/background-extraction/background-extraction.png`

#### UG-MEDIA-078 — `auto-gradient-removal.png`

1. Open an approved linear image containing both a gradient and meaningful
   structure, then choose **Process > Background Modelization > Auto Gradient
   Removal**.
2. Configure both the background model and structure-protection sections with a
   reproducible state.
3. Select either the **Gradient model** or **Protection mask** diagnostic and
   wait for the full-image Preview to finish.
4. Capture the diagnostic plus both control groups. Prefer the diagnostic that
   most clearly demonstrates the distinction without hiding the target.

Save as: `docs/user-guide/docs/en/assets/images/processes/background-extraction/auto-gradient-removal.png`

### UG-70 — Noise reduction and multiscale

#### UG-MEDIA-061 — `wavelet-denoise.png`

1. Open an approved noisy RGB crop and choose **Process > Noise Reduction >
   Wavelet Denoise Filter**.
2. Turn on Preview and choose a stable representative region. Use moderate
   settings that preserve recognizable detail.
3. Keep **Threshold**, **Scales**, **Iterations**, **Amount**, **Channel**, and
   the preview visible together. Widen or heighten the process rather than
   cropping out controls.
4. Wait for the preview to finish before capture; do not use a transient progress
   frame.

Save as: `docs/user-guide/docs/en/assets/images/processes/noise-reduction/wavelet-denoise.png`

#### UG-MEDIA-070 — `deepsnr.png`

1. Configure a supported DeepSNR CLI and model under neutral paths, then open an
   eligible safe target.
2. Choose **Process > Noise Reduction > DeepSNR** and wait for the supported
   readiness message.
3. Select the custom stride mode so the custom value is visible. Keep the
   pre-stretch option, all stride choices, weights, target selector,
   **Configure**, readiness message, and execution action on screen.
4. This shot is configuration evidence only. Do not run inference just to take
   it, and do not expose executable/model/weights or user paths.

Save as: `docs/user-guide/docs/en/assets/images/processes/noise-reduction/deepsnr.png`

#### UG-MEDIA-103 — `multiscale-linear.png`

1. Open a safe RGB still and choose **Process > Multiscale > Multiscale Linear
   Transform**.
2. Select the MLT/Dyadic method and configure six detail layers.
3. Expand the complete layer tree so **Residual** is visible, then select one
   detail layer to populate its inspector. Turn on a stable preview if practical.
4. Capture the method, six-layer tree, Residual row, selected-layer highlight,
   and inspector together.

Save as: `docs/user-guide/docs/en/assets/images/processes/multiscale/multiscale-linear.png`

### UG-71 — Star operations and masking

#### UG-MEDIA-079 — `star-mask.png`

1. Open an approved linear star field and choose **Process > Mask Generation >
   Star Mask**.
2. Select one available detection backend and use settings that find a useful
   range of stars. Expand the backend-specific detection section.
3. Enable **Shaped Areas**, turn on Preview, and wait for a representative mask
   preview.
4. Capture the target/preview, backend identity, detection controls, and complete
   Shaped Areas controls together.

Save as: `docs/user-guide/docs/en/assets/images/processes/star-operations/star-mask.png`

#### UG-MEDIA-080 — `starnet.png`

1. Configure a supported StarNet2 installation and model under neutral paths,
   then open a safe calibrated target.
2. Choose **Process > Star Processing > Starnet++** and wait for the ready state.
3. Select a clear output configuration. Keep shared tool settings, output
   controls, target, readiness message, **Configure**, and execution actions
   visible.
4. Do not execute solely for this shot. Verify that no executable/model, target,
   username, or environment path is shown.

Save as: `docs/user-guide/docs/en/assets/images/processes/star-operations/starnet.png`

#### UG-MEDIA-084 — `profiled-starmask-recomb.png`

1. Open compatible safe starless and stars images and prepare a permitted
   `.anspv1` or `.anspv2` profile under the neutral capture root.
2. Choose **Process > Star Processing > Profiled Starmask Recombination**,
   assign both inputs, and load the matching profile.
3. Enable **Current Stars** and **Binary Areas**, then turn on Preview and its
   profile overlays. Wait for all profile data to load.
4. Capture the two selectors, safe profile state, both named controls, and
   visible overlays without exposing the profile path.

Save as: `docs/user-guide/docs/en/assets/images/processes/star-operations/profiled-starmask-recomb.png`

#### UG-MEDIA-100 — `mask-drawing.png`

1. Open a synthetic or approved grayscale target and choose **Process > Mask
   Generation > Mask Drawing**.
2. Select one drawing tool, choose representative settings, and add a few clean
   strokes so undo and redo have meaningful state.
3. Keep the drawing canvas, complete settings for the selected tool, **Mask
   View** modes, undo/redo, import, and export actions visible.
4. Do not open an operating-system file dialog. Use a neutral target name and
   keep desktop content outside the capture.

Save as: `docs/user-guide/docs/en/assets/images/processes/masking/mask-drawing.png`

#### UG-MEDIA-102 — `morphological-transformation.png`

1. Open or duplicate a safe grayscale mask and choose **Process > Morphology >
   Morphological Transformation**.
2. Select **Erosion**, set a visible iteration count and moderate amount, and
   choose a filled 3-by-3 structuring element.
3. Turn on Preview. Keep the operation, iterations, amount, complete 3-by-3 grid,
   and structuring-element actions visible together.
4. Capture before Apply so the source mask remains a disposable setup.

Save as: `docs/user-guide/docs/en/assets/images/processes/masking/morphological-transformation.png`

### UG-72 — Composition, geometry, Pixel Math, and analysis

#### UG-MEDIA-086 — `channel-composition.png`

1. Open three compatible monochrome sources with neutral names.
2. Choose **Process > Color Spaces > Channel Composition**, select RGB, and
   assign one source to each dynamically labelled channel selector.
3. Make the **Linear Match** choice visible and ensure **OK** is enabled.
4. Capture all three selectors, selected color space, Linear Match, **OK**, and
   **Close**. Do not execute solely for this screenshot.

Save as: `docs/user-guide/docs/en/assets/images/processes/color-composition/channel-composition.png`

#### UG-MEDIA-087 — `lrgb-combination.png`

1. Open compatible, same-size RGB and grayscale luminance sources and focus the
   RGB target.
2. Choose **Process > Color Spaces > LRGB Combination** and assign the luminance
   source and RGB target.
3. Turn on Preview and choose moderate, readable weight and MTF values.
4. Capture the histogram, source selectors, weight/MTF controls, and preview
   together. Check both view names and metadata for private information.

Save as: `docs/user-guide/docs/en/assets/images/processes/color-composition/lrgb-combination.png`

#### UG-MEDIA-092 — `crop.png`

1. Open a sanitized solved image and choose **Process > Geometry > Crop**.
2. Drag a clearly non-full-size crop rectangle that leaves useful image context
   around every edge.
3. Make sure the rectangle agrees with the displayed position and size fields.
   Keep the before-size and new-size readouts visible.
4. Capture before Apply and remove or approve any sensitive coordinates in the
   solved-image metadata.

Save as: `docs/user-guide/docs/en/assets/images/processes/geometry/crop.png`

#### UG-MEDIA-096 — `pixel-math.png`

1. Open a safe RGB target and choose **Process > Pixel Math > Pixel Math**.
2. Enter a short harmless RGB/K expression that contains no paths, credentials,
   or private identifiers.
3. Expand or arrange the function, operator, and symbol browsers. Enable creation
   of a new image and use a neutral output identifier.
4. Capture the expression editor, browsers, import/export actions, and enabled
   new-image settings together; do not open a file dialog.

Save as: `docs/user-guide/docs/en/assets/images/processes/pixel-math/pixel-math.png`

#### UG-MEDIA-097 — `image-compare.png`

1. Open two approved same-size images and focus the first.
2. Choose **Process > Utilities > Image Compare** and assign the second image.
3. Select raw-data histograms and a horizontal split, then center the divider.
4. Capture both labelled images, both selectors, raw histograms, centered
   divider, and preview tools together.

Save as: `docs/user-guide/docs/en/assets/images/processes/analysis/image-compare.png`

### UG-73 — Extensions and verified broadband workflow media

#### UG-MEDIA-112 — `extensions-manager.png`

1. Prepare an approved official/safe repository cache. Open **Extensions >
   Manage Extensions... > Browse**; do not install or update anything during
   capture.
2. Use a neutral search/filter combination that leaves at least one signed,
   compatible package. Select a visible sort order and refresh before arranging
   the shot.
3. Expand the safe package and keep search, filter, sort, refresh, **More
   Actions**, signature/compatibility information, and the unsigned-package
   policy visible.
4. Remove private repository URLs, credentials, identities, paths, and any
   unapproved package from the visible results.

Save as: `docs/user-guide/docs/en/assets/images/interface/extensions-manager.png`

#### UG-MEDIA-113 — `extensions-status.png`

1. Install one approved non-private extension into a neutral capture environment
   and wait until its status is **Ready**.
2. Open **Extensions > Manage Extensions... > Installed** and expand that card.
3. Keep source, host mode, availability, sanitized path fields, validation,
   lifecycle, and maintenance actions visible together.
4. If an absolute path must be visible, install the extension under the neutral
   capture root. Do not blur a private path while claiming the path field as UI
   evidence.

Save as: `docs/user-guide/docs/en/assets/images/interface/extensions-status.png`

#### UG-MEDIA-114 — `broadband-step-calibration.png`

1. Open the approved UG-60 OSC/RGB/LRGB Image Integration project, or an
   equivalently approved reproducible project.
2. Select the **Calibration** workflow tab after all broadband source frames and
   masters have loaded.
3. Confirm that matched masters, coverage, loaded frame groups, and the neutral
   output state are visible and internally consistent.
4. Capture this verified checkpoint before changing stages. Sanitize every
   frame, master, project, output, observer/site, and device label.

Save as: `docs/user-guide/docs/en/assets/images/workflows/broadband-step-calibration.png`

#### UG-MEDIA-115 — `broadband-step-stretch.png`

1. Open a duplicate of the approved linear broadband master and retain the
   original as before-state evidence.
2. Open either **Histogram Transformation** or **Generalized Hyperbolic
   Transformation**, reproduce the approved first-stretch settings, and Apply
   once.
3. Leave the process open beside the newly nonlinear result. Keep enough viewer
   context to show that this is the first permanent stretch, not viewer-only
   autostretch.
4. Capture immediately after the successful Apply, with target/path/metadata
   sanitized and the retained settings recorded for provenance.

Save as: `docs/user-guide/docs/en/assets/images/workflows/broadband-step-stretch.png`

#### UG-MEDIA-116 — `broadband-final-result.png`

1. Open the approved polished broadband derivative and verify that publication
   permission covers the displayed pixels and metadata.
2. Open **Edit > Image History** and select the current/final row so the finishing
   lineage is visible beside the result.
3. Show AfterNight's final save/export context using the in-app action or safe
   export panel required by the verified workflow. If an operating-system save
   dialog is used, point it at the neutral output root and ensure no unrelated
   files or locations are visible.
4. Arrange the viewer, history, and export context in one readable capture. This
   is a bounded example result, not a general quality promise.

Save as: `docs/user-guide/docs/en/assets/images/workflows/broadband-final-result.png`

### Deferred shots — do not capture yet

Do not capture UG-MEDIA-088, UG-MEDIA-089, UG-MEDIA-090, UG-MEDIA-117,
UG-MEDIA-118, UG-MEDIA-119, or UG-MEDIA-120. Their rows below name the missing
workflow/data/permission gate. When a gate is genuinely satisfied, move the row
back into current-release scope and add a plain-language checklist here before
capturing it.

## UG-66 — Onboarding and workspace orientation

| Media ID | Phase | Canonical asset | Consumer pages | Role | Disposition | Reader question | Distinctive required state | Evidence/data/tool/platform gate | Privacy boundary | Capture status | Replacement trigger |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| UG-MEDIA-001 | UG-66 | `docs/user-guide/docs/en/assets/images/interface/hero-screenshot.png` | `docs/user-guide/docs/en/index.md`<br>`docs/user-guide/docs/en/getting-started/first-launch.md`<br>`docs/user-guide/docs/en/interface/dashboard.md` | Orientation | Keep | What does a fresh AfterNight profile show before any file is opened? | Fresh-profile Dashboard with onboarding, start cards, quick actions, drop zone, and keyboard hints; no recent files. | Existing maintainer capture with recorded hash; no astronomical data or application launch is required by this refactor. | No recent filenames, user identity, private paths, or account data. | Validated | Dashboard onboarding, start-card, quick-action, drop-zone, or shell layout changes. |
| UG-MEDIA-002 | UG-66 | `docs/user-guide/docs/en/assets/images/interface/quick-start-step1.png` | `docs/user-guide/docs/en/getting-started/quick-start.md`<br>`docs/user-guide/docs/en/processes/stacking-integration/image-integration-lite.md` | State | Keep | How can a beginner recognize that Image Integration Lite is ready to run? | Representative single-target set imported with dataset summary, frames table, and readiness banner permitting execution. | Approved single-target calibration/light set; stable current Lite UI; current release capture build. | Sanitized filenames and output paths; no observer, site, device serial, or user identity. | Validated | Lite readiness logic, summary/table layout, or launch workflow changes. |
| UG-MEDIA-003 | UG-66 | `docs/user-guide/docs/en/assets/images/interface/quick-start-result.png` | `docs/user-guide/docs/en/getting-started/quick-start.md` | Verified result | Keep | What does a successful Lite run produce before nonlinear stretching? | Successful completion with an opened linear master displayed in the image viewer. | Reproducible successful Lite execution with approved data and retained settings/report evidence. | Sanitized project, file, and output names; exclude acquisition identity and absolute paths. | Validated | Lite completion, output-opening, report, or linear-state presentation changes. |
| UG-MEDIA-004 | UG-66 | `docs/user-guide/docs/en/assets/images/interface/overview.png` | `docs/user-guide/docs/en/interface/overview.md` | Orientation | Keep | Where are the main workspace, viewer, toolbar, and session panels? | One approved demo image open with the Session Console and the main viewer toolbar groups visible. | Stable current workspace on the designated demo PC; approved representative image. Demo-PC paths, image names, log messages, and public repository details are accepted capture context. | Credentials, access tokens, private repositories, unrelated personal data, and details from non-demo systems remain forbidden. | Validated | Main shell, viewer toolbar, dock layout, session-console placement, or demo-environment disclosure policy changes. |
| UG-MEDIA-006 | UG-66 | `docs/user-guide/docs/en/assets/images/interface/dashboard-recents.png` | `docs/user-guide/docs/en/interface/dashboard.md` | State | Keep | How does the Dashboard differ for a returning user? | Featured recent file, quick actions, search, sort, counts, hint, and at least three safe thumbnails; onboarding absent. | Isolated profile populated only with approved or synthetic recent items. | Sanitized filenames, thumbnails, paths, timestamps, and metadata; no user identity. | Validated | Recent-item card, search/sort, featured-item, or returning-state logic changes. |
| UG-MEDIA-008 | UG-66 | `docs/user-guide/docs/en/assets/images/interface/docking-panels.png` | `docs/user-guide/docs/en/interface/docking-panels.md` | Interaction | Keep | How do tab grouping and dock layout actions affect multiple image views? | At least two image views, one tabbed dock group, and Lock, Group, Split, and Fit actions visible. | Stable current dock implementation with approved or synthetic non-private images. | No private filenames, metadata, paths, or desktop content. | Validated | Dock toolbar, grouping, splitting, locking, or fit behavior changes. |
| UG-MEDIA-009 | UG-66 | `docs/user-guide/docs/en/assets/images/interface/toolbar-and-menus.png` | `docs/user-guide/docs/en/interface/toolbar-and-menus.md` | Orientation | Keep | Where are processes organized in the main menu? | Main menu bar with Process open and an active safe image in the workspace. | Stable current menu registry and release platform; non-private target image. | No private target names, recent paths, extension repositories, or desktop content. | Validated | Top-level menu hierarchy, Process categories, or main toolbar changes. |

## UG-67 — Viewer and application components

| Media ID | Phase | Canonical asset | Consumer pages | Role | Disposition | Reader question | Distinctive required state | Evidence/data/tool/platform gate | Privacy boundary | Capture status | Replacement trigger |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| UG-MEDIA-007 | UG-67 | `docs/user-guide/docs/en/assets/images/interface/image-viewer.png` | `docs/user-guide/docs/en/interface/image-viewer.md` | Orientation | Keep | Which viewer controls change display only, and where are they located? | Representative still view with the toolbar, channel selector, display-mode selector, and More overflow action visible. | Approved Caldwell 77 still; stable current viewer on the release platform. | Approved target title may remain; no credentials, device serial, or unrelated personal data. | Validated | Viewer toolbar, channel, display-mode, or overflow organization changes. |
| UG-MEDIA-010 | UG-67 | `docs/user-guide/docs/en/assets/images/interface/process-windows.png` | `docs/user-guide/docs/en/interface/process-windows.md` | Orientation | Keep | What controls are shared by target-bound process windows? | Non-Local Bayes process with target identity, parameter controls, preview controls, and Apply, Reset, and Close visible. | Approved Caldwell 77 target and stable process shell; no execution-quality claim. | Approved target name may remain; no credentials, device serial, or unrelated desktop content. | Validated | Generic process shell, target identity, preview, or standard action layout changes. |
| UG-MEDIA-011 | UG-67 | `docs/user-guide/docs/en/assets/images/interface/log-viewer.png` | `docs/user-guide/docs/en/interface/log-viewer.md` | State | Keep | How can a reader recognize filtering, search, and file-log state in the Session Console? | Info and error messages plus visible-level filter, file-log indicator, Search, ANSI Colors, and Settings controls. | Approved documentation capture session on a stable current UI. | Approved demo-PC paths, image names, session messages, and public repository endpoint may remain; no credentials, tokens, private repositories, or unrelated personal data. | Validated | Session Console filtering, search, severity, or file-log UI changes. |
| UG-MEDIA-012 | UG-67 | `docs/user-guide/docs/en/assets/images/interface/image-history.png` | `docs/user-guide/docs/en/interface/image-history.md` | Interaction | Keep | How are current state, historical preview, and lineage distinguished? | Caldwell 77 duplicate with inherited lineage and retained steps; current and selected historical rows, preview, histogram, navigation, save, refresh, and close visible. | Deterministic disposable history sequence on the current build. | Approved image names and a truncated capture-machine path fragment may remain; no credentials or unrelated personal data. | Validated | History timeline, lineage, preview, navigation, or retention UI changes. |
| UG-MEDIA-014 | UG-67 | `docs/user-guide/docs/en/assets/images/interface/image-histogram.png` | `docs/user-guide/docs/en/interface/image-histogram.md` | Interaction | Keep | How are channels, clipping, and histogram resolution inspected? | Representative M20 RGB histogram with all channels, clipping information, plot toolbar, and resolution selector legible. | Approved maintainer-owned RGB still with a meaningful finite distribution. | Approved target title may remain; no credentials, device serial, or unrelated personal data. | Validated | Histogram channels, clipping readouts, plot tools, or resolution controls change. |
| UG-MEDIA-016 | UG-67 | `docs/user-guide/docs/en/assets/images/interface/fits-header-viewer.png` | `docs/user-guide/docs/en/interface/fits-header-viewer.md` | State | Keep | What do representative WCS records look like in the FITS Header table? | Solved FITS header with Key, Value, and Comment columns plus representative celestial WCS rows; the selected file has no accepted SIP coefficients. | Approved solved Caldwell 77 FITS with current WCS metadata. | Approved target identity and celestial metadata may remain; no credentials, device serials, or unrelated sensitive headers. | Validated | FITS Header columns, WCS metadata naming, or viewer behavior changes. |
| UG-MEDIA-018 | UG-67 | `docs/user-guide/docs/en/assets/images/interface/astrometry-info.png` | `docs/user-guide/docs/en/interface/fits-header-viewer.md` | State | Keep | How can a reader tell that an astrometric solution is available? | Solved Caldwell 77 still with the available-status banner and complete solution summary, including an explicit SIP None state. | Approved solved still and current astrometry metadata implementation. | Approved target identity and celestial coordinates may remain; no service keys, credentials, or unrelated personal data. | Validated | Astrometry availability gate or solution-summary fields change. |
| UG-MEDIA-019 | UG-67 | `docs/user-guide/docs/en/assets/images/interface/settings-dialog.png` | `docs/user-guide/docs/en/interface/settings-dialog.md` | Orientation | Keep | How is the Settings dialog organized and applied? | General page with all registered navigation pages, search, page header, Reset Page, and Apply/Cancel/OK. | Designated maintainer capture profile on the current release platform; no data required. | Capture-machine local directories and username-bearing path segments are approved; no credentials, tokens, private repositories, or unrelated personal data. | Validated | Settings navigation, search, reset, footer, or registered-page set changes. |
| UG-MEDIA-024 | UG-67 | `docs/user-guide/docs/en/assets/images/interface/masks.png` | `docs/user-guide/docs/en/interface/masks.md` | Interaction | Keep | How is a mask selected and shown on its target? | Image Mask menu and modeless Mask Selector beside an M83 luminance target with the assigned grayscale mask shown in Overlay Red. | Compatible maintainer-owned target/mask pair on the current viewer. | Approved target and mask names may remain; no credentials, device serial, or unrelated personal data. | Validated | Mask selector, assignment, rendering modes, or viewer mask actions change. |
| UG-MEDIA-025 | UG-67 | `docs/user-guide/docs/en/assets/images/interface/astrometry-overlays.png` | `docs/user-guide/docs/en/interface/astrometry-overlays.md` | Interaction | Keep | How do the available astrometry overlays appear together on a solved image? | M83 field with an equatorial grid, catalog annotation, detected-star markers, and SIP distortion vectors visible together. | Approved solved WCS/SIP still; required local catalog data; current overlay rendering. | Approved target identity and celestial coordinates may remain; no credentials, tokens, or unrelated personal data. | Validated | Overlay styling, catalog content, magnitude behavior, SIP vectors, or toolbar controls change. |
| UG-MEDIA-026 | UG-67 | `docs/user-guide/docs/en/assets/images/interface/sequences.png` | `docs/user-guide/docs/en/interface/sequences.md` | Interaction | Keep | Where are sequence playback, navigation, and frame position shown? | Approved Saturn SER workspace with playback/navigation controls and a meaningful current/total frame readout visible. | Maintainer-owned SER sequence supported by the deployed codec. | Approved sequence name and capture-machine paths may remain; no credentials, device serials, or unrelated personal data. | Validated | Sequence toolbar, playback/navigation, frame readout, or action placement changes. |
| UG-MEDIA-027 | UG-67 | `docs/user-guide/docs/en/assets/images/interface/saving-and-exporting.png` | `docs/user-guide/docs/en/interface/saving-and-exporting.md` | State | Keep | Which export options depend on sequence type, codec, CFA, and resize choices? | CFA Sequence Export dialog with MP4/H.264, output, FPS, debayer, autostretch, resize, dimensions, and fill mode visible. | Approved Saturn CFA sequence; deployed H.264 codec; writable capture-machine output folder. | Approved output filename and capture-machine path may remain; no credentials, device IDs, or unrelated embedded metadata. | Validated | Sequence Export formats, codec dependencies, CFA, resize, or output controls change. |
| UG-MEDIA-028 | UG-67 | `docs/user-guide/docs/en/assets/images/interface/catalogs-data-settings.png` | `docs/user-guide/docs/en/interface/settings-dialog.md` | State | Keep | How do ready, incomplete, and pending Gaia/SPCC catalog states appear? | Catalogs & Data page with Gaia G<=14 selected and ready plus SPCC partial-cache and pending-region states with management actions. | Approved local Gaia/SPCC state on the designated capture profile; no downloads during capture. | Capture-machine cache roots and username-bearing path segments are approved; no credentials, account identifiers, private endpoints, or unrelated personal data. | Validated | Catalog pack descriptors, readiness, download, repair, or eviction controls change. |
| UG-MEDIA-029 | UG-67 | `docs/user-guide/docs/en/assets/images/interface/external-tools-settings.png` | `docs/user-guide/docs/en/interface/settings-dialog.md` | State | Keep | How can a reader recognize supported StarNet2 and DeepSNR readiness? | External Tools page showing StarNet2 2.5.3 and DeepSNR 1.2.2 detected and ready. | Installed supported tool versions and models on the designated capture platform. | Capture-machine executable/model/weights paths and username-bearing path segments are approved; no license secrets, credentials, tokens, or unrelated environment data. | Validated | Tool detection, version gate, readiness wording, or settings controls change. |

## UG-68 — Calibration, registration, solving, stacking, and integration

| Media ID | Phase | Canonical asset | Consumer pages | Role | Disposition | Reader question | Distinctive required state | Evidence/data/tool/platform gate | Privacy boundary | Capture status | Replacement trigger |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| UG-MEDIA-031 | UG-68 | `docs/user-guide/docs/en/assets/images/processes/calibration/image-calibration.png` | `docs/user-guide/docs/en/processes/calibration/image-calibration.md` | State | Keep | How are light frames and master bias, dark, and flat relationships configured? | Lights loaded, master selectors visible, Advanced Dark Settings expanded, and Output & Execution visible. | Approved calibration/light set with compatible masters and isolated output root. | Capture-machine frame/master/output paths, including personal directories, are approved; credentials, tokens, secrets, and unrelated account data remain forbidden. | Validated | Calibration inputs, advanced dark settings, or execution layout changes. |
| UG-MEDIA-037 | UG-68 | `docs/user-guide/docs/en/assets/images/processes/registration-alignment/image-registration.png` | `docs/user-guide/docs/en/processes/registration-alignment/image-registration.md` | State | Keep | What populated metrics and reference state indicate a registration run is ready or complete? | Calibrated frames, writable output, registration/alignment controls, and Stars, FWHM, SNR, Eccentricity, and RMS values. | Approved calibrated set with reproducible registration metrics. | Capture-machine frame/output paths, including personal directories, are approved; credentials, tokens, secrets, and unrelated account data remain forbidden. | Validated | Registration table metrics, reference selection, controls, or output workflow changes. |
| UG-MEDIA-038 | UG-68 | `docs/user-guide/docs/en/assets/images/processes/registration-alignment/plate-solving.png` | `docs/user-guide/docs/en/processes/registration-alignment/plate-solving.md` | Verified result | Keep | How can a reader recognize a successfully solved field and its acquisition hints? | Acquisition Hints expanded with solver status and complete Solved Field values. | Approved UG-16 M83 Internal Gaia result or equivalently approved reproducible solve; required catalog ready. | Recorded field identity, acquisition hints, and capture-machine values are approved; API keys, credentials, tokens, and secrets remain forbidden. | Validated | Solver status, acquisition hints, solved fields, or supported solver workflow changes. |
| UG-MEDIA-039 | UG-68 | `docs/user-guide/docs/en/assets/images/processes/registration-alignment/star-profiler.png` | `docs/user-guide/docs/en/processes/registration-alignment/star-profiler.md` | Comparison | Keep | How do Star Profiler v1 and v2 differ in detected-star and PSF controls? | Paired v1/v2 windows, each bound to a distinct byte-identical copy of one linear field, with both tables and version-specific controls. | Two byte-identical copies of an approved or synthetic representative linear star field; both profilers available. | The duplicate names and approved field identity are retained; credentials, tokens, secrets, and unrelated account data remain forbidden. | Validated | Profiler versions, detection tables, PSF controls, menu routes, or exclusive-target behavior change. |
| UG-MEDIA-040 | UG-68 | `docs/user-guide/docs/en/assets/images/processes/stacking-integration/image-integration.png` | `docs/user-guide/docs/en/processes/stacking-integration/image-integration.md` | Orientation | Keep | How is the full Image Integration workspace organized? | Frame Sources on Lights, all source/workflow tabs visible, writable output selected, and a populated light group. | Approved integration project with isolated writable output root. | Capture-machine project, file, group, and output paths, including personal directories, are approved; credentials, tokens, secrets, and unrelated account data remain forbidden. | Validated | Integration source/workflow tabs, group layout, or output selection changes. |
| UG-MEDIA-044 | UG-68 | `docs/user-guide/docs/en/assets/images/processes/stacking-integration/image-integration-summary.png` | `docs/user-guide/docs/en/processes/stacking-integration/image-integration.md` | State | Keep | What status, history, estimate, and report controls appear after completion? | Summary status, Integration Summary, Run History, resource estimate, and report action. | Reproducible approved-data or permitted synthetic workflow run with retained report evidence. | Capture-machine source/output paths, filenames, project/run names, and hardware text are approved; credentials, tokens, secrets, and unrelated account data remain forbidden. | Validated | Summary status, run history, resource estimate, or report action changes. |
| UG-MEDIA-047 | UG-68 | `docs/user-guide/docs/en/assets/images/processes/stacking-integration/image-list-analyzer.png` | `docs/user-guide/docs/en/processes/stacking-integration/image-list-analyzer.md` | Interaction | Keep | How are frame metrics, metadata, histogram, and optimization status reviewed together? | Populated nine-column table, selected-row metadata and histogram, plus optimization status. | Approved or synthetic still list with deterministic analysis results. | Capture-machine paths, filenames, and displayed acquisition metadata are approved; credentials, tokens, secrets, and unrelated account data remain forbidden. | Validated | Analyzer table, selection detail, histogram, metrics, or optimization UI changes. |

## UG-69 — Color calibration, background extraction, and stretching

| Media ID | Phase | Canonical asset | Consumer pages | Role | Disposition | Reader question | Distinctive required state | Evidence/data/tool/platform gate | Privacy boundary | Capture status | Replacement trigger |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| UG-MEDIA-050 | UG-69 | `docs/user-guide/docs/en/assets/images/processes/color-calibration/spectrophotometric-cc.png` | `docs/user-guide/docs/en/processes/color-calibration/spectrophotometric-cc.md` | State | Keep | How can a reader recognize SPCC catalog, WCS, photometry, and fit readiness? | Readiness panel plus Catalog and WCS Preflight expanded, with Photometry, Fit, and selected target visible. | Approved solved broadband RGB target; required Gaia/SPCC data ready; reproducible preflight state. | Hide target/path identity, coordinates if sensitive, catalog roots, and network/account details. | Planned | SPCC readiness, catalog/WCS preflight, photometry, or fit UI changes. |
| UG-MEDIA-054 | UG-69 | `docs/user-guide/docs/en/assets/images/processes/stretching/histogram-transformation.png` | `docs/user-guide/docs/en/processes/stretching/histogram-transformation.md` | Interaction | Keep | How do histogram handles, clipping, channels, and auto-stretch controls relate? | Linear RGB target with both histograms, toolbar, channel, auto-stretch actions, tonal controls, and clipping readouts. | Approved or synthetic linear RGB image with meaningful histogram. | Sanitized target name and metadata; no source path. | Planned | Histogram editor, clipping, auto-stretch, channel, or action layout changes. |
| UG-MEDIA-055 | UG-69 | `docs/user-guide/docs/en/assets/images/processes/stretching/curves-transformation.png` | `docs/user-guide/docs/en/processes/stretching/curves-transformation.md` | Interaction | Keep | How are control points and interpolation manipulated in Curves Transformation? | Nonlinear RGB target with curve editor, control points, clipping readout, graph toolbar, and interpolation visible. | Approved or synthetic nonlinear RGB image. | Sanitized target name and metadata; no source path. | Planned | Curve editor, point interaction, interpolation, clipping, or graph tools change. |
| UG-MEDIA-056 | UG-69 | `docs/user-guide/docs/en/assets/images/processes/stretching/gh-transformation.png` | `docs/user-guide/docs/en/processes/stretching/gh-transformation.md` | Interaction | Keep | Where are GHT's mode-dependent controls and symmetry-point pickers? | Linear RGB target with histograms, transformation type, current mode controls, and all three SP pickers. | Approved or synthetic linear RGB image with a meaningful distribution. | Sanitized target name and metadata; no source path. | Planned | GHT modes, symmetry-point pickers, histograms, or control visibility changes. |
| UG-MEDIA-077 | UG-69 | `docs/user-guide/docs/en/assets/images/processes/background-extraction/background-extraction.png` | `docs/user-guide/docs/en/processes/background-extraction/background-extraction.md` | Interaction | Keep | How are background samples placed and the fitted model previewed? | Populated sample grid with RBF/Polynomial, correction/output controls, and full-image preview. | Approved linear image with a representative gradient; reproducible sample/model state. | Sanitized target name and metadata; no source or output path. | Planned | Sample-grid interaction, model choices, correction outputs, or preview changes. |
| UG-MEDIA-078 | UG-69 | `docs/user-guide/docs/en/assets/images/processes/background-extraction/auto-gradient-removal.png` | `docs/user-guide/docs/en/processes/background-extraction/auto-gradient-removal.md` | Comparison | Keep | How do gradient-model and structure-protection diagnostics differ? | Background model and structure protection controls with either Gradient model or Protection mask diagnostic visible. | Approved linear gradient/structure image; reproducible diagnostic state. | Sanitized target name and metadata; no source/output path. | Planned | Background/protection models, correction modes, diagnostics, or outputs change. |

## UG-70 — Noise reduction and multiscale

| Media ID | Phase | Canonical asset | Consumer pages | Role | Disposition | Reader question | Distinctive required state | Evidence/data/tool/platform gate | Privacy boundary | Capture status | Replacement trigger |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| UG-MEDIA-061 | UG-70 | `docs/user-guide/docs/en/assets/images/processes/noise-reduction/wavelet-denoise.png` | `docs/user-guide/docs/en/processes/noise-reduction/wavelet-denoise.md` | Interaction | Keep | Which scale, threshold, iteration, amount, and channel controls define native wavelet denoise? | Representative RGB target with Threshold, Scales, Iterations, Amount, Channel, and preview visible. | Approved noisy RGB crop or synthetic equivalent; stable preview state. | Sanitized target name and metadata; no path or acquisition identity. | Planned | Wavelet controls, channel handling, preview, or process shell changes. |
| UG-MEDIA-070 | UG-70 | `docs/user-guide/docs/en/assets/images/processes/noise-reduction/deepsnr.png` | `docs/user-guide/docs/en/processes/noise-reduction/deepsnr.md` | State | Keep | How can a reader recognize a supported DeepSNR configuration before execution? | Supported readiness message with pre-stretch, stride modes, custom stride, weights, target, and Configure visible. | Supported DeepSNR CLI and weights installed on the capture platform; no result claim required. | Hide executable/model/weights paths, username, target path, and environment details. | Planned | DeepSNR version/readiness, stride, weights, or configuration UI changes. |
| UG-MEDIA-103 | UG-70 | `docs/user-guide/docs/en/assets/images/processes/multiscale/multiscale-linear.png` | `docs/user-guide/docs/en/processes/multiscale/multiscale-linear.md` | Interaction | Keep | How are detail layers, the residual, and the selected-layer inspector organized? | MLT/Dyadic state with six detail layers, complete tree including Residual, and selected-layer inspector. | Approved or synthetic RGB still with stable current multiscale editor. | Sanitized target name and metadata; no path. | Planned | Multiscale method, layer tree, residual, inspector, or preview changes. |

## UG-71 — Star operations and masking

| Media ID | Phase | Canonical asset | Consumer pages | Role | Disposition | Reader question | Distinctive required state | Evidence/data/tool/platform gate | Privacy boundary | Capture status | Replacement trigger |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| UG-MEDIA-079 | UG-71 | `docs/user-guide/docs/en/assets/images/processes/star-operations/star-mask.png` | `docs/user-guide/docs/en/processes/star-operations/star-mask.md` | Interaction | Keep | How do backend-specific detection and shaped-area controls affect Star Mask setup? | Representative linear star field with backend-specific detection group and Shaped Areas controls visible. | Approved or synthetic linear star field; selected backend available. | Sanitized target name and metadata; no path or acquisition identity. | Planned | Star Mask backends, detection controls, shaped areas, or preview changes. |
| UG-MEDIA-080 | UG-71 | `docs/user-guide/docs/en/assets/images/processes/star-operations/starnet.png` | `docs/user-guide/docs/en/processes/star-operations/starnet.md`<br>`docs/user-guide/docs/en/processes/star-operations/starnet-star-mask.md` | State | Keep | How can a reader recognize ready StarNet-family configuration? | Ready StarNet state with shared tool settings, output configuration, target, readiness, Configure, and execution actions. | Supported StarNet2 installation and model readiness; safe target; current family UI. | Hide executable/model paths, username, target path, and environment details. | Planned | StarNet readiness, shared settings, output configuration, or family launch UI changes. |
| UG-MEDIA-084 | UG-71 | `docs/user-guide/docs/en/assets/images/processes/star-operations/profiled-starmask-recomb.png` | `docs/user-guide/docs/en/processes/star-operations/profiled-starmask-recomb.md` | Interaction | Keep | How do profile overlays and Binary Areas guide profiled recombination? | Compatible starless/stars inputs, loaded safe profile, Current Stars, Binary Areas, and profile overlays in preview. | Approved or synthetic compatible inputs plus a non-private profile. | Sanitized image/profile names and paths; no embedded source identity. | Planned | Profile format, overlay rendering, Binary Areas, or recombination UI changes. |
| UG-MEDIA-100 | UG-71 | `docs/user-guide/docs/en/assets/images/processes/masking/mask-drawing.png` | `docs/user-guide/docs/en/processes/masking/mask-drawing.md` | Interaction | Keep | Where are direct drawing tools, mask-view modes, and canvas actions? | Grayscale target with drawing tool selected, complete relevant tool settings, Mask View modes, and undo/redo/import/export actions. | Approved or synthetic grayscale target; stable interactive canvas. | Sanitized target/import/export names and paths; no metadata or desktop content. | Planned | Drawing tools, settings, mask modes, history, file actions, or canvas changes. |
| UG-MEDIA-102 | UG-71 | `docs/user-guide/docs/en/assets/images/processes/masking/morphological-transformation.png` | `docs/user-guide/docs/en/processes/masking/morphological-transformation.md` | Interaction | Keep | How is a structuring element edited for a morphological operation? | Grayscale mask with Erosion, iterations, amount, filled 3-by-3 grid, and structuring-element actions visible. | Approved or synthetic grayscale mask target. | Sanitized target name and metadata; no path. | Planned | Morphological operations, grid editor, element actions, or preview changes. |

## UG-72 — Composition, geometry, Pixel Math, and analysis

| Media ID | Phase | Canonical asset | Consumer pages | Role | Disposition | Reader question | Distinctive required state | Evidence/data/tool/platform gate | Privacy boundary | Capture status | Replacement trigger |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| UG-MEDIA-086 | UG-72 | `docs/user-guide/docs/en/assets/images/processes/color-composition/channel-composition.png` | `docs/user-guide/docs/en/processes/color-composition/channel-composition.md` | Interaction | Keep | How are dynamically labelled monochrome sources assigned to output channels? | Color space selected with all three dynamic source selectors, Linear Match, OK, and Close visible. | Approved or synthetic compatible monochrome sources. | Sanitized source names and metadata; no paths. | Planned | Color spaces, dynamic channel labels, source assignment, or actions change. |
| UG-MEDIA-087 | UG-72 | `docs/user-guide/docs/en/assets/images/processes/color-composition/lrgb-combination.png` | `docs/user-guide/docs/en/processes/color-composition/lrgb-combination.md` | Interaction | Keep | How are luminance and RGB sources weighted and previewed together? | Compatible RGB and luminance sources with histogram, selectors, weight/MTF controls, and preview. | Approved or synthetic same-size, same-pixel-type sources. | Sanitized source/target names and metadata; no paths. | Planned | LRGB selectors, weights, MTF controls, histogram, or preview changes. |
| UG-MEDIA-092 | UG-72 | `docs/user-guide/docs/en/assets/images/processes/geometry/crop.png` | `docs/user-guide/docs/en/processes/geometry/crop.md` | Interaction | Keep | How do the crop rectangle and coordinate/size fields correspond? | Solved image with non-full selection rectangle plus position, size, and before/new size readouts. | Approved or synthetic solved image; stable crop overlay. | Sanitized target name, metadata, and coordinates if sensitive; no path. | Planned | Crop overlay, coordinate/size controls, or action behavior changes. |
| UG-MEDIA-096 | UG-72 | `docs/user-guide/docs/en/assets/images/processes/pixel-math/pixel-math.png` | `docs/user-guide/docs/en/processes/pixel-math/pixel-math.md` | Interaction | Keep | Where are expressions, symbols/functions, and output-image settings composed? | RGB/K editor, function/operator/symbol browsers, import/export, and enabled new-image settings. | Safe non-private RGB target; stable Pixel Math editor. | Sanitized target, expression file, and output identifiers; no paths or embedded secrets. | Planned | Expression editor, browsers, settings, output modes, or actions change. |
| UG-MEDIA-097 | UG-72 | `docs/user-guide/docs/en/assets/images/processes/analysis/image-compare.png` | `docs/user-guide/docs/en/processes/analysis/image-compare.md` | Comparison | Keep | How does the split divider compare two images and their histograms? | Two same-size images with raw-data histograms, horizontal split, centered divider, labels, selectors, and preview tools. | Approved or synthetic same-size non-private stills. | Sanitized image names and metadata; no paths. | Planned | Compare layout, split modes, divider, histograms, or selection controls change. |

## UG-73 — Extensions and verified broadband workflow media

| Media ID | Phase | Canonical asset | Consumer pages | Role | Disposition | Reader question | Distinctive required state | Evidence/data/tool/platform gate | Privacy boundary | Capture status | Replacement trigger |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| UG-MEDIA-112 | UG-73 | `docs/user-guide/docs/en/assets/images/interface/extensions-manager.png` | `docs/user-guide/docs/en/extensions/using-extensions.md`<br>`docs/user-guide/docs/en/extensions/extension-marketplace.md` | State | Keep | How are compatible repository packages found, filtered, and trusted? | Browse tab with search, filter, sort, refresh, More Actions, one expanded signed compatible package, and unsigned policy visible. | Approved official/safe repository cache and stable current extension manager; no transaction required. | Exclude private repository URLs, paths, credentials, identities, and unapproved packages. | Planned | Extension Browse, package cards, filters, trust, or policy UI changes. |
| UG-MEDIA-113 | UG-73 | `docs/user-guide/docs/en/assets/images/interface/extensions-status.png` | `docs/user-guide/docs/en/extensions/using-extensions.md` | State | Keep | How can a reader inspect installed extension readiness and maintenance actions? | Installed tab with one safe card expanded to show source, host mode, availability, paths, validation, lifecycle, and maintenance actions. | Approved non-private installed extension and stable Ready-only status behavior. | Sanitize repository, environment, manifest, executable, user, and package paths. | Planned | Installed-card readiness, host modes, validation, lifecycle, or maintenance UI changes. |
| UG-MEDIA-114 | UG-73 | `docs/user-guide/docs/en/assets/images/workflows/broadband-step-calibration.png` | `docs/user-guide/docs/en/workflows/broadband-imaging.md`<br>`docs/user-guide/docs/en/processes/stacking-integration/image-integration.md` | Verified result | Keep | What does a verified populated calibration checkpoint look like in Image Integration? | Calibration tab with loaded approved broadband frames, matched masters, coverage, and isolated output state. | UG-60-approved OSC/RGB/LRGB data or equivalent approved run; reproducible settings and workflow evidence. | Sanitized frame/master/project/output names, paths, observer/site, device IDs, and acquisition metadata. | Planned | Integration calibration tab, matching/coverage, or broadband workflow checkpoint changes. |
| UG-MEDIA-115 | UG-73 | `docs/user-guide/docs/en/assets/images/workflows/broadband-step-stretch.png` | `docs/user-guide/docs/en/workflows/broadband-imaging.md` | Verified result | Keep | What does the first nonlinear broadband transition look like in context? | Approved broadband master after first GHS or Histogram Transformation stretch with process and viewer visible. | Reproducible UG-60-approved-data stretch with retained settings and before-state evidence. | Sanitized target/path/metadata; no private acquisition identity. | Planned | Recommended first-stretch checkpoint, process UI, or viewer presentation changes. |
| UG-MEDIA-116 | UG-73 | `docs/user-guide/docs/en/assets/images/workflows/broadband-final-result.png` | `docs/user-guide/docs/en/workflows/broadband-imaging.md` | Verified result | Keep | What bounded final result and export context does the verified broadband workflow reach? | Approved polished broadband result with viewer, history, and final export context. | Approved result derivative and permission; reproducible settings/history/export evidence; bounded claim only. | Strip observer/site/device/path identity and any metadata not approved for publication. | Planned | Verified workflow result, finish guidance, history, export, or display presentation changes. |

## Deferred media outside the current release

These rows have future owners but no current-release `MEDIA_SLOT`. Their
consumer pages may discuss the deferred capability, but must not link the
missing asset or present it as verified evidence.

| Media ID | Phase | Canonical asset | Consumer pages | Role | Disposition | Reader question | Distinctive required state | Evidence/data/tool/platform gate | Privacy boundary | Capture status | Replacement trigger |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| UG-MEDIA-088 | UG-72 | `docs/user-guide/docs/en/assets/images/processes/color-composition/nbrgb-combination.png` | `docs/user-guide/docs/en/processes/color-composition/nbrgb-combination.md` | State | Deferred | How are broadband and narrowband sources configured together? | Registered linear RGB plus representative mono narrowband source with meaningful preview and bandwidth/scale state. | Blocked on approved representative narrowband and matched continuum data plus reactivated verified workflow evidence. | Sanitized source names, paths, filter metadata, observer/site, and acquisition identity. | Blocked | NBRGB workflow maturity, representative-data gate, or process UI changes before reactivation. |
| UG-MEDIA-089 | UG-72 | `docs/user-guide/docs/en/assets/images/processes/color-composition/nb-color-mapper.png` | `docs/user-guide/docs/en/processes/color-composition/nb-color-mapper.md` | Interaction | Deferred | How are Ha, OIII, and SII layers mapped and adjusted? | Representative registered mono layers with populated table, selected layer, color wheel, modifications, and preview. | Blocked on approved representative Ha/OIII/SII data and reactivated narrowband workflow verification. | Sanitized source names, paths, filter metadata, observer/site, and acquisition identity. | Blocked | Mapper layer model, controls, or narrowband evidence gate changes before reactivation. |
| UG-MEDIA-090 | UG-72 | `docs/user-guide/docs/en/assets/images/processes/color-composition/narrowband-palette-composer.png` | `docs/user-guide/docs/en/processes/color-composition/narrowband-palette-composer.md` | Comparison | Deferred | How do named palette tiles compare for the same verified narrowband inputs? | Registered Ha/OIII/SII sources with complete palette grid, selected SHO tile, intensities, and status. | Blocked on corrected, reactivated UG-61 execution with approved representative branches. | Sanitized source names, paths, filter metadata, observer/site, and acquisition identity. | Blocked | Palette grid, output-state contract, or UG-61 evidence gate changes before reactivation. |
| UG-MEDIA-117 | UG-73 | `docs/user-guide/docs/en/assets/images/workflows/narrowband-palette.png` | `docs/user-guide/docs/en/workflows/narrowband-imaging.md` | Comparison | Deferred | What verified SHO/HOO palette checkpoint would the narrowband workflow produce? | Reproducible palette alternatives from approved representative physical narrowband inputs. | Blocked on UG-61 reactivation, missing representative branches, corrected execution, and permissioned result derivatives. | Strip all acquisition identity, paths, observer/site/device metadata, and unapproved coordinates. | Blocked | UG-61 workflow, palette contract, or representative-data gate changes before reactivation. |
| UG-MEDIA-118 | UG-73 | `docs/user-guide/docs/en/assets/images/workflows/narrowband-final-result.png` | `docs/user-guide/docs/en/workflows/narrowband-imaging.md` | Verified result | Deferred | What bounded final result would the verified narrowband workflow reach? | Approved final SHO, HOO, or custom result with history context. | Blocked on complete reactivated UG-61 representative execution and explicit derivative publication permission. | Strip acquisition identity, paths, observer/site/device metadata, and unapproved coordinates. | Blocked | UG-61 verification, final-result guidance, or evidence permission changes before reactivation. |
| UG-MEDIA-119 | UG-73 | `docs/user-guide/docs/en/assets/images/workflows/planetary-stacking.png` | `docs/user-guide/docs/en/workflows/planetary-imaging.md` | Verified result | Deferred | What supported native planetary stacking checkpoint can AfterNight verify? | Representative sequence and supported alignment/stacking/sharpening boundary with verified result. | Blocked on product maturity, supported native boundary, approved SER data, provenance, and executed workflow plan. | Strip acquisition identity, paths, capture device/site metadata, and unapproved target details. | Blocked | Planetary workflow support, data gate, or process boundary changes before reactivation. |
| UG-MEDIA-120 | UG-73 | `docs/user-guide/docs/en/assets/images/workflows/mosaic-workflow.png` | `docs/user-guide/docs/en/workflows/mosaic-workflow.md` | Verified result | Deferred | What supported native mosaic workflow and overlap result can AfterNight verify? | Approved multi-panel workspace or result with alignment, overlap, normalization, seams, and crop context. | Blocked on native workflow maturity, approved overlapping panels, provenance, and executed verification plan. | Strip acquisition identity, paths, observer/site/device metadata, and unapproved coordinates. | Blocked | Mosaic workflow boundary, data gate, or composition behavior changes before reactivation. |

## Reconciliation and current backlog

| Disposition | Original requests | Canonical rows | Current-release future files |
| --- | ---: | ---: | ---: |
| Keep | 52 | 52 | 51 |
| Merge into kept canonical asset | 4 | 0 | 0 |
| Not needed | 57 | 0 | 0 |
| Deferred | 7 | 7 | 0 |
| **Total** | **120** | **59** | **51** |

The four merged requests create no alias or duplicate files:

- `dashboard-empty.png` uses `UG-MEDIA-001` (`hero-screenshot.png`).
- `image-integration-calibration.png` uses `UG-MEDIA-114`
  (`broadband-step-calibration.png`).
- `image-integration-lite.png` uses `UG-MEDIA-002`
  (`quick-start-step1.png`).
- `starnet-star-mask.png` uses `UG-MEDIA-080` (`starnet.png`).

Ownership belongs to the canonical row. In particular, the Image Integration
calibration consumer does not gate UG-68 because `UG-MEDIA-114` is owned by
UG-73. Multiscale Linear Transform is owned by UG-70. Mask Drawing and
Morphological Transformation are owned by UG-71; Binarize is not needed and has
no media row or slot.
