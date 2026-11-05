# Image Stacking

## Purpose and expected result

Image Stacking combines two or more already calibrated, same-geometry still
images into one floating-point master per generated frame group. It is the
focused, standalone route for users who already have aligned inputs and do not
need Image Integration to manage calibration, registration, drizzle, project
persistence, workflow reports, or artifact history.

The process can normalize frames, assign global per-frame weights, reject
outlying pixel samples, group by metadata, and save optional rejection maps. A
successful run writes one master for each row in **Frame Groups** and can open
the masters and maps in the workspace. The source frames are not modified.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Preprocess > Image Stacking** |
| Enabled when | Always available; no active image or sequence is required. **Stack Images** remains disabled until at least one generated group, a nonempty output directory, and a nonempty name template exist, and while a run is in progress. Every rejection method needs at least three frames in the group. |
| Inputs | A list of supported still-image files. Frames within each group must have the width, height, channel count, and pixel type of the group's first frame. Comet mode additionally requires exact role-qualified registered products in `registration_metrics.db` beside the outputs. |
| Outputs | One FITS-by-default master per group, plus optional merged or separate low/high rejection maps. Existing destination names receive no confirmation prompt. No workflow report, quality/confidence map, or drizzle product is produced by this window. |
| Recommended image state | Calibrated, registered, linear frames. The process does not align ordinary inputs. |
| Preview support | A static preview of the selected source frame only. A CFA source is demosaiced for display by the viewer. There is no stack preview and changing settings does not recompute the preview. |
| Sequence support | No direct SER/video/sequence input. Extract or export frames to supported still files first. Folder import may add many still files. |
| Mask support | Not supported. An application mask is not read. In every frame an exact zero - such as the border an alignment leaves where a frame does not reach - and a non-finite (NaN or infinite) sample are treated as missing: neither is combined nor judged by a rejection. |
| External dependencies | No external executable or network service. Metric weighting and comet mode depend on a compatible `registration_metrics.db` created by AfterNight registration. CPU execution uses the deployed engine and oneTBB. |

## Before you start

- Calibrate and register the frames first. The ordinary **Stars** path stacks
  pixels at their existing coordinates; it does not read transforms or align
  them.
- Keep each intended group compatible in geometry, pixel type, and channels.
  The process validates each generated group at execution time, including the
  CFA layout: a group that mixes mosaics of different patterns, or mosaics with
  frames that state no CFA, is refused.
- Keep inputs linear. Normalization and rejection estimators are designed for
  the sample relationships in linear data.
- Decide whether grouping is required. With **Filter** enabled by default,
  missing filter metadata becomes `Broadband`; it does not prevent stacking.
- Choose an output location with enough space. Masters use 32-bit float by
  default; **64-bit** doubles their pixel-storage cost. Rejection maps add one
  or two more images per group.
- For FWHM, SNR, or Stars weighting, place the applicable
  `registration_metrics.db` in the selected output directory. When no frame has
  usable figures, the stack falls back to equal weights and the banner says why;
  when only some frames lack them, the banner warns before you stack, naming the
  first frame without figures, and the stack is refused. **Exposure** weighting
  reads the frames' headers and needs no database or output directory; its
  banner says the same before you stack - that no frame states an exposure and
  equal weights will be used, or, when only some frames state one, which is the
  first without one, for a stack that is then refused when it starts.
- **Noise** weighting needs no database: when the stack starts, each frame's
  background noise is measured from its pixels. When no frame has a measurable
  noise - flat frames whose rows do not differ, for one - the stack uses equal
  weights and the message at the end of the run says so; when only some frames
  have one, the stack is refused with an error that names the group.
- Preserve backups. Output filenames are deterministic, and the process does
  not ask before saving to an existing path.

## Procedure

1. Open **Process > Preprocess > Image Stacking**. The process can open with no
   active document.
2. Select **Add Files**, or select **Add Folder** and set the recursive, frame
   type, and optional exact filter-name criteria. File headers are read in the
   background; when an import does not add every file, the window reports how
   many it added, passed over (already listed or kept out by the filter) and
   could not read, with the first error. Confirm that the
   **Frames** table shows the expected files and metadata.
3. Select rows to inspect their static preview. Sorting or using Move Up/Down
   changes the visible list only. Group generation deterministically reorders
   frames by filter, session, exposure, and path; the first frame in that
   generated group becomes the engine reference for global normalization and
   reference-derived metadata. This window does not expose manual reference
   selection.
4. Set **Method**, **Normalization**, **Weighting**, and **Comet Mode**. If
   **Local** is selected, open **Configure Local Normalization...** and accept
   the settings.
5. For **Average**, expand **Rejection Settings**, select a rejection method,
   and set its two dynamic parameters. Enable rejection maps while tuning if
   the method supports rejection.
6. Choose grouping toggles. Confirm every intended output in **Frame Groups**;
   this table is the execution plan, not an editable list.
7. Set **Name Template** and **Directory**. Confirm that the resolved **Output
   Name** values are unique and use a supported suffix.
8. Set output precision, exposure metadata, memory limit, and post-stack CFA
   debayering as needed.
9. Select **Stack Images**. The groups are stacked in the background, so the
   window does not freeze; a progress dialog reports the current group and can
   request cancellation. Controls changed after launch affect a later run,
   because the active run uses a launch-time settings snapshot.
10. Verify every expected master and rejection map on disk. If **Open after
    stacking** is enabled, inspect the opened images at 100% and compare their
    statistics and rejection maps with the input set.

## Complete parameter and action reference

### Frames, preview, and folder import

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Add Files | File-picker action | No files | Last picker directory may be remembered by the application | Supported image-codec files; duplicate paths are skipped | Reads file headers in the background; the window stays responsive | Disabled while a stack runs | Metadata loading costs I/O; unreadable files do not stop the others, and when an import does not add every file the window reports how many it added, passed over (already listed or kept out by the filter) and could not read, with the first error | Still-file list and selected-frame preview | The frame list is saved with the window's settings and imported again, in the order it was saved, when they are restored; picker path uses application path memory |
| Add Folder | Folder-picker and options action | No folder | Last folder is remembered under the stacking import context | Existing directory, then the options below | Scans supported files asynchronously | Disabled while a stack runs | Recursive scans can be I/O-heavy | Still-file list only | Folder path uses application path memory; options are per invocation |
| Include subfolders (recursive) | Checkbox | On | None | On/off | Includes or excludes descendant directories | Add Folder options only | Broader scans take longer and may mix sessions | Folder import | Per invocation |
| Frame Type | Selector | All | None | All, Light, Dark, Flat, Bias, Dark Flat | Filters by the type text the cards state: Light needs LIGHT; Dark needs DARK and not FLAT; Flat needs FLAT and not DARK; Dark Flat needs both; Bias accepts BIAS or OFFSET. A file whose cards state no type counts as LIGHT | Add Folder options only | Prevents unintended calibration-frame mixing when metadata is reliable | Folder import | Per invocation |
| Filter Name | Text | Empty | None | Case-insensitive exact match after trimming; example `Ha` | Empty accepts all filters; text restricts import | Add Folder options only | Incorrect or missing metadata can yield an empty result | Folder import | Per invocation |
| Frames table | Sortable selection table | Empty; ordered by Date ascending | None | File, Date, Size, Filter, ISO/Gain, Offset, Exposure, CFA, Depth | Selecting changes the static preview; supported headers sort the visible source list | Populated after import; Size is display-only rather than a sort key | Generated groups are independently reordered by filter, session, exposure, and path, so table sorting does not select the engine reference | Source list and preview only | Paths saved with the window's settings |
| Remove Selected / Remove | Button/context action | Disabled | None | One or more selected rows | Removes paths from this window, not disk | Enabled with rows/selection | No source data loss | List and later apply | Not serialized |
| Move Up / Move Down | Buttons/context actions | Disabled | None | Selected row within list bounds | Reorders the visible source list | Enabled only when movement is possible | Does not select the engine reference because group generation applies its own deterministic order | List and preview only | Not serialized |
| Delete From Disk | Context action | No | None | Selected row plus confirmation | Removes the row, then permanently deletes its file | Row context menu only | Destructive; a failed deletion is reported with **Could not delete '...' from the disk.** after the row is removed | Source file and list | Not serialized |
| Clear All | Button | Disabled | None | All current rows | Clears frames, groups, preview, and counts, and stops files still being read: none of them is added afterwards | Enabled when rows exist | Does not delete source files | List and preview | Not serialized |
| Current Image / Total Images | Read-only counters | `0` / `0` | None | Derived integers | Reports selected 1-based row and list size | Always visible | No processing effect | Preview/list | Not persisted |
| Image Preview toolbar | Static-view actions | Fit requested on first image | Shared viewer display state may apply | Viewer zoom, pan, display channel, and autostretch actions | Changes display only; CFA input is demosaiced for this preview | Available after a preview loads | Display stretch is not pixel data and does not predict the stack | Preview only; no mask/apply | Viewer-scoped, not stacking settings |

### Combination, normalization, weighting, and rejection

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Method | Selector | Average | Restored from process JSON when available | Average, Sum, Maximum, Minimum, Median, Auto-Adaptive Weighted Average | Chooses the per-pixel combiner; rejection is a submode of Average | Rejection controls are enabled only for Average | Median and auto-adaptive need per-pixel sample sets; extrema emphasize special features/artifacts | Apply, every channel; no preview/mask | `params.stackMethod`; Reset -> Average |
| Normalization | Selector | None | Restored from process JSON | None, Additive, Multiplicative, Additive With Scaling, Multiplicative With Scaling, Local | Matches frames to reference statistics or a spatial model before combination | Local shows its banner/configure action; the With Scaling modes enable Scale Estimator | Statistics and local models add work; unsuitable normalization changes background/contrast relationships | Apply, per channel or linked RGB in local mode | `params.normalizationMode`; Reset -> None |
| Weighting | Selector | None (Equal) | Restored from process JSON | None (Equal), FWHM, Noise, SNR, Stars, Exposure | Gives higher influence to better/larger-weight frames, by the engine's weight sources - **FWHM**, **Noise** and **Stars** are the sources Image Integration offers as Weighted FWHM, Noise and Number of stars; **SNR** and **Exposure** exist only here | The standalone policy honors weighting only for Average with or without rejection. FWHM/SNR/Stars read `registration_metrics.db` in the output directory and Exposure reads each frame's header, and each shows the banner; Noise measures each frame's background noise from its pixels when the stack starts, and its tooltip reads "Noise: weight by each frame's measured background noise."; comet mode clears weights | When no frame has usable figures the stack uses equal weights (the banner says why for FWHM/SNR/Stars/Exposure, the end-of-run message for Noise); when only some frames lack them the stack is refused | Apply, global per-frame weights; no preview/mask | `params.weighting`; Reset -> None (Equal) |
| Scale Estimator | Selector | IKSS | Restored from process JSON | IKSS, MAD | Chooses the dispersion estimator for the With Scaling modes | Enabled for either With Scaling mode; disabled otherwise, Local included, since no other mode reads it | IKSS is more robust and slower; MAD is faster | Apply, per channel | `params.scaleEstimator`; Reset -> IKSS |
| Comet Mode | Selector | Stars | Restored from process JSON | Stars, Comet | Stars consumes listed paths; Comet resolves exact `comet_aligned` registered products for every source frame | Comet requires an output-directory `registration_metrics.db`, an active exact registration state, and all role-qualified outputs; disabled while a stack runs | Comet disables weighting; missing/stale state fails rather than silently using ordinary frames | Apply to each group | `params.cometStackingMode`; Reset -> Stars |
| Configure Local Normalization... | Dialog action | Hidden | Local settings restored from process JSON | See Local Normalization table | Edits the shared local-normalization settings; Cancel discards dialog changes | Visible only when Normalization = Local | Fine grids/local scale increase computation and can imprint noise | Apply only; no preview | `params.localNormalization`; Reset restores Standard defaults |
| Rejection Method | Selector | Winsorized Sigma Clipping | Restored from process JSON | None, Sigma Clipping, MAD Clipping, Median Sigma, Winsorized Sigma Clipping, Percentile, GESDT, Linear Fit | For Average, replaces the plain-average engine method with the selected rejection method | Rejection section enabled only for Average; parameters/maps disabled for None. Every method except None needs at least three frames in the group | Robust methods add iterative statistics/sorting; aggressive rejection reduces effective samples | Apply, per pixel/channel; no mask | `params.rejectionMethod`; Reset -> Winsorized Sigma Clipping |
| Sigma Low / Sigma High | Two numeric sigma thresholds | 3.0 / 3.0 | Restored values are retained until rejection-method changes reset them | UI 0.001–1000.000, step 0.1; every rejection needs a positive threshold | Lower values reject more low/high samples | Average plus sigma, MAD, Median Sigma, or Winsorized method | Too low removes valid signal; too high leaves outliers | Apply, per pixel/channel | `params.sigmaLow`, `params.sigmaHigh`; method selection -> 3/3 |
| Linear Low / Linear High | Two deviation multipliers | 5.0 / 4.0 when Linear Fit is newly selected | Stored values load, but changing into Linear Fit applies its defaults | UI 0.001–1000.000, step 0.1 | Lower values reject smaller deviations from the sorted-sample fitted line | Average + Linear Fit | Better suited to larger stacks; still preserves at least a small retained population | Apply, per pixel/channel | Same sigma keys; method selection -> 5/4 |
| Low Fraction of Median / High Fraction of Median | Two median-relative fractions | 0.2 / 0.1 when Percentile is newly selected, the engine's defaults | Stored values load, but changing into Percentile applies its defaults | UI 0.001–1000.000; the engine consumes positive fractions: `0.2` means a 20% median-relative deviation, not the 20th percentile | Lower fractions reject values closer to the median | Average + Percentile | The broad UI range is not engine-clamped; values above 1 are unusually permissive. This is not rank-percentile trimming | Apply, per pixel/channel | Same sigma keys; method selection -> 0.2/0.1 |
| ESD Outliers / ESD Significance | Fraction / alpha | 0.30 / 0.05 when GESDT is newly selected | Stored values load, but changing into GESDT applies its defaults | UI 0.001–0.999 while GESDT is selected, because the engine accepts each only strictly between 0 and 1 | First sets maximum tested outlier count; second sets the test significance | Average + GESDT; at least 3 frames | Intended for larger approximately normal sample sets | Apply, per pixel/channel | Same sigma keys; method selection -> 0.30/0.05 |
| Save rejection maps | Checkbox | Off | Restored from process JSON | On/off | Requests rejection-frequency maps | Enabled only for Average with a non-None rejection method | Adds compute, memory, and one/two output files | Apply, per pixel/channel | `params.createRejectionMaps`; Reset -> off |
| Merge rejection maps | Checkbox | On but disabled until maps are enabled | Restored from process JSON | On/off | On writes one low+high map; off writes separate low and high maps | Save rejection maps must be enabled and supported | One merged map is compact; separate maps preserve outlier direction | Apply/output | `params.mergeRejectionMaps`; Reset -> on |

### Grouping, output, resources, and window actions

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Filter | Group-by checkbox | On | Restored from process JSON | On/off | Creates separate groups by normalized filter metadata; empty/`-`/`N/A` becomes Broadband | Frame Groups rebuilds immediately | Avoids mixing filters; unreliable metadata can misgroup | Group plan/apply | `params.groupByFilter`; Reset -> on |
| Session | Group-by checkbox | Off | Restored from process JSON | On/off | Groups by parsed capture date, file modification date, or parent-folder fallback | Frame Groups rebuilds immediately | Separates nights but can over-split inconsistent dates | Group plan/apply | `params.groupBySession`; Reset -> off |
| Exposure | Group-by checkbox | Off | Restored from process JSON | On/off | Groups around each group's evolving representative exposure | Enables Tolerance | Useful for mixed exposures; may create many small groups that cannot take a rejection | Group plan/apply | `params.groupByExposure`; Reset -> off |
| Tolerance | Percent spin box | 5.0% | Restored from process JSON | 0.0–100.0%, 0.1 display precision | Larger values merge a wider relative exposure difference, measured against the larger of frame and representative exposure | Enabled only when Exposure is on | Too high mixes materially different exposures; too low fragments groups | Group plan/apply | `params.exposureTolerancePercent`; Reset -> 5% |
| Frame Groups table | Read-only plan | Derived | None | Group, Frames, Filter, Session, Total Exposure, Output Name | Shows exactly which independent stacks and names will run | Rebuilt from frames/grouping/template | Each group's frames must be compatible, and a rejection needs at least three of them | Apply plan | Not serialized; regenerated |
| Name Template | Text | `stacked_$filter.fit` | Restored from process JSON | Text plus `$filter` (empty for Broadband), `$session`, `$exposure`, `$group`; a suffix other than a FITS, TIFF, PNG, JPEG, or XISF one, or none, becomes `.fit`; unsafe filename characters are sanitized | Expands per group; case-insensitive collisions receive a group suffix and then a numeric suffix | Nonempty text required for Stack Images | A supported extension selects the codec; deterministic names may replace existing files without a prompt | Output files | `params.outputTemplate`; Reset -> default |
| Directory | Read-only path | Empty | Restored from process JSON | Chosen with Browse | Chooses masters, maps, and the location searched for `registration_metrics.db` | Nonempty required for Stack Images; created on run if missing | Must hold all outputs; registration database placement affects metric/comet modes | Output/apply | `params.outputDirectory`; **Reset does not clear it** |
| Browse | Directory-picker action | No selection | Starts from current path or, when empty, the first listed frame's directory | Existing directory picker | Updates Directory, the output plan, and the weighting banner | Always available | No processing effect until run | Output setup | Updates Directory persistence |
| 64-bit | Checkbox | Off | Restored from process JSON | Off = 32-bit float, on = 64-bit float engine output | Increases stored calculation/output precision | Independent | Approximately doubles master pixel storage and relevant working memory; source precision is not restored | Apply/output, all channels | `params.process64Bits`; Reset -> off |
| Open after stacking | Checkbox | On | Restored from process JSON | On/off | Opens each saved master and map in workspace order | Independent | Many groups/maps create many views; no effect on saved pixels | Post-apply UI | `params.openAfterStack`; Reset -> on |
| Sum exposure times | Checkbox | Off | Restored from process JSON | On/off | Off writes the reference frame's exposure as `EXPTIME`; on writes the sum of the frames' exposures, a frame stating no exposure counted at the reference's | Independent | Changes metadata, not pixels | Output metadata | `params.sumExposureTimes`; Reset -> off |
| Limit Memory Usage | Checkable group | On | Restored from process JSON | On/off | Caps the stack's working set at Max Memory; off states no limit of the window's own | Independent | Lower peak memory can increase I/O/compute; memory outside the stack's working set is not counted | Apply resource policy | `params.limitMemoryUsage`; Reset -> on |
| Max Memory | Integer MB | 1024 MB | Restored from process JSON | 256–8184 MB, integer | Higher values permit larger tiles/in-memory work | Limit Memory Usage must be on | Too low increases tiling overhead | Apply resource policy | `params.maxMemoryMBValue`; Reset -> 1024 MB |
| Debayer CFA | Checkable group | Off | Restored from process JSON | On/off | If the completed master is still CFA, demosaics the master **after** stacking | Has effect only on CFA output | This is not per-frame pre-stack debayering; leave off for mono or already RGB inputs | Post-stack output | `params.debayerEnabled`; Reset -> off |
| Pattern | Selector | Auto | Restored from process JSON | Auto, RGGB, BGGR, GBRG, GRBG | Supplies CFA pattern to post-stack demosaic; Auto uses the pattern the image states | Debayer CFA must be on and result must be CFA | Wrong override causes color artifacts | Post-stack output | `params.cfaPattern`; Reset -> Auto |
| Method (Debayer CFA) | Selector | RCD | Restored from process JSON | Bilinear, VNG4, AHD, AMaZE, LMMSE, RCD; DCB, HPHD, and IGV are not offered | Selects post-stack demosaic algorithm | Debayer CFA must be on and result must be CFA | Higher-cost methods can improve interpolation but cannot recover information already lost or misregistered | Post-stack output | `params.debayerMethod`; Reset -> RCD |
| Stack Images | Primary action | Disabled with empty plan | None | Requires nonempty frames/groups/directory/template; every group needs compatible frames, and at least three with a rejection | Snapshots settings and stacks each group sequentially in the background | Disabled during a run | Completed earlier groups remain if a later group fails/cancels; no file is left partly written | Full apply | No direct persistence |
| Progress Cancel | Progress-dialog action | Available during run | None | Cancellation request | Stops the engine/current group and prevents later groups from starting | Execution only | Already saved groups remain; no file is left partly written | Current run | Not persisted |
| Reset | Dialog button | Available | None | One action | Restores process controls and local-normalization defaults, but retains frames and current output directory | Disabled while a stack runs | Does not undo files | Future runs/UI | Writes defaults when later serialized |
| Close | Dialog button | Available | None | One action/Escape/window close | Stops any import or stacking in progress and closes at once without waiting for background work | Available during stacking | Completed group outputs remain; the stopped group leaves no partial file | Window only | Settings are stored on close; normal process-settings lifecycle applies |

### Local Normalization Settings

The dialog opens only from **Normalization = Local**. **OK** commits values to
the parent process; **Cancel** discards the dialog edit. Selecting any individual
value changes the preset label to **Custom**.

Image Stacking fits each frame against the reference frame. It applies the grid
size, grid interpolation, background estimator with its percentile and sigma
clipping, BG Smoothing, Low Clip and High Clip, scale mode, and the local scale
with its smoothing, and the dialog offers and the window saves only those. The
dialog is Image Integration's, which also offers the reference strategy, the
evaluation that ranks references, the scale evaluation method with its PSF
settings, and Hot Pixel Radius and Noise Reduction; Image Stacking reads none
of them, so its dialog hides them, says so in a note at its top, and its
settings do not keep them.

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Preset | Selector | Standard (Grid 4) | Exact restored values derive Quick/Standard/Fine or Custom | Quick (Grid 2), Standard (Grid 4), Fine (Grid 8), Custom | Applies grid 2/4/8; shared remaining defaults stay robust | Dialog only | Finer grids are more adaptive and more prone to noise imprinting | Apply only | Derived label; underlying values serialized |
| Grid Size | Integer cells/axis | 4 | Restored | 1–32; engine clamps to its supported maximum | Larger creates `(g+1)^2` model nodes | Always enabled in dialog | More local correction and cost; excessive values can imprint structure/noise | Apply | `gridSize` |
| Interpolation | Selector | Bicubic | Restored | Bilinear, Bicubic | Interpolates background/scale grids | Always enabled | Bicubic is smoother/costlier; bilinear can show transitions | Apply | `gridInterpolation` |
| BG Smoothing | Integer radius | 1 | Restored | 0–10; 0 disables | Smooths background grid nodes | Always enabled | More smoothing suppresses noisy nodes but can miss local changes | Apply | `backgroundSmoothingRadius` |
| Mode | Selector | Linked RGB | Restored | Linked RGB, Per-Channel | Linked uses a common scale; Per-Channel fits channels independently | Color behavior only | Linked is more color-stable; per-channel can suit narrowband but alter ratios | Apply/channel | `scaleMode` |
| Low Clip | Normalized float | 0.000045 | Restored | 0–1, step 0.00001 | Values below threshold use local background estimate | Always enabled | Too high can replace faint valid signal | Apply | `lowClip` |
| High Clip | Normalized float | 0.85 | Restored | 0–1, step 0.01 | Excludes bright samples from local statistics | Always enabled | Too low excludes too much signal; too high admits saturation | Apply | `highClip` |
| Enable Advanced Options | Checkbox | Off | This presentation toggle is not serialized | On/off | Enables advanced controls; it does not itself alter engine parameters | Dialog only | Exposes controls that can destabilize a good default model | Dialog/apply settings | Per dialog opening |
| Estimator | Selector | Sigma Clipped Median | Restored | Percentile, Sigma Clipped Median, Sigma Clipped Mode | Estimates each background node | Advanced on | Robust estimators add work and resist bright structure | Apply | `backgroundEstimator` |
| Percentile | Fraction | 0.25 | Restored | 0–1, step 0.01 | Chooses background percentile | Advanced on + Percentile estimator | Low values follow darker background; extremes can bias nodes | Apply | `backgroundPercentile` |
| Sigma Clip K | Sigma | 3.0 | Restored | 0.1–10.0, step 0.1 | Sets node-estimator clipping threshold | Advanced on + sigma estimator | Lower is more aggressive | Apply | `sigmaClipK` |
| Sigma Iterations | Integer | 3 | Restored | 1–20 | Caps node-estimator rejection iterations | Advanced on + sigma estimator | More iterations cost more and may stabilize contaminated nodes | Apply | `sigmaClipIterations` |
| Use local scale k(x,y) | Checkbox | Off | Restored | On/off | Enables spatially varying scale instead of only local offset/global scale | Advanced on | Experimental; can correct spatial transparency changes or imprint noise | Apply | `localScaleEnabled` |
| Smoothing | Integer radius | 1 | Restored | 0–10 | Smooths local-scale grid | Advanced on + local scale enabled | More smoothing reduces noise imprinting but weakens local response | Apply | `localScaleSmoothingRadius` |
| Reset to Defaults / Cancel / OK | Dialog actions | Standard defaults / discard / accept | None | One action each | Reset restores all values; Cancel discards this opening; OK commits | Dialog only | No pixel work occurs until Stack Images | Settings only | OK values serialize with parent process |

## How the algorithm works

### Conceptual pipeline

1. The process loads metadata, removes duplicate paths, and deterministically
   sorts grouping descriptors by filter, session, exposure, and path.
2. It creates groups from enabled metadata keys and expands/sanitizes a unique
   output name for each group.
3. At launch it snapshots settings. Comet mode may replace listed files with
   exact role-qualified registered products. Each group's frames are probed and
   held to the first frame's dimensions, pixel type, and channels; a group whose
   frames differ in size or colour depth is refused, and the message names the
   group. Every frame is read as fractions of its representation's full scale.
4. Optional global normalization maps each frame toward reference statistics;
   local normalization fits each frame against the reference frame. Optional
   weights are computed by the engine's weight sources, the ones Image Integration uses for Weighted FWHM, Noise and Number of stars (SNR and Exposure exist only here); for **Noise** each frame's background noise is first measured from its pixels.
5. At every pixel/channel, the selected combination operates on the samples
   that mean something - in every frame an exact zero and a non-finite sample
   are left out, so they are neither combined nor judged by a rejection;
   Average can first reject outliers. Tiled and non-tiled paths implement the
   same result contract.
6. The engine builds a 32- or 64-bit floating-point master carrying the first
   frame's metadata as the stack changes it. The standalone process optionally
   post-debayers a CFA master, saves it, then saves requested rejection maps.
   Pixels the stack left without a sample are zero and marked invalid; after a
   post-stack debayer, every colour pixel whose estimate reads one of them is
   marked invalid instead.

### Model and equations

For accepted samples \(x_i\), plain Average uses

\[
\bar{x}=\frac{1}{N}\sum_{i=1}^{N}x_i.
\]

With explicit nonnegative weights \(w_i\), Average and its rejection variants
use the accepted sample/weight association:

\[
\bar{x}_w=\frac{\sum_i w_i x_i}{\sum_i w_i}.
\]

The weights come from the engine's weight sources, the ones
[Image Integration](image-integration.md) uses for Weighted FWHM, Noise and Number of stars (**SNR** and **Exposure** exist only here): **FWHM**,
**SNR**, and **Stars** read each frame's figures from `registration_metrics.db`
in the output directory and **Exposure** reads each frame's exposure. **Noise**
measures each frame's background noise \(\sigma_i\) when the stack starts - the
mean over its channels of the engine's row first-difference estimate, over the
samples as its file stores them - and weighs it \(w_i = 1/\sigma_i^2\), in every
channel. If no frame has a measurable noise the stack uses equal weights and
the message at the end of the run says so; if only some do, the stack is
refused. If no frame has usable figures (the database is missing, nothing was
measured, or every exposure is zero), the stack uses equal weights and the
banner says why. If only some frames lack figures, the stack is refused, naming
the first frame without figures - the banner warns before you stack; no frame
is given zero weight.

Global normalization is applied per channel against reference index 0: the
first frame after deterministic group ordering by filter, session, exposure,
and path. The Frames table order does not control this reference. Let
\(p\) be a pixel, \(l\) a robust location, and \(s\) a robust scale:

| Mode | Transform |
| --- | --- |
| Additive | \(p'=p+(l_r-l_i)\) |
| Multiplicative | \(p'=p(l_r/l_i)\), with neutral fallback when \(l_i=0\) |
| Additive With Scaling | \(p'=p(s_r/s_i)+l_r-(s_r/s_i)l_i\) |
| Multiplicative With Scaling | \(p'=p(s_r/s_i)(l_r/l_i)\) with neutral invalid denominators |

IKSS uses its robust location and scale; MAD uses median and median absolute
deviation; both are computed exactly from the frame's samples. Frames are read as fractions of full scale and normalized values may
remain below 0 or above 1. No combination clamps its result to the unit
interval, so a master can hold values above 1 - where hot pixels or star cores
average above full scale - and below 0.

Combination behavior is exact:

- **Sum** adds the samples, then divides every sum by the largest one so the
  brightest element is exactly 1; a stack with no positive peak is written as
  summed, unnormalized. The engine reports a warning for that case, which the
  window shows when the run ends and logs. It preserves relative ratios but not the
  absolute accumulated pixel sum. The standalone policy does not expose
  weighted Sum.
- **Maximum/Minimum** take extrema of the samples that mean something; an
  exact zero is left out like any missing sample.
- **Median** returns the exact per-pixel median.
- **Auto-Adaptive Weighted Average** runs five DeepSkyStacker-derived robust
  weighting iterations over every column's samples scaled by 65535. A column
  whose scaled samples spread by five units or less, or that holds fewer than
  three samples, takes their plain mean; a column whose largest sample is not
  positive is combined like any other. It ignores the Weighting selector.
- **Sigma Clipping** iteratively rejects deviations from the median using
  standard deviation; **MAD Clipping** substitutes MAD.
- **Median Sigma** replaces rejected values with the current median until
  stable rather than removing them.
- **Winsorized Sigma Clipping** repeatedly clamps a working copy to
  median ± 1.5 sigma, applies the 1.134 correction, converges at a 0.0005
  tolerance, then rejects from the original sample set.
- **Linear Fit** sorts samples, fits a line against sample rank, and iteratively
  rejects deviations scaled by mean absolute residual.
- **Percentile** names the method, not its rule: it rejects when deviation from
  the median exceeds `median × low/high fraction`; it is not quantile trimming.
- **GESDT** performs Rosner's multiple-outlier ESD test using Student-t critical
  values and backward confirmation, testing up to `ceil(N × outlier_fraction)`
  candidates and stopping below three active samples.

Rejection maps store rejected-frame fractions per pixel/channel, each a 32-bit
float fraction of the group's frames, held at 1. The merged file carries
low+high. The standalone UI does not request the engine's structured
quality report, coverage map, variance map, SNR-gain map, weight-sum map,
spatial rejection-cluster report, or automatic re-stack excluding flagged
frames.

### Feature boundary: HDR, overlap normalization, and drizzle

These shared-engine capabilities are deliberately not controls in this
standalone window:

- **HDR exposure merge** is an Image Integration feature; Image Stacking
  cannot enable it.
- **Overlap normalization** is represented in the shared workflow model but is
  not mapped into standalone engine parameters or UI. The listed frames must
  already occupy one common geometry; this process does not mosaic or feather
  panels.
- **Drizzle** requires registration transforms and exposes scale, pixel
  fraction, kernels, CFA-aware mapping, canvas modes, SIP correction, and trim
  offsets through Image Integration. The standalone execution runs an ordinary
  stack and cannot produce drizzle output.

Do not infer any of these features from the shared engine. Use
[Image Integration](image-integration.md) for their exposed workflow.

### Boundary and failure behavior

- Every rejection method needs at least three frames in the group; a smaller
  group with a rejection is refused when the stack starts.
- A group whose frames differ in size, colour depth (pixel type or channel
  count) or CFA layout (pattern, or mosaic beside non-mosaic) is refused, and
  the message names the group.
- Comet mode needs a `registration_metrics.db` with a matching comet run in the
  output directory; without it the stack is refused with a message.
- Ordinary stacking does not apply registration transforms. Misalignment can
  complete successfully and still produce a soft or doubled result.
- An Average with **Noise** weighting over a group only some of whose frames
  have a measurable background noise is refused when the stack starts, with an
  error that names the group; a group none of whose frames has one is stacked
  with equal weights, with a warning.
- What the weights and the engine warn of each group's stack - equal weights
  used, a **Sum** written unnormalized - is shown when the run ends, as
  **Stacking completed with warnings.** followed by each warning prefixed with
  its group (the first eight; the rest are in the log), and each is logged as it
  is given. A run that is stopped shows none.
- An exact zero, such as the border a registration leaves where a frame does
  not reach, and a non-finite (NaN or infinite) sample, such as a calibration
  leaves where its flat was zero, mean nothing in every frame, whatever the
  method: neither is averaged, ranked or judged by a rejection, so a rejection
  map does not count it, and the pixel is combined from the other frames. A
  genuinely black sample of exactly zero is left out too. A pixel no frame
  reaches is written as zero and marked invalid.
- A frame that cannot be read stops its group with an error naming the frame;
  the group is not stacked from the frames that could be read.
- Cancelling, or closing the window, stops the run. Completed group files are
  retained, and no file is left partly written. Errors stop the remaining
  groups and show **Error stacking images**.
- The process snapshots settings and groups before the worker begins, but most
  editable controls remain enabled. Mid-run edits do not change the active run.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Deep-sky lights | Average + Winsorized Sigma Clipping, 3/3, Additive With Scaling, IKSS | Trails/hot pixels remain; backgrounds differ | Valid faint signal is absent from maps/master; too few retained samples | Verify alignment -> normalization -> inspect maps -> adjust high then low threshold |
| Small stack | Median or Average with no/very cautious rejection; fewer than three frames refuse any rejection | Transients remain | Statistical rejection becomes unstable or removes too much | Add frames first; prefer Median before aggressive thresholds |
| Mixed transparency/seeing | Average + a source-backed weight mode | Poor frames influence result equally | The stack is refused when only some frames lack figures | Verify metrics warning -> Equal baseline -> chosen weighting -> compare statistics |
| Uneven spatial backgrounds | Global scaling first; Local Standard only if needed | Residual frame-to-frame background structure | Local grid imprints noise or source structure | Global mode -> Local Standard -> inspect grid size -> only then advanced/local scale |
| Mixed filters/sessions/exposures | Filter on; Session/Exposure only when separate masters are intended | Incompatible data mix in one master | Groups too small for the chosen rejection fail | Verify metadata -> enable one grouping key at a time -> inspect Frames count per group |
| CFA source masters | Keep Debayer CFA off unless all inputs share the same registered CFA sampling | Output remains a CFA mosaic | Wrong pattern/interpolation creates color artifacts; post-stack demosaic cannot fix bad registration | Confirm CFA/type -> stack -> only then enable correct Pattern/Method if required |
| Large project | 32-bit, Limit Memory on at 1024 MB; no maps for first timing pass | Memory pressure or allocation failure | Excessive tiling overhead | Plain group prototype -> tune memory -> add rejection maps -> consider 64-bit last |

## Inputs, outputs, and metadata

One output master is written for each generated group. A template without a
FITS, TIFF, PNG, JPEG, or XISF extension becomes FITS (`.fit`). `$filter`
(empty for Broadband), `$session`, `$exposure`, and `$group` are expanded,
unsafe filename characters are sanitized, repeated and edge underscores are
trimmed, and duplicate case-folded names gain a group/numeric suffix.

The master has the input width, height, and channel count, unless optional
post-stack CFA demosaic converts one CFA channel to RGB. It is 32-bit float by
default or 64-bit float with **64-bit**, whatever the inputs' type. Pixels the
stack left without a sample are zero and marked invalid. A post-stack debayer
carries that mark through: a colour pixel is marked invalid when the demosaic's
estimate of it reads a pixel the stack left without a sample.

The stack's `EXPTIME` is the reference frame's exposure, or, with **Sum exposure
times**, the sum of the frames' exposures, a frame stating no exposure counted
at the reference's; no card is written when no frame states one. The rest of
the master's metadata is the first frame's (in group order) as the engine's
stack changes it.

Merged maps are named `<master>_rej_l+h.<ext>`; separate maps use `_rej_l` and
`_rej_h`. Each map is a 32-bit float fraction of the group's frames. No report, manifest, quality map, log file, temporary calibrated file,
or drizzle file is part of the standalone output contract. Processing messages
go to AfterNight's normal log/console.

## Performance

Runtime scales with pixels × frames × channels. Median, rejection methods,
auto-adaptive weighting, IKSS statistics, and local normalization add work. Rejection and median paths need per-pixel sample storage;
maps add output buffers. Multiple groups repeat loading and analysis.

With **Limit Memory Usage**, the engine selects tiles within the budget where
the algorithm supports tiling and can reduce a request above available memory.
The limit is not a whole-process hard ceiling: metadata, reference statistics,
local-normalization models, output buffers, codecs, and UI views also consume
memory. Start with one representative group, 32-bit output, equal weights, and
no maps; add expensive options after the baseline succeeds.

## Worked example

- Evidence mode: Source-and-test-derived; no approved astrophotography dataset
  or visual quality result was used.
- Input condition/provenance and settings: at least two same-size, same-type,
  registered linear synthetic/still frames with valid `L` filter metadata;
  Filter on, other grouping off, `stacked_$filter.fit`, Average, None
  normalization, None (Equal) weighting, and Rejection Method None.
- Expected checkpoint: **Frame Groups** contains one `L` group named
  `stacked_L.fit`; **Stack Images** writes one 32-bit floating-point master.
- Inspect at 100%/statistics/history: verify alignment was preserved, no group
  was omitted, the output remains linear, and metadata/exposure matches the
  chosen policy. A later rejection run should be evaluated with its map rather
  than by assuming every bright map feature is unwanted.
- Limitations: this predicts contract behavior only. It does not claim a visual
  improvement, execution time, production-scale memory bound, or compatibility
  with unreviewed third-party data.

## Warnings and limitations

- The standalone process is not an aligner, calibrator, sequence stacker,
  drizzle front end, mosaic stitcher, HDR front end, or workflow reporter.
- Sum normalizes the accumulated master to a peak of 1. Use the exposure
  metadata policy for total integration time; do not interpret Sum pixels as
  unnormalized accumulated flux.
- Average's Percentile fields use fractions despite their broad 0.001–1000 UI
  range. Treat values above 1 as outside the meaningful engine domain.
- Metric weighting searches the **output** directory for
  `registration_metrics.db`; selecting a different output directory can cause
  a fallback even when registration metrics exist elsewhere.
- Partial metric coverage refuses the stack, naming the first frame without
  figures, rather than estimating or zero-weighting those frames. When no frame
  has usable figures the stack falls back to equal weights.
- **Noise** weighting reads every frame's pixels once more before the stack,
  which adds a pass over the group's files, and it refuses a group only some of
  whose frames have a measurable noise.
- A Sum with no positive peak is written unnormalized; the window says so when
  the run ends.
- Local normalization fits each frame against the reference frame and offers
  none of Image Integration's reference analysis: no reference strategy,
  evaluation, scale evaluation method, PSF settings, Hot Pixel Radius or Noise
  Reduction.
- **Debayer CFA** runs after stacking, not before it. Inputs must still be
  registered compatibly at the CFA sampling level.
- Rejection maps describe rejected sample frequency, not proof that a frame or
  structure is bad. Inspect the source frames.
- Source files are safe under normal Remove/Clear actions, but **Delete From
  Disk** is intentionally irreversible after confirmation.
- Existing outputs can be replaced without an overwrite confirmation. Verify
  destination names before running.

## Troubleshooting

| Symptom or exact error | Verified cause/validation path | Recovery | Evidence source |
| --- | --- | --- | --- |
| **Missing output directory** | Directory is empty when Stack Images is invoked | Select Browse; verify write access and free space | Launch validation source |
| **No groups to stack** / Stack Images disabled | No frames, no generated groups, empty directory, or empty template | Add frames and inspect Frame Groups; restore a nonempty template/path | Button and launch validation sources |
| **Group '...': frames with mismatched dimensions** / **... mismatched color depth** / **... mismatched filter arrays** | Width/height, pixel type/channel count, or CFA pattern (or mosaic beside non-mosaic) differs inside that group | Resample/convert upstream or split into compatible groups | Standalone group validation |
| Weighting warning; result uses equal weights | No frame has usable figures: the metrics database is missing or unreadable, nothing was measured, or every exposure is zero; the banner says which before you stack | Put the correct database in Directory, re-register, choose Exposure where appropriate, or use Equal | Standalone weighting helpers/tests; `StackingTest.TheWeightingBannerSaysBeforeTheRunWhichFramesStateNoExposure` |
| Stack refused because a frame has no figures | Only some frames lack FWHM/SNR/Stars/Exposure figures; the banner warns before stacking, naming the first such frame | Re-run registration for those frames with the database in Directory, add the exposure to their headers, remove them, or use Equal | Partial-metric warning path/test |
| Error about missing measurements with **Noise** weighting | Only some of the group's frames have a measurable background noise | Remove or replace the frames with no noise to measure (for example flat or constant frames), or choose another weighting | `StandaloneStackingUtils.cpp` (`standaloneNoiseWeights`); `StackingTest.NoiseWeightsAreEachFramesNoiseOrEqualWeightsWhenNoFrameHasOne` |
| **Stacking completed with warnings.** | The weights or the engine warned of a group's stack: equal weights used, or a **Sum** written unnormalized | Read each group's warning; the log holds every one | `ImageStackingProcess.cpp`; `StackingTest.AStackSaysWhatTheEngineWarnedAndLogsIt` |
| Standalone comet stacking requires an active exact run | Database missing, or its state hashes, role products, reference, or per-frame outputs do not match a comet run | Complete comet registration and keep exact products/database together; otherwise select Stars | Transform-role resolver/tests |
| **... RejectionNeedsMoreFrames** | A group with a rejection method has fewer than three frames | Add frames, merge groups, or select Rejection Method None | Engine stack model |
| Satellite trails or hot pixels remain | No rejection, thresholds too permissive, or too few samples | Enable supported rejection, inspect maps, then lower the relevant threshold cautiously | Rejection algorithm/artifact tests |
| Master is soft/doubled | Ordinary inputs were not aligned | Register upstream; this process cannot correct transforms | Standalone execution path |
| Unexpected group/output name | Missing/inconsistent filter/date/exposure metadata or template collision sanitization | Inspect Frames and Frame Groups; correct metadata upstream or simplify grouping/template | Grouping helpers/tests |
| Wrong colors after Debayer CFA | Incorrect pattern, unsuitable post-stack demosaic, or incompatible CFA registration | Verify CFA pattern and register CFA data consistently; otherwise debayer before registration/stacking | Post-stack demosaic source |
| Error after earlier groups succeeded | Later group validation/load/save failed, or cancellation was requested; no file is left partly written | Keep/verify completed outputs, correct the failing group, and rerun; remove stale outputs deliberately | Sequential group worker/error path |

## Related workflow steps

- First [calibrate](../calibration/image-calibration.md) the inputs when they
  are not already calibrated.
- Use [Image Registration](../registration-alignment/image-registration.md)
  before this process when ordinary frames are not already aligned.
- Use [Image Integration](image-integration.md) when you need managed
  calibration/registration, overlap/canvas behavior, drizzle, HDR, quality
  reports/maps, spatial rejection analysis, workflow history, or reports.
- Use [Image Integration Lite](image-integration-lite.md) for its fixed,
  single-target guided workflow rather than exposing these controls.
- Inspect results with [Image Statistics](../../interface/image-statistics.md),
  [Histogram Analysis](../../interface/image-histogram.md), and
  [Image Summary](../../interface/image-summary.md).
- Continue with linear background/color processing before nonlinear stretching.

## References and provenance

- Fruchter, A. S. and Hook, R. N., [*Drizzle: A Method for the Linear
  Reconstruction of Undersampled Images*](https://arxiv.org/abs/astro-ph/9808087).
  This is background for the engine capability that is explicitly unavailable
  in this standalone window.
- Rosner, B., [*Percentage Points for a Generalized ESD Many-Outlier
  Procedure*](https://doi.org/10.1080/00401706.1983.10487848), Technometrics 25
  (1983), 165–172.
- Upstream reference: [Siril stacking documentation](https://siril.readthedocs.io/en/stable/preprocessing/stacking.html)
  for the upstream-compatible normalization/rejection concepts identified in
  engine comments. AfterNight behavior, defaults, ranges, and availability on
  this page come from AfterNight's source and tests, not from the
  upstream program's interface.
- Auto-adaptive combination is adapted from the DeepSkyStacker
  routine: five iterations over every column's samples scaled by 65535.
