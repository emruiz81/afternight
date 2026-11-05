# User Guide Screenshot Triage And Mechanism Findings

> **Historical.** This records the 2026-07-22 triage of the original 120 screenshot requests; its status bullets are
> as of that date. The current capture brief is
> [SCREENSHOT_REQUEST_MANIFEST.md](../user-guide/SCREENSHOT_REQUEST_MANIFEST.md). Keep this file in place with its
> decision rows unchanged: `build/scripts/validate_user_guide_media.ps1` reads them.

Date: 2026-07-22

Status: Implemented as the canonical media mechanism on 2026-07-22. The
row-by-row record below remains the approved, immutable reconciliation source
for the original 120 requests.

Reviewed inputs:

- docs/user-guide/SCREENSHOT_REQUEST_MANIFEST.md
- the user guide's content rules (`docs/dev/USER_GUIDE_AUTHORING.md`)
- all SCREENSHOT_REQUEST occurrences under docs/user-guide/docs/en
- the one currently committed screenshot, hero-screenshot.png

## Implementation status and evidence

- The canonical manifest contains 59 retained rows: 52 Keep and seven Deferred.
- Four merged source requests and 57 Not needed requests create no manifest rows,
  slots, aliases, or image files; this full triage and the deterministic
  media contract preserve their audit trail.
- The existing hero is `UG-MEDIA-001`, remains byte-identical at SHA-256
  `169599c34f66fbdc655f2f0749bb440454a7302469279806232bea9fa6715d28`,
  and is placed on Home, First Launch, and Dashboard.
- The 51 missing kept files use 55 consumer placements because four canonical
  assets serve an additional page: Quick Start readiness, StarNet readiness,
  broadband calibration, and Extension Manager Browse. Every missing placement
  is a compact stable `MEDIA_SLOT`; deferred and not-needed media has no slot.
- `build/scripts/validate_user_guide_media.ps1` checks the complete media
  contract. `docs/user-guide/audits/user-guide-media-contract.json` preserves
  counts, phase ownership, merge mappings, eliminated-row decisions, and eight
  negative fixtures.
- The contract fixture was added first and produced the expected red result,
  `The manifest does not use the canonical media schema.`, before the canonical
  manifest and validator implementation made it green.
- Focused media validation and all eight negative fixtures pass; UG-00 and
  UG-01 pass; the checked deterministic audit matches its reviewed baseline;
  and the Windows strict MkDocs build passes, including link/orphan warnings.
- UG-66 through UG-73 remain Not started. This refactor launched no application,
  accessed no approved astronomical data, and created or modified no media.

## Executive recommendation

The current screenshot plan is over-specified and would make the guide visually
repetitive, expensive to maintain, and difficult to read at normal documentation
widths. The problem is not that a 112-page guide may never have many images. The
problem is that most current process requests repeat the same process shell,
target selector, channel selector, preview toolbar, and Apply/Close/Reset actions
while trying to make every control visible in one dense frame.

The manifest currently contains 120 rows:

| Current state | Count |
| --- | ---: |
| Committed | 1 |
| Requested, including phase-tagged variants | 114 |
| Deferred | 4 |
| Not needed | 1 |
| **Total** | **120** |

The proposed editorial disposition is:

| Proposed disposition | Rows | Result |
| --- | ---: | --- |
| Keep as a canonical standalone asset | 52 | Includes the existing hero; 51 future captures remain |
| Merge into another kept asset | 4 | Preserve the teaching purpose without another file |
| Not needed | 57 | Remove the request and its placeholders |
| Deferred | 7 | Retain outside current release capture scope |
| **Total** | **120** | Every manifest row is accounted for |

This reduces the immediate standalone capture backlog from 114 to 51, a
reduction of 63 captures, or about 55 percent. The resulting release guide would
have 23 interface assets, 26 process assets, and 3 workflow assets. That is still
a media-rich guide, but the images would answer distinct reader questions.

## Decision rubric

### Keep

Keep a standalone screenshot when it materially explains at least one of:

- first-time orientation or a major application surface;
- a meaningful dynamic state, readiness gate, completion state, or error state;
- a spatial or direct-manipulation interaction that prose cannot show well;
- a visually distinctive editor, overlay, plot, grid, or multi-image layout;
- a verified workflow checkpoint or result that readers must recognize.

### Merge

Merge when two pages need the same visual evidence. A canonical asset may be
placed on more than one page with page-specific alt text and captions. Merging
must not create an unreadable collage merely to preserve two filenames.

### Not needed

Remove a request when the proposed image would primarily repeat:

- a static form already covered by a canonical parameter/action table;
- the shared process-window shell already explained by Process Windows;
- simple numeric fields, selectors, or standard dialog buttons;
- another retained screenshot without adding a distinct state or interaction.

### Deferred

Retain a request outside current release scope when representative approved data,
workflow maturity, external readiness, or another evidence gate is absent.
Deferred media must not be presented as verified current-release behavior.

## Findings about the current mechanism

### 1. Priority no longer distinguishes importance

Ninety-eight of 120 rows are High priority and 22 are Medium. All 81 process
rows are High. Priority therefore cannot help an editor decide what to capture,
omit, or schedule. The mechanism should record editorial role and evidence gate
instead of a nearly universal priority.

### 2. Capture briefs optimize for control inventory instead of reader value

Many process descriptions require every preview button, channel choice, target
selector, and dialog action to be visible. Those requirements create large,
dense images whose labels become unreadable when the documentation layout scales
them down. Exact control inventory belongs in the page table. A capture brief
should instead state the reader question, distinctive state, required data gate,
and privacy boundary.

### 3. Shared process chrome is requested repeatedly

The process-window anatomy request already covers target identity, preview,
shared display actions, and Apply/Reset/Close. Most routine filters do not need
another screenshot of those same controls. Individual process captures should
be limited to visually distinctive interactions or important dynamic states.

### 4. The manifest, placeholders, and actual asset can disagree

The committed hero image currently shows the empty Dashboard, while its manifest
description asks for a processed-image workspace. The empty Dashboard request
then asks for substantially the same visual a second time. This triage keeps the
existing hero as the canonical fresh-profile Dashboard image and merges the
separate dashboard-empty request into it. The Interface Overview keeps its own
workspace capture.

The First Launch page also references stale target
assets/images/interface/dashboard.png, which is not a manifest row; the manifest
uses dashboard-empty.png.

### 5. Placeholder syntax is duplicated and inconsistent

The guide contains 163 actual SCREENSHOT_REQUEST comments across 100 pages:

| Placeholder form | Count |
| --- | ---: |
| Save as ... | 99 |
| Target file: ... | 18 |
| Generic Missing media remains ... marker | 46 |

There are 117 targeted placeholder comments, one of which is the stale
dashboard.png target. Four manifest rows have no targeted placeholder: the
committed hero, Image Viewer, Process Windows, and the intentionally unneeded
Non-interactive Processes entry. Forty-six pages also carry a second generic
end-of-page missing-media marker. Capture instructions are therefore maintained
in two or three places and cannot be validated reliably with one syntax.

### 6. Phase ownership is not a structured field

Phase ownership is encoded inconsistently in status text, prose such as
Capture is deferred to UG-70, or not at all. Multiscale Linear Transform is
tagged UG-72 in the manifest even though the plan assigns multiscale capture to
UG-70. Mask Drawing, Binarize, and Morphological Transformation should be owned
by UG-71. The revised mechanism needs an explicit Phase column.

### 7. Result media needs a stronger evidence distinction

A screenshot of a populated dialog is interface evidence. A screenshot that
depicts an astronomical output is also execution/result evidence and requires
approved data, reproducible settings, and a bounded claim. The manifest should
record that distinction directly instead of treating both as generic screenshots.

## Proposed replacement mechanism

### Canonical manifest fields

Replace the current Target/Description/Priority/Status table with one canonical
row per retained asset:

| Field | Purpose |
| --- | --- |
| Media ID | Stable identifier independent of filename |
| Phase | One explicit UG-66 through UG-73 owner |
| Canonical asset | Repository-relative output path |
| Consumer pages | Every page allowed to reuse the asset |
| Role | Orientation, state, interaction, comparison, or verified result |
| Disposition | Keep, Merge, Not needed, or Deferred |
| Reader question | What the image communicates that prose does not |
| Required state | Only the distinctive state that must be visible |
| Evidence gate | Data, tool, catalog, platform, or workflow prerequisite |
| Privacy boundary | Paths, metadata, identities, or keys that must be absent |
| Capture status | Planned, Ready, Blocked, Captured, Placed, or Validated |
| Replacement trigger | UI or behavior change that makes the asset stale |

Priority should be removed. Scheduling follows phase order, evidence readiness,
and whether the disposition is Keep.

### Page placeholders

Use the manifest as the only capture brief. While an asset is missing, a page
may contain one compact stable reference:

    <!-- MEDIA_SLOT: UG-MEDIA-### -->

Rules:

1. Do not repeat capture instructions in page comments.
2. Remove all generic Missing media remains comments.
3. Not-needed pages have no media slot.
4. Merged consumers use the same Media ID and canonical asset.
5. Deferred slots are absent from release pages unless the page explicitly
   explains the deferred status.
6. Once captured, replace the slot with the actual image and a reader-focused
   caption; retain the Media ID only in the manifest/provenance records.

### Validation

Add a deterministic media validator that checks:

- every MEDIA_SLOT resolves to exactly one manifest Media ID;
- every kept manifest row has at least one consumer page;
- merged rows identify one kept canonical asset;
- no Not needed row has a placeholder or linked asset;
- captured/placed assets exist and have a provenance hash;
- missing assets are never linked from navigated Markdown;
- SCREENSHOT_REQUEST comments and stale target paths are absent;
- phase ownership agrees with the tracker's phase ledger;
- deferred assets are not represented as current release evidence.

### Page-level visual budget

- Default: no screenshot.
- Typical component/process page: at most one.
- Two are justified only for mutually exclusive states, a meaningful
  before/after comparison, or a setup/completion sequence.
- Reuse canonical assets across pages when the reader question is the same.
- Prefer a readable crop of the distinctive region over a full-window control
  inventory.

## Phase-level impact

| Phase | Keep | Merge | Not needed | Deferred | Editorial result |
| --- | ---: | ---: | ---: | ---: | --- |
| UG-66 | 7 | 1 | 0 | 0 | Preserve onboarding/orientation; reuse the hero for empty Dashboard |
| UG-67 | 14 | 0 | 8 | 0 | Keep unique interface states; remove routine settings/table captures |
| UG-68 | 7 | 2 | 8 | 0 | Keep core integration/registration states; consolidate duplicate Lite/calibration views |
| UG-69 | 6 | 0 | 14 | 0 | Keep graphical stretch/background/SPCC states; remove routine adjustment forms |
| UG-70 | 3 | 0 | 14 | 0 | One denoise example, DeepSNR readiness, and one multiscale editor |
| UG-71 | 5 | 1 | 4 | 0 | Keep star/mask interactions; merge shared StarNet readiness |
| UG-72 | 5 | 0 | 9 | 3 | Keep distinctive composition/geometry/editor/comparison views; defer narrowband-dependent captures |
| UG-73 | 5 | 0 | 0 | 4 | Keep extensions and verified broadband media; retain deferred workflows |
| **Total** | **52** | **4** | **57** | **7** | **51 future standalone captures after the existing hero** |

## Row-by-row triage

Each original manifest row appears exactly once below. Keep means one canonical
asset. Merge means no file at the original target. Not needed means remove the
manifest row and all matching page placeholders during the later mechanism
rewrite.

### UG-66 — Home, Getting Started, Dashboard, Overview, Docking, and menus

| Row | Current target | Decision | Canonical target or action | Rationale |
| ---: | --- | --- | --- | --- |
| 001 | assets/images/interface/hero-screenshot.png | Keep | Keep existing asset and revise its brief to fresh-profile Dashboard/home orientation | Useful home image; current pixels show the empty Dashboard, not the processed workspace described by the manifest |
| 002 | assets/images/interface/quick-start-step1.png | Keep | Same target | Readiness and warning state is a meaningful beginner checkpoint |
| 003 | assets/images/interface/quick-start-result.png | Keep | Same target | Completion state and linear-result context answer a different question from setup |
| 004 | assets/images/interface/overview.png | Keep | Same target | Workspace anatomy is distinct from the Dashboard hero |
| 005 | assets/images/interface/dashboard-empty.png | Merge | Reuse hero-screenshot.png on First Launch and Dashboard | The existing hero already shows onboarding, start cards, drop zone, and hints |
| 006 | assets/images/interface/dashboard-recents.png | Keep | Same target | Returning state is mutually exclusive with onboarding and worth showing |
| 008 | assets/images/interface/docking-panels.png | Keep | Same target | Tab grouping and dock-layout controls are spatial interactions |
| 009 | assets/images/interface/toolbar-and-menus.png | Keep | Same target | Process-menu location and category organization aid navigation |

### UG-67 — Viewer and interface components

| Row | Current target | Decision | Canonical target or action | Rationale |
| ---: | --- | --- | --- | --- |
| 007 | assets/images/interface/image-viewer.png | Keep | Same target | Establishes the canonical viewer toolbar and display-versus-data controls |
| 010 | assets/images/interface/process-windows.png | Keep | Same target | One shared process-window anatomy image replaces many repetitive dialog captures |
| 011 | assets/images/interface/log-viewer.png | Keep | Same target | Filters, severity, search, and logging state form a distinct diagnostic surface |
| 012 | assets/images/interface/image-history.png | Keep | Same target | Timeline selection, preview, and lineage are spatial and stateful |
| 013 | assets/images/interface/image-statistics.png | Not needed | Remove request/placeholders | A static table duplicates the documented field inventory |
| 014 | assets/images/interface/image-histogram.png | Keep | Same target | The plot, channels, clipping, and resolution controls are inherently visual |
| 015 | assets/images/interface/image-summary.png | Not needed | Remove request/placeholders | Static file/sequence fields are clearer in the canonical table |
| 016 | assets/images/interface/fits-header-viewer.png | Keep | Same target | WCS/SIP row recognition is useful and the table is a common troubleshooting surface |
| 017 | assets/images/interface/exif-viewer.png | Not needed | Remove request/placeholders | Repeats the same read-only table pattern as FITS Header without a distinct interaction |
| 018 | assets/images/interface/astrometry-info.png | Keep | Same target | Available/solved status and the compact solution summary are dynamic |
| 019 | assets/images/interface/settings-dialog.png | Keep | Same target | One overview establishes navigation, search, reset, and apply behavior |
| 020 | assets/images/interface/settings-interface.png | Not needed | Remove request/placeholders | Routine settings controls are fully represented by the page inventory |
| 021 | assets/images/interface/settings-raw-images.png | Not needed | Remove request/placeholders | Static selectors and multipliers do not add meaningful visual instruction |
| 022 | assets/images/interface/settings-export.png | Not needed | Remove request/placeholders | Format options are more legible and maintainable in tables |
| 023 | assets/images/interface/settings-logging.png | Not needed | Remove request/placeholders | Static controls plus a path create privacy and staleness cost without unique visual value |
| 024 | assets/images/interface/masks.png | Keep | Same target | Viewer-to-mask relationship and overlay state are difficult to convey with prose alone |
| 025 | assets/images/interface/astrometry-overlays.png | Keep | Same target | The overlays themselves are the feature |
| 026 | assets/images/interface/sequences.png | Keep | Same target | Player, frame position, and sequence actions form a unique workspace |
| 027 | assets/images/interface/saving-and-exporting.png | Keep | Same target | Format/codec/resize dependencies make this a useful stateful dialog example |
| 028 | assets/images/interface/catalogs-data-settings.png | Keep | Same target | Pack readiness and management status are dynamic and operationally important |
| 029 | assets/images/interface/external-tools-settings.png | Keep | Same target | Tool readiness is dynamic and supports troubleshooting |
| 030 | assets/images/interface/plate-solving-settings.png | Not needed | Remove request/placeholders | Static configuration is covered by tables; the retained Plate Solve runtime state is more valuable |

### UG-68 — Calibration, registration, plate solving, stacking, and integration

| Row | Current target | Decision | Canonical target or action | Rationale |
| ---: | --- | --- | --- | --- |
| 031 | assets/images/processes/calibration/image-calibration.png | Keep | Same target | Master relationships and expanded dark settings are a core calibration setup |
| 032 | assets/images/processes/calibration/cosmetic-correction.png | Not needed | Remove request/placeholders | Routine parameter form and shared preview shell |
| 033 | assets/images/processes/calibration/debayer.png | Not needed | Remove request/placeholders | Pattern/method/output choices are concise in the parameter table |
| 034 | assets/images/processes/calibration/split-cfa.png | Not needed | Remove request/placeholders | Small static form with no distinctive visual interaction |
| 035 | assets/images/processes/calibration/merge-cfa.png | Not needed | Remove request/placeholders | Populated selectors do not justify a standalone capture |
| 036 | assets/images/processes/calibration/cfa-integer-resample.png | Not needed | Remove request/placeholders | Numeric size readouts are better documented textually |
| 037 | assets/images/processes/registration-alignment/image-registration.png | Keep | Same target | Populated metrics/reference state is central to registration decisions |
| 038 | assets/images/processes/registration-alignment/plate-solving.png | Keep | Same target | Acquisition hints, solver status, and solved field form a meaningful runtime state |
| 039 | assets/images/processes/registration-alignment/star-profiler.png | Keep | Same target | Paired v1/v2 tables and PSF controls communicate a real model difference |
| 040 | assets/images/processes/stacking-integration/image-integration.png | Keep | Same target | Establishes the full workflow workspace and source/tab structure |
| 041 | assets/images/processes/stacking-integration/image-integration-calibration.png | Merge | Reuse broadband-step-calibration.png | The workflow calibration checkpoint can serve both the process and Broadband pages |
| 042 | assets/images/processes/stacking-integration/image-integration-registration.png | Not needed | Remove request/placeholders | Image Registration already shows populated metrics; this tab is covered by the integration procedure |
| 043 | assets/images/processes/stacking-integration/image-integration-stacking.png | Not needed | Remove request/placeholders | Static stacking options duplicate tables and the workflow overview |
| 044 | assets/images/processes/stacking-integration/image-integration-summary.png | Keep | Same target | Completion, run history, estimate, and report action are a distinct post-run state |
| 045 | assets/images/processes/stacking-integration/image-integration-lite.png | Merge | Reuse quick-start-step1.png | One populated Lite readiness capture should serve both pages |
| 046 | assets/images/processes/stacking-integration/image-stacking.png | Not needed | Remove request/placeholders | Full Image Integration and its parameter tables already cover grouping/rejection concepts |
| 047 | assets/images/processes/stacking-integration/image-list-analyzer.png | Keep | Same target | Table, selected-row metadata, histogram, and optimization status form a unique analysis surface |

### UG-69 — Color calibration, background extraction, stretching, and adjustments

| Row | Current target | Decision | Canonical target or action | Rationale |
| ---: | --- | --- | --- | --- |
| 048 | assets/images/processes/color-calibration/color-calibration.png | Not needed | Remove request/placeholders | Generic form/preview description adds no distinctive state |
| 049 | assets/images/processes/color-calibration/photometric-cc.png | Not needed | Remove request/placeholders | Static selectors and controls are covered by the process table |
| 050 | assets/images/processes/color-calibration/spectrophotometric-cc.png | Keep | Same target | Readiness, catalog/WCS preflight, and fit state are dynamic and operationally important |
| 051 | assets/images/processes/color-calibration/background-neutralization.png | Not needed | Remove request/placeholders | Routine target/reference form with shared preview |
| 052 | assets/images/processes/color-calibration/scnr.png | Not needed | Remove request/placeholders | Simple parameter form; no unique visual interaction |
| 053 | assets/images/processes/color-calibration/linear-match.png | Not needed | Remove request/placeholders | Source/target selectors and scalar controls are clearer in tables |
| 054 | assets/images/processes/stretching/histogram-transformation.png | Keep | Same target | Histogram handles, clipping, channels, and auto-stretch actions are visual |
| 055 | assets/images/processes/stretching/curves-transformation.png | Keep | Same target | Curve editor and control points are a distinctive direct-manipulation surface |
| 056 | assets/images/processes/stretching/gh-transformation.png | Keep | Same target | Complex mode-dependent controls and symmetry-point pickers justify a reference image |
| 057 | assets/images/processes/stretching/arcsinh-stretch.png | Not needed | Remove request/placeholders | Simple stretch form repeats the histogram/preview shell |
| 058 | assets/images/processes/stretching/statistical-stretch.png | Not needed | Remove request/placeholders | Parameter inventory is sufficient; Histogram/GHT captures establish the shared shell |
| 059 | assets/images/processes/stretching/gamma-correction.png | Not needed | Remove request/placeholders | One scalar control plus shared preview shell |
| 060 | assets/images/processes/stretching/exposure-correction.png | Not needed | Remove request/placeholders | One scalar control plus shared preview shell |
| 077 | assets/images/processes/background-extraction/background-extraction.png | Keep | Same target | Sample-grid placement and model preview are inherently visual |
| 078 | assets/images/processes/background-extraction/auto-gradient-removal.png | Keep | Same target | Gradient/protection diagnostic modes are visually distinctive |
| 104 | assets/images/processes/color-adjustment/adjust-hsl.png | Not needed | Remove request/placeholders | Routine sliders and shared histogram/preview shell |
| 105 | assets/images/processes/color-adjustment/adjust-lch.png | Not needed | Remove request/placeholders | Routine sliders and shared histogram/preview shell |
| 106 | assets/images/processes/color-adjustment/adjust-brightness-contrast.png | Not needed | Remove request/placeholders | Routine sliders and shared histogram/preview shell |
| 107 | assets/images/processes/color-adjustment/color-saturation.png | Not needed | Remove request/placeholders | Hue selection and amounts are sufficiently documented in the parameter table |
| 108 | assets/images/processes/color-adjustment/tighten-edges.png | Not needed | Remove request/placeholders | Simple controls and shared preview shell |

### UG-70 — Noise reduction, sharpening, and multiscale

| Row | Current target | Decision | Canonical target or action | Rationale |
| ---: | --- | --- | --- | --- |
| 061 | assets/images/processes/noise-reduction/wavelet-denoise.png | Keep | Same target | Representative native denoise process with meaningful scale/threshold controls |
| 062 | assets/images/processes/noise-reduction/total-variation-denoise.png | Not needed | Remove request/placeholders | Algorithm choices are better captured by the parameter table than repeated preview chrome |
| 063 | assets/images/processes/noise-reduction/nl-bayes.png | Not needed | Remove request/placeholders | Dense control inventory would be unreadable and does not explain the algorithm |
| 064 | assets/images/processes/noise-reduction/patch-pca-denoise.png | Not needed | Remove request/placeholders | Routine parameter form with shared preview |
| 065 | assets/images/processes/noise-reduction/bilateral-filter.png | Not needed | Remove request/placeholders | Routine parameter form with shared preview |
| 066 | assets/images/processes/noise-reduction/gaussian-blur.png | Not needed | Remove request/placeholders | One principal parameter plus shared preview |
| 067 | assets/images/processes/noise-reduction/median-filter.png | Not needed | Remove request/placeholders | Routine parameter form with shared preview |
| 068 | assets/images/processes/noise-reduction/graycstoration.png | Not needed | Remove request/placeholders | Showing all settings creates density without reader orientation value |
| 069 | assets/images/processes/noise-reduction/banding-reduction.png | Not needed | Remove request/placeholders | Routine parameter form with shared preview |
| 070 | assets/images/processes/noise-reduction/deepsnr.png | Keep | Same target | External CLI readiness, stride modes, weights, and configuration state are operationally important |
| 071 | assets/images/processes/sharpening/unsharp-mask.png | Not needed | Remove request/placeholders | Standard controls and shared preview shell |
| 072 | assets/images/processes/sharpening/sharpen-alpha.png | Not needed | Remove request/placeholders | Parameters are covered textually; no unique interaction |
| 073 | assets/images/processes/sharpening/high-pass-filter.png | Not needed | Remove request/placeholders | Histogram and shared preview are already represented elsewhere |
| 074 | assets/images/processes/sharpening/local-contrast-enhancement.png | Not needed | Remove request/placeholders | Routine controls and shared preview shell |
| 075 | assets/images/processes/sharpening/local-histogram-equalization.png | Not needed | Remove request/placeholders | Routine controls and shared preview shell |
| 076 | assets/images/processes/sharpening/clahe.png | Not needed | Remove request/placeholders | Routine controls and shared preview shell |
| 103 | assets/images/processes/multiscale/multiscale-linear.png | Keep | Reassign explicitly to UG-70 | Layer tree, residual, and selected-layer inspector are visually distinctive; current UG-72 tag conflicts with the plan |

### UG-71 — Star operations and masking

| Row | Current target | Decision | Canonical target or action | Rationale |
| ---: | --- | --- | --- | --- |
| 079 | assets/images/processes/star-operations/star-mask.png | Keep | Same target | Backend-specific detection and shaped-area behavior are visually meaningful |
| 080 | assets/images/processes/star-operations/starnet.png | Keep | Same target; broaden brief to StarNet family readiness | External-tool readiness and output configuration deserve one canonical family image |
| 081 | assets/images/processes/star-operations/starnet-star-mask.png | Merge | Reuse starnet.png | Shared readiness/configuration is the important state; process-specific controls remain in its table |
| 082 | assets/images/processes/star-operations/star-reduction.png | Not needed | Remove request/placeholders | Static multi-image selectors and controls do not require a separate screenshot |
| 083 | assets/images/processes/star-operations/starmask-recombination.png | Not needed | Remove request/placeholders | Routine source selectors and shared preview |
| 084 | assets/images/processes/star-operations/profiled-starmask-recomb.png | Keep | Same target | Profile overlays and Binary Areas make the interaction visually distinctive |
| 085 | assets/images/processes/star-operations/dark-structure-enhance.png | Not needed | Remove request/placeholders | Routine form/preview state is covered by shared anatomy and tables |
| 100 | assets/images/processes/masking/mask-drawing.png | Keep | Assign explicitly to UG-71 | Direct drawing tools, mask modes, and canvas actions are inherently visual |
| 101 | assets/images/processes/masking/binarize.png | Not needed | Remove request/placeholders | Simple thresholds and shared preview shell |
| 102 | assets/images/processes/masking/morphological-transformation.png | Keep | Assign explicitly to UG-71 | Structuring-element grid and operation selection are visually distinctive |

### UG-72 — Composition, geometry, Pixel Math, analysis, and utilities

| Row | Current target | Decision | Canonical target or action | Rationale |
| ---: | --- | --- | --- | --- |
| 086 | assets/images/processes/color-composition/channel-composition.png | Keep | Same target | Dynamic channel labels and multiple source assignments are central to the task |
| 087 | assets/images/processes/color-composition/lrgb-combination.png | Keep | Same target | Representative multi-image composition with weights and preview |
| 088 | assets/images/processes/color-composition/nbrgb-combination.png | Deferred | Narrowband data/workflow gate | Do not manufacture a representative populated state from unverified proxy data |
| 089 | assets/images/processes/color-composition/nb-color-mapper.png | Deferred | Narrowband data/workflow gate | Requires representative Ha/OIII/SII sources and meaningful preview state |
| 090 | assets/images/processes/color-composition/narrowband-palette-composer.png | Deferred | Narrowband data/workflow gate | Depends on the currently deferred narrowband workflow evidence |
| 091 | assets/images/processes/color-composition/colorize.png | Not needed | Remove request/placeholders | Routine parameters and shared histogram/preview |
| 092 | assets/images/processes/geometry/crop.png | Keep | Same target | Selection rectangle, overlay, and before/new size are spatial |
| 093 | assets/images/processes/geometry/resize.png | Not needed | Remove request/placeholders | Numeric fields and interpolation choices are clearer in tables |
| 094 | assets/images/processes/geometry/rotate.png | Not needed | Remove request/placeholders | Crop already represents geometry overlay; rotation options are documented textually |
| 095 | assets/images/processes/geometry/integer-resample.png | Not needed | Remove request/placeholders | Numeric size/mode form has no distinctive interaction |
| 096 | assets/images/processes/pixel-math/pixel-math.png | Keep | Same target | Expression editor, symbol/function browsers, and output mode are unique |
| 097 | assets/images/processes/analysis/image-compare.png | Keep | Same target | Split comparison, divider, paired histograms, and two-image selection are visual |
| 098 | assets/images/processes/analysis/image-blend.png | Not needed | Remove request/placeholders | Standard selectors/sliders plus shared preview |
| 099 | assets/images/processes/analysis/range-selection.png | Not needed | Remove request/placeholders | Threshold controls and shared preview are fully described in tables |
| 109 | assets/images/processes/utility/noise-generator.png | Not needed | Remove request/placeholders | Routine parameter form with shared preview |
| 110 | assets/images/processes/utility/extract-sequence.png | Not needed | Remove request/placeholders | Small range form is sufficiently represented by procedure and field table |
| 111 | assets/images/processes/utility/non-interactive-processes.png | Not needed | Retain the existing no-screenshot decision, but remove it from the media manifest | Immediate actions have no dedicated window to illustrate |

### UG-73 — Extensions and release-scope workflows

| Row | Current target | Decision | Canonical target or action | Rationale |
| ---: | --- | --- | --- | --- |
| 112 | assets/images/interface/extensions-manager.png | Keep | Same target | Marketplace search/filter/trust/package state is dynamic and unfamiliar |
| 113 | assets/images/interface/extensions-status.png | Keep | Same target | Installed readiness, host mode, validation, and maintenance actions are operationally important |
| 114 | assets/images/workflows/broadband-step-calibration.png | Keep | Same target; also serve Image Integration Calibration | A verified populated workflow checkpoint can support both pages |
| 115 | assets/images/workflows/broadband-step-stretch.png | Keep | Same target | Process-plus-image checkpoint teaches what the first nonlinear transition looks like |
| 116 | assets/images/workflows/broadband-final-result.png | Keep | Same target | A bounded approved final example gives the workflow an understandable destination |
| 117 | assets/images/workflows/narrowband-palette.png | Deferred | Retain existing post-release gate | Narrowband workflow remains outside release scope |
| 118 | assets/images/workflows/narrowband-final-result.png | Deferred | Retain existing post-release gate | Narrowband workflow remains outside release scope |
| 119 | assets/images/workflows/planetary-stacking.png | Deferred | Retain existing post-release gate | Native lucky-imaging workflow is not release-ready |
| 120 | assets/images/workflows/mosaic-workflow.png | Deferred | Retain existing post-release gate | Native mosaic workflow and representative data remain unavailable |

## Required follow-up changes after approval

This document is designed to drive one later coordinated mechanism rewrite:

1. Replace the manifest schema and apply all 120 dispositions.
2. Give every kept canonical asset a stable Media ID, phase, consumer-page list,
   role, gate, privacy boundary, status, and replacement trigger.
3. Remove 57 not-needed rows and their page placeholders.
4. Convert four merged rows into additional consumer pages of the canonical
   asset; do not create alias image files.
5. Retain seven deferred entries outside current release capture counts.
6. Replace all SCREENSHOT_REQUEST comments with compact MEDIA_SLOT IDs
   only for kept missing placements.
7. Remove all generic end-of-page missing-media comments.
8. Correct the stale First Launch dashboard.png reference.
9. Align UG-70/UG-71 ownership for multiscale and masking assets.
10. Update the phase plan, prerequisite map, tracker media states, provenance
    workflow, authoring templates, and audit validators to use the new model.
11. Verify the committed hero description and consumer pages against its actual
    empty-Dashboard content.
12. Run strict documentation builds and deterministic media-slot/manifest audits
    after the coordinated rewrite.

The triage is intentionally editorial rather than a claim that omitted processes
are unimportant. Their exact controls, algorithms, examples, limitations, and
recovery guidance remain in prose and canonical tables; only redundant visual
inventory is removed.
