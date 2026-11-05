# Planetary Imaging


Planetary imaging is a lucky-imaging workflow: capture many short exposures, keep the best frames, stack them to beat seeing noise, then sharpen the integrated result. The AfterNight toolset currently covers sequence loading, extraction/conversion, image-list analysis, stacking-oriented processing, sharpening, and finishing. It is not a dedicated planetary capture suite, so use this page as a practical processing path rather than a camera-control guide.

Use this workflow for:

- SER, AVI, MP4, MOV, MKV, MPG, MPEG, WEBM, or AfterNight sequence inputs.
- Lunar and solar regions.
- Planetary discs where only the best frames should contribute.
- Short-exposure image sets exported from external capture tools.

## Pipeline

The release guide does not include a diagram of this workflow. AfterNight
does not yet expose a verified end-to-end planetary/lucky-imaging pipeline,
so a complete sequence-to-export visual would overstate the current native
workflow. The sections below are retained as planning notes; they do not
describe a verified workflow.

## 1. Open Or Convert The Sequence

Open a sequence through the normal file workflow. The file filters support SER, AVI, MP4, MOV, MKV, MPG, MPEG, WEBM, and `.anseq` sequence files. Use [Extract Sequence](../processes/utility/extract-sequence.md) when you need still frames or a subset of a video-backed sequence.

Useful sequence actions:

| Action | Where it helps |
| --- | --- |
| Extract Frame | Pull one representative frame into the workspace |
| Extract Sequence | Save a chosen span or frame set for downstream work |
| Convert to SER | Normalize a sequence into a planetary-friendly container |
| Convert to FITS | Produce still astronomy frames for inspection or integration |
| Image List Analyzer | Preview and metadata-sort a still-frame list; it does not compute lucky-imaging quality ranks |

See [Non-Interactive Processes](../processes/utility/non-interactive-processes.md), [Extract Sequence](../processes/utility/extract-sequence.md), and [Image List Analyzer](../processes/stacking-integration/image-list-analyzer.md).

## 2. Evaluate Frame Quality

Planetary results depend on quality sorting. If you have a still-frame set, [Image List Analyzer](../processes/stacking-integration/image-list-analyzer.md) can preview frames and sort acquisition metadata, but it has no star-quality score or acceptance threshold; remove only manually identified outliers. For sequence files, extract a representative subset first if the full video is too large.

Reject frames with:

- Severe blur from seeing.
- Tracking jumps.
- Clouds or transparency changes.
- Saturated highlights on the planet or lunar surface.
- Major focus drift.

For planetary work, a smaller stack of sharp frames is usually better than a larger stack that includes poor frames.

## 3. Stack The Best Frames

Use [Image Stacking](../processes/stacking-integration/image-stacking.md) when your selected frames are already aligned well enough or have been converted into an image list. If the frames need star-based registration, [Image Registration](../processes/registration-alignment/image-registration.md) may not be ideal for feature-poor planetary discs; in that case, use external planetary alignment software first, then bring the aligned frames back to AfterNight for stacking and finishing.

Stacking guidance:

| Setting area | Planetary tendency |
| --- | --- |
| Selection | Use the sharpest fraction, not every frame |
| Rejection | Avoid over-rejection on bright planet edges |
| Drizzle | Test only if the capture is undersampled and the data supports it |
| Output | Keep a high-bit-depth master before sharpening |

## 4. Sharpen Detail

After stacking, sharpen with scale-aware tools:

- [Wavelet Denoise](../processes/noise-reduction/wavelet-denoise.md) for scale-separated noise control.
- [Multiscale Linear Transform](../processes/multiscale/multiscale-linear.md) for detail by scale.
- [Unsharp Mask](../processes/sharpening/unsharp-mask.md) for classic local contrast.
- [Local Contrast Enhancement](../processes/sharpening/local-contrast-enhancement.md) for surface detail.

Sharpen in small increments and compare at 100 percent zoom. Planetary images can look impressive while developing hard ringing around the limb, crater rims, or cloud bands.

## 5. Balance Color And Finish

Use finishing processes conservatively:

- [Color Calibration](../processes/color-calibration/color-calibration.md) or [Background Neutralization](../processes/color-calibration/background-neutralization.md) when channel balance is off.
- [Curves Transformation](../processes/stretching/curves-transformation.md) for contrast.
- [Gamma Correction](../processes/stretching/gamma-correction.md) for display copies.
- [Crop](../processes/geometry/crop.md), [Resize](../processes/geometry/resize.md), and [Rotate](../processes/geometry/rotate.md) for presentation framing.

## Practical Tips

- Keep a pre-sharpened stacked master; sharpening is easy to overdo.
- Judge edge artifacts against the dark sky/background, not just the planet surface.
- Save intermediate TIFF or FITS files before display-size exports.
- If AfterNight registration struggles on a planet disc, align externally and return to AfterNight for stacking/detail/color.

## Troubleshooting

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| Stacked planet is smeared | Poor alignment, too many weak frames, or rotation during capture | Select fewer frames or align externally before stacking |
| Limb has bright/dark halos | Sharpening radius or strength is too high | Reduce sharpening, use multiscale edits, and inspect at 100 percent |
| Color channels are offset | Atmospheric dispersion or channel alignment mismatch | Use external channel alignment or process channels separately before recombination |
| Sequence opens but processing is slow | Video decode and frame extraction are IO-heavy | Extract a smaller span or convert to SER/image list first |

## Related Pages

- [Extract Sequence](../processes/utility/extract-sequence.md)
- [Image List Analyzer](../processes/stacking-integration/image-list-analyzer.md)
- [Image Stacking](../processes/stacking-integration/image-stacking.md)
- [Wavelet Denoise](../processes/noise-reduction/wavelet-denoise.md)
- [File Formats](../reference/file-formats.md)
