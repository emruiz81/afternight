# Mosaic Workflow


A mosaic combines multiple overlapping panels into one larger field. The key is consistency: each panel should be calibrated, integrated, background-corrected, stretched, and color-balanced in a way that does not create visible seams.

Use this workflow for:

- Wide nebulae, Milky Way regions, and galaxy fields that do not fit one frame.
- Multi-panel LRGB, OSC, narrowband, or mixed-filter projects.
- Datasets where each panel has overlapping stars with neighboring panels.

## Pipeline

The release guide does not include a diagram of this workflow. AfterNight does
not yet expose a verified end-to-end native mosaic pipeline, and no approved
overlapping-panel dataset has passed the workflow gate. The sections below are
retained as planning notes; they do not describe a verified workflow.

## 1. Plan And Group Panels

A mosaic starts before processing. Keep panel identity visible in folders, filenames, tags, or Image Integration groups.

| Requirement | Why it matters |
| --- | --- |
| Overlap between panels | Gives registration/blending enough shared stars |
| Similar exposure depth | Avoids one panel becoming noisy or dominant |
| Consistent filter/session naming | Keeps calibration and composition manageable |
| Stable rotation and scale | Reduces resampling artifacts |
| Flats per panel/session when needed | Prevents panel-specific vignetting seams |

If panels were captured across nights, treat session differences as real processing variables, not just folder names.

## 2. Integrate Each Panel

Use [Image Integration](../processes/stacking-integration/image-integration.md) for each panel or for a grouped project that preserves panel metadata. Keep calibration, registration, normalization, and stacking settings consistent unless one panel has a clear reason to differ.

For each panel:

1. Calibrate with matching masters.
2. Register lights within the panel.
3. Stack to a linear panel master.
4. Inspect the report and rejection output.
5. Save the linear master before later edits.

Do not aggressively crop individual panels before alignment; keep overlap available.

## 3. Solve And Align Panels

If the panel masters have WCS metadata, use [Plate Solving](../processes/registration-alignment/plate-solving.md) to verify it. SIP distortion correction is handled by the engine when FITS metadata contains the needed coefficients, and the registration/alignment pages document that behavior.

Use [Image Registration](../processes/registration-alignment/image-registration.md) when panels need a common geometry. For large mosaics:

- Use a good reference panel or solved reference.
- Prefer alignment modes that preserve the full field if you need every panel.
- Use intersection/crop later only after confirming the mosaic coverage.
- Save registered panels separately from originals.

## 4. Normalize Backgrounds And Gradients

Panel seams often come from sky background differences rather than registration failure. Work while the panels are still linear:

- Use [Background Extraction](../processes/background-extraction/background-extraction.md) on each panel.
- Use [Linear Match](../processes/color-calibration/linear-match.md) to match panel scale where needed.
- Use [Image Compare](../processes/analysis/image-compare.md) and [Image Blend](../processes/analysis/image-blend.md) to inspect overlap behavior.

Avoid fitting away real nebula gradients. In very large fields, true sky structure can span panels.

## 5. Compose And Crop

After panels share geometry and background scale, compose the mosaic. Depending on your current build and dataset, this may involve a dedicated composition step outside AfterNight or a manual panel-blend workflow using registered masters and blend/geometry tools.

After composition:

- Use [Crop](../processes/geometry/crop.md) to remove blank or low-coverage edges.
- Use [Resize](../processes/geometry/resize.md) only after composition and inspection.
- Keep a linear mosaic master before any permanent stretch.

## 6. Color, Stretch, And Finish

For broadband mosaics, follow the color and stretch guidance in [Broadband Imaging](broadband-imaging.md). For narrowband mosaics, follow [Narrowband Imaging](narrowband-imaging.md).

Common final sequence:

1. Background extraction or final seam cleanup.
2. [Spectrophotometric Color Calibration](../processes/color-calibration/spectrophotometric-cc.md), [Photometric Color Calibration](../processes/color-calibration/photometric-cc.md), or composition-specific color balancing.
3. [Generalized Hyperbolic Transformation](../processes/stretching/gh-transformation.md) or [Histogram Transformation](../processes/stretching/histogram-transformation.md).
4. Denoise, local contrast, star treatment, and final saturation.
5. Export in a master format and a display format.

## Seam Checklist

Before calling the mosaic finished, inspect:

| Area | What to look for |
| --- | --- |
| Background | Panel-to-panel brightness steps or color tint shifts |
| Stars | Doubled stars or stretched stars along seams |
| Noise | One panel visibly noisier after stretch |
| Color | Filter/session mismatch across overlap |
| Edges | Blank canvas, low-coverage corners, or resampling artifacts |

## Troubleshooting

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| Panels do not align | Too little overlap, weak WCS, different scale/rotation, or bad reference | Solve panels, choose a stronger reference, or align externally before returning |
| Visible seams after stretch | Background or normalization mismatch | Rework background extraction and linear matching before stretch |
| One panel has different star size | Seeing, focus, filter, or resampling mismatch | Process that panel separately, reduce stars, or use masks for local correction |
| Mosaic crop removes too much field | Intersection-style alignment or early crop was too aggressive | Rebuild from full-panel masters and crop only after composition |

## Related Pages

- [Image Integration](../processes/stacking-integration/image-integration.md)
- [Image Registration](../processes/registration-alignment/image-registration.md)
- [Plate Solving](../processes/registration-alignment/plate-solving.md)
- [Background Extraction](../processes/background-extraction/background-extraction.md)
- [Broadband Imaging](broadband-imaging.md)
- [Narrowband Imaging](narrowband-imaging.md)
