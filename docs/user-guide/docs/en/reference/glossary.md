# Glossary

## Purpose, scope, audience, prerequisites, expected result, and authority

This glossary explains current acquisition, calibration, processing, geometry,
file-format, and extension terms used throughout the AfterNight guide. Use it to
interpret a process page or compare similarly named concepts such as display
channels versus processing channels, menu categories versus workflow roles, or
linear data versus a recorded processing-state marker.

Definitions here are authoritative summaries. The linked
process/component/reference page remains authoritative for controls, equations,
exceptions, outputs, and limitations.

## Reachability or applicability

Open **Help > User Guide**, then **Reference > Glossary**, or follow a Glossary
link from a process/component page. Use the browser's page search for an exact
term or abbreviation. Terms describe the current English guide and product
behavior; they are not a substitute for camera, codec, solver, external-tool, or
extension documentation.

## Complete reference matrix

Terms are grouped by the part of the workflow where readers are most likely to
encounter them. A definition may point out a current AfterNight-specific
implementation boundary; it does not imply that every external application uses
the term identically.

### Application Concepts

| Term | Meaning |
| --- | --- |
| Active image | The image window that currently receives process, menu, mask, and viewer actions. |
| AVX2 | The x86-64 vector-instruction extension, with FMA, required by current AfterNight release binaries: the engine and every application target are compiled for it. It is a CPU compatibility boundary, not an optional application setting. On a processor with AVX-512 (the F, BW, DQ, and VL subset), the engine selects its AVX-512 kernels at run time where it has them. |
| Autostretch | A viewer-computed display transfer derived from the image's exact median and normalized MAD. It does not change stored pixels or history unless the user explicitly confirms **Apply Stretch**. |
| Crash dump | A local Crashpad minidump captured after a native crash in supported Linux and Windows MSVC builds. It can contain sensitive process state, requires symbols from the exact build for useful symbolication, and is not uploaded automatically. |
| Dashboard | AfterNight's startup and return surface for onboarding, recent files, quick actions, and local-file drop. It is distinct from the dock workspace. |
| Display transfer | A viewer-only mapping used to render stored samples, such as linear display, autostretch, or a selected View Channel. It is not committed processing unless an explicit action such as **Apply Stretch** changes the image data. |
| Dock workspace | The central surface that hosts standard image/sequence views and the Session Console. A successful standard-view creation or opening the console selects it. |
| Featured file | The Dashboard hero candidate selected independently from grid pinning: normally the first usable recent file, or one explicitly pinned as the featured file. |
| File log | The rotating persistent logging sink, separate from the Session Console's retained in-memory view. Its location and level are configurable, and it can contain file paths or other session details. |
| Global symbol | A Pixel Math named constant: its declared value is the same for every sample of a run, and an expression cannot assign to it - one that tries is refused by name. |
| Image history | A standard view's volatile timeline of at most 20 retained image or sequence states. It supports preview, restore, clone, removal, and a descriptive log, but is not embedded when the image is saved. |
| History lineage | Read-only inherited history copied into a new standard view to describe where it came from. Lineage is not selectable or restorable in the receiving view. |
| Keyboard focus | The currently active window or widget that receives keyboard events. It determines whether a window-scoped, widget-scoped, or direct-handler shortcut applies. |
| Local symbol | A Pixel Math numeric variable reset to its declared initial value before every output sample. |
| Launch mode | A registry contract: **SingleImage** uses the active view and launch guards, **TopLevel** opens an application-level process that selects/validates its own sources, and **NonInteractive** invokes an action without a normal parameter process window. |
| Metasymbol | A Pixel Math built-in image reference beginning with `$`: `$T` is the target, `$A` is the active image, and `$M` is the target's attached mask when one exists. |
| Non-interactive action | A menu action that starts from the active view without opening a parameter window. It may still run a background sequence job or hand off to a dedicated export/extraction dialog. |
| Pixel Math | AfterNight's per-sample expression process. It reads every image an expression names as fractions of full scale into one floating work image - 64-bit when an image read, or the output, holds 64-bit floating-point or 32-bit integer samples, 32-bit otherwise - and does not clamp the result: a floating result keeps values outside `0..1`, an integer result saturates at its range, and **Rescale result** (off by default) maps the smallest and largest finite value across all channels to the stated bounds. `random()` draws from the run's seed, recorded in the result's lineage. It refuses to combine an image of sensor codes or physical quantities with fractions of full scale, and has no preview action. |
| Preview area | A process-owned source-coordinate rectangle that limits real-time preview computation. The processed patch is composited into a full-frame presentation; full Apply still processes the full target. |
| Process | A user-facing operation launched from the Process menu, dashboard, recent/favorite lists, or an extension. |
| Process category | One of the current user-visible Process-menu submenus used for reachability. The application's UI builder defines this placement; it is separate from an internal registry category and from the Process Catalog's workflow role. |
| Processing channel | An optional process selector that converts/extracts one RGB, CIE, HSL, HSV, or YCbCr component, runs the algorithm on it, and reconstructs the result. It changes processed data and is distinct from the viewer's display-only View Channel. |
| Process registry | The application registry that controls descriptor availability, launch mode, guards, and launch binding. The application's UI builder, not a built-in descriptor's registry category, defines exact built-in menu placement. |
| Process window | A top-level or inline control surface that exposes parameters, preview behavior, apply/reset actions, and persistence for a process. |
| Recent file | A normalized local path remembered after a successful open/save path for quick reopening. Removing it from the Dashboard removes history/thumbnail artifacts, not the source file. |
| Real-time preview | An asynchronous, non-committing rendering built from a copy of the target. Each preview uses the parameters the window held when it was requested; a change made while one runs asks for one more preview with the latest settings, and only the newest result is shown. Original/result switching, display autostretch, and histogram controls do not Apply the target. |
| Recorded processing state | The processing-state cards an image carries among its FITS-style header cards: `ANLIN` (linear, nonlinear, or unknown), `ANBASIS` (the channel basis), `ANCCAL` (whether and how it was colour calibrated), `ANASRC` (who stated the record: the loader, a process, or a person's attestation), and `ANOP` (the operation that stated it). A FITS or XISF save writes them and a reopen reads them back; TIFF, PNG, and JPEG carry none. Whether an image is linear is read first from the transfer function its description states and, when it states none, from `ANLIN`; nothing infers it from the sample type. A stretch, a tone or colour adjustment, CLAHE, SCNR, or PixelMath on its target records its result nonlinear under its own name. It is guidance, not proof or the Image History timeline: an operation that drops an image's processing-state cards, such as a geometry change, leaves no record, and a file another program wrote usually states none. |
| Registry category | An internal descriptor grouping and ID prefix used for registration and lookup. It need not match the user-visible Process category or the documentation-only workflow role. |
| Session Console | The in-app log viewer used to inspect messages, warnings, errors, and diagnostics during a processing session. |
| Single-image process | A registry launch mode attached to the active view. It requires a compatible active image and can be rejected for unsupported channels/sequences, a duplicate attached instance, or an exclusive-process conflict. |
| System Information | The copyable/exportable diagnostic report opened from **About > System Information**. It summarizes application, build, dependency, operating-system, hardware, display, and writable-storage facts; it is not a health check or performance benchmark. |
| Top-level process | A registry launch mode that opens an application-level window without the generic single-image guard, then uses its own frame/source selectors and validation. |
| View Channel | The Image Viewer's display-only component selector. It changes the rendered copy without changing stored samples, metadata, pending-save state, or history. |
| Workflow role | The Process Catalog's task/timing group, such as calibration, integration, or finishing. It can differ from the exact Process-menu category used to reach the action. |
| Working copy | A duplicate or derived view used for destructive experiments and comparisons. It is not automatically durable; save/export it explicitly if it must survive the session. |

### Acquisition And Calibration

| Term | Meaning |
| --- | --- |
| ADU | Analog-to-digital unit, the numeric pixel value produced by the camera electronics. |
| Bias frame | A shortest-possible exposure used to measure electronic readout offset. |
| Bayer pattern | The repeating 2×2 placement of red, green, and blue CFA samples, identified as RGGB, BGGR, GBRG, or GRBG by the first two rows. |
| Calibration frame | A bias, dark, flat, or dark-flat frame used to correct light frames. |
| CFA | Color filter array, such as RGGB, GRBG, GBRG, or BGGR Bayer data from an OSC camera. |
| CFA positional plane | One of CFA0–CFA3: samples from a fixed location within every 2×2 Bayer cell, independent of which color that position represents for a selected pattern. |
| CFA-preserving resample | A geometry change that splits CFA0–CFA3, resamples each positional plane independently, and interleaves them back into the same Bayer phase. |
| Dark frame | A closed-shutter exposure matching lights by duration, gain, offset, temperature, and readout mode. |
| Dark optimization | Scaling a dark master to better match thermal signal in each light. The factor is chosen from 0-2 by minimizing noise over the central 512×512 area. Only a single-channel light (including an undebayered OSC frame) at least 512 pixels on each side is optimized; a colour or smaller light is calibrated with the fixed dark factor, and the log says so. This applies in Image Calibration and in Image Integration's light calibration. |
| Dark-flat | A dark frame matched to flats, used when flat exposures need their own dark correction. |
| Debayer | Demosaicing a CFA image into full RGB pixels. |
| Demosaicing | Estimating the two missing color channels at every CFA pixel while preserving the channel sample recorded there. |
| Flat frame | An evenly illuminated frame used to correct vignetting, dust shadows, and pixel response variation. |
| Flat normalization | Dividing a master flat by a representative level so flat correction changes relative illumination without imposing the flat's original overall scale. |
| Gain | Camera amplification setting that affects signal scale and dynamic range. |
| Light frame | A real exposure of the astronomical target. |
| Master frame | A combined calibration or science frame, usually created by stacking many related frames. |
| Offset | Camera black-level setting added to avoid clipped zero values. |
| OSC | One-shot-color camera, usually using a Bayer CFA sensor. |
| Pedestal | A nonnegative offset, entered in 16-bit codes (65535 is full scale, whatever the light's own bit depth), added to a light before calibration subtraction and division to keep a positive baseline where required. It is added as stated, not clipped to the light's range. |

### Integration And Registration

| Term | Meaning |
| --- | --- |
| Alignment | Warping registered frames onto a shared output geometry. |
| Artifact | A generated master, intermediate file, map, metric database, normalization-coefficient document, or drizzle descriptor document whose provenance and validity can be checked for reuse. |
| Binning | Reducing image dimensions by combining rectangular source blocks. AfterNight's ordinary Integer Resample offers Average, Median, Maximum, and Minimum reducers; it discards an incomplete block at the right and bottom edges, an integer Average rounds to nearest, and the reducers do not preserve integrated stored-value sums. |
| Drizzle | Subpixel integration method that can recover resolution from dithered, undersampled data. |
| Dither | A small intentional pointing shift between light frames, useful for rejection and drizzle reconstruction. |
| FWHM | Full width at half maximum, commonly used as a star-size/sharpness metric. |
| Flux | Integrated signal measured by summing sample values over an object or aperture. Geometry resampling can change that stored-value sum unless an explicitly flux-conserving method is used. |
| Instrumental magnitude | A relative brightness measure computed from image counts, typically `-2.5 log10(flux)`, before a photometric zero point turns it into a calibrated magnitude. |
| Moffat profile | A point-spread-function model with power-law wings controlled by beta; it often describes atmospheric star profiles better than a Gaussian. |
| PSF | Point spread function: the image-plane shape produced by a point-like source, measured through widths, angle, wings, and fit residuals. |
| Homography | A projective transform used when simple rotation/scale/translation is not enough. |
| Incremental run | A workflow execution that recomputes only groups made dirty by changed inputs/settings or missing/stale artifacts and reuses unaffected valid work. |
| Integration group | The set of included light frames that produces one final stacked master; Image Integration always separates it by resolution, filter, and object, with optional session, custom-tag, or exposure splits. |
| Interpolation | Estimating output samples from nearby input samples during geometric remapping. Kernel choice trades smoothness, sharpness, ringing, aliasing, and runtime. |
| Normalization | Matching background and signal scale between frames before stacking. |
| Phase correlation | Fourier-domain translation estimator that finds the relative shift between two images from their cross-power spectrum. AfterNight's standalone registration path returns translation only. |
| Plate solving | Matching detected image stars to a sky catalog to derive WCS metadata such as field center, scale, rotation, projection, and optional distortion. |
| Processing master | A durable, high-precision output retained for later processing, commonly FITS, XISF, or a suitable TIFF. A lossy or precision-reducing delivery copy should not be its only representation. |
| RANSAC | Random Sample Consensus, a robust model-fitting method that separates geometrically consistent inliers from false star correspondences. |
| Rejection | Removing outlier pixel samples such as satellites, hot pixels, cosmic rays, or airplane trails. |
| Registration | Matching stars or WCS data so frames share a coordinate system. |
| Registration group | The set of light frames sharing one registration reference and settings contract; Image Integration currently keys it by resolution and effective object. |
| Reference frame | The frame or output plane other frames are registered/aligned against. |
| RMS | Root mean square error; in astrometry and registration it summarizes residual fit error. |
| SIP | Simple Imaging Polynomial, a FITS WCS convention for optical distortion correction. |
| SNR | Signal-to-noise ratio, the strength of real signal relative to noise. |
| WCS | World Coordinate System metadata that maps image pixels to sky coordinates. |
| Workflow manifest | The immutable, versioned description of inputs, group assignments, settings, dependencies, stage sequence, scope, and output names prepared for one workflow execution. |
| Workflow report | The HTML/Markdown report produced by Image Integration to summarize inputs, validation, stages, outputs, warnings, and metrics. |

### Color And Stretching

| Term | Meaning |
| --- | --- |
| ArcSinh stretch | A normalized inverse-hyperbolic-sine display stretch. AfterNight lifts a mono value directly; for RGB it scales black-adjusted channels by one luminance-derived factor, preserving those channel ratios only until individual clipping. |
| Background neutralization | Removing channel offsets from a neutral sky/background sample. |
| Background sample | A small image patch whose representative intensity constrains a fitted background surface. Background Extraction uses fixed 25×25 median patches. |
| Blend mode | A named equation that combines a base sample and blend sample. AfterNight Image Blend exposes 27 current implementation equations; selecting a mode does not align, normalize, or photometrically match the inputs. |
| Brightness/contrast transfer | AfterNight Adjust Brightness/Contrast adds `Brightness/255` and clamps, then scales around normalized `0.5` by `259(Contrast+255)/(255(259-Contrast))` and clamps again. It is neither exposure-stop scaling nor a primary stretch. |
| CIE Lab / LCh | Approximately perceptual color spaces with L* lightness and opponent a*/b* coordinates; LCh expresses the latter as chroma and hue angle. Adjust LCh reads stored RGB as sRGB-encoded (D65), or as linear light when the image states linear RGB; it holds L* to 0-100 and chroma to 0-200, wraps the adjusted hue around the circle, and clips the rebuilt RGB to 0-1. |
| Channel composition | Direct assignment of separate scalar component planes into RGB or another encoded color model before RGB reconstruction. It does not imply registration, normalization, or color calibration. |
| Component encoding | Storage of a derived scalar such as hue, chroma, or CIE a* packed into the 0-1 range (a hue as a fraction of a turn, a* as (a*+128)/255). AfterNight holds such a component as a 32-bit floating-point plane, or 64-bit for a 64-bit floating-point image, so an integer image's component is not rounded to the image's codes. It does not store a human-readable unit such as degrees. |
| Exposure stop | A base-two linear-light exposure interval: a change of `s` stops corresponds to multiplier `2^s`, so +1 doubles and -1 halves the signal. AfterNight's current Exposure Correction control does not implement this standard mapping. |
| GHS | Generalized Hyperbolic Stretch, the five-parameter intensity-transform family built from strength, local intensity, a symmetry point, and shadow/highlight protection points. |
| GHT | Generalized Hyperbolic Transformation, AfterNight's multi-family stretching process that includes GHS, histogram, arcsinh, linear, inversion, and STF transfers. |
| Gamma correction | A power-law tone mapping. AfterNight evaluates normalized stored or selected-component samples as `output = input^(1/gamma)`, so values above `1` brighten midtones and values below `1` darken them. |
| Gaia XP | Gaia DR3's externally calibrated low-resolution BP/RP spectra; AfterNight SPCC uses a separate sampled-spectrum catalog rather than the Gaia astrometric/PCC catalog. |
| HOO | Narrowband palette mapping Ha to red and OIII to green/blue. |
| HSI / HSL / HSV | Hue-based three-component models whose third component is intensity, lightness, or value respectively. Their scalar component planes are not interchangeable. Adjust HSL uses ordinary stored-RGB HSL, wraps hue circularly, scales saturation asymmetrically (`+25` doubles it), and adds HSL lightness; HSL lightness is not CIE L*. Color Saturation instead multiplies HSL saturation inside hard hue bands only above a green-statistics-derived lightness cutoff. Colorize assigns fixed H/S with either HSV Value or HSI Intensity; its optional CIE L* source is only the input scalar, not a guarantee of output lightness preservation. |
| Modified Sobel edge map | Tighten Edges combines absolute responses from four fixed 3×3 directional derivative kernels, scales their sum by `0.05`, and subtracts Value times that nonnegative map before Amount blending. It is not a selectable-scale Sobel operator, symmetric sharpening, or mathematical morphology. |
| Linear image | Image data whose pixel values still preserve proportional scene signal, usually before stretching. |
| LRGB | A luminance-plus-RGB workflow. AfterNight's LRGB Combination replaces or blends CIE L* from a grayscale source and keeps the target's CIE hue; it can stretch the luminance and the chroma with a midtones transfer first, then optionally scales reconstructed R, G, and B. A luminance of another sample type is read in its own units. It does not imply registration, matching, or type promotion. |
| NB Color Mapper | A free-form narrowband mixer that applies per-layer MTF and optional nonrecursive cross-layer modifiers, maps eligible mono layers to fixed HSV hue/saturation, then uses an N-layer screen blend. It is not fixed SHO/HOO channel assignment or spectral calibration. |
| Narrowband Palette Composer | A fixed-formula narrowband explorer that synthesizes roles from direct or OSC inputs, stretches each input automatically from exact statistics, applies one of 12 fixed palettes (eleven channel mixes or the nonlinear Foraxx mapping), clips each component, then globally peak-normalizes every nonblack output. It is not a custom matrix or spectral calibration. |
| NBRGB | A narrowband-plus-broadband workflow. AfterNight's NBRGB Combination uses user-entered rectangular passband widths to estimate continuum-corrected mono line planes, boosts them, fits them to the corresponding RGB channels where enough samples within the fit limits vary, then keeps the per-pixel maximum; it does not model central wavelengths, measured transmission, exposure scaling, or registration. |
| Midtones | The histogram region between shadows and highlights. In AfterNight's Histogram Transformation, the relative Midtones value is the input point mapped to output `0.5` after Shadows/Highlights rescaling. |
| Midtones transfer function (MTF) | A rational normalized mapping through black, a configurable `{midtones, 0.5}` point, and white. `0.5` is the identity midtones setting; smaller values brighten and larger values darken the unclipped interval. |
| Nonlinear image | Image data that has been stretched or tone-mapped for display, so pixel values no longer preserve original proportional signal. |
| Palette | A channel-to-color mapping, especially for narrowband masters such as SHO, HOO, or custom mapped-color combinations. |
| PCC | Photometric Color Calibration, using catalog star photometry to calibrate color. |
| Protection points (LP/HP) | In GHT, the source intensities below/above which the nonlinear curve continues along tangent lines to reserve shadow/highlight contrast; they are not clipping points. |
| Response profile | A versioned set of sensor, filter, window/LPF, and related throughput curves used to predict how a spectrum produces R/G/B counts. |
| Radial basis function (RBF) | A surface built from distance-based kernels centered on samples. Background Extraction fits a smoothed thin-plate spline through the sample medians and evaluates it at every pixel. |
| SCNR | Subtractive Chromatic Noise Reduction, often used sparingly for residual green cast. |
| SHO | Narrowband palette mapping SII to red, Ha to green, and OIII to blue. |
| Spline | A piecewise interpolation curve that passes through ordered control points; Curves Transformation exposes Linear, Cubic, Akima, Steffen, and Monotone Cubic choices, the last of which never overshoots a node. |
| SPCC | Spectrophotometric Color Calibration, fitting measured image-star color ratios to synthetic ratios from catalog spectra and a selected response profile. |
| Stretch | A nonlinear transform that makes faint linear data visible. |
| Symmetry point (SP) | In GHT, the source intensity at which the nonlinear transfer has maximum slope and around which the base curve is constructed symmetrically. |
| Synthetic photometry | Integrating a sampled spectrum through a passband/response curve to predict relative channel counts or magnitudes. |
| White reference | A versioned reference spectrum that defines the balance point where an SPCC response fit is evaluated. |
| YCbCr | A luma plus two color-difference component model. AfterNight encodes neutral Cb/Cr at normalized `0.5`; it is not a substitute for calibrated luminance/chrominance data. |

### Masks, Stars, And Local Processing

| Term | Meaning |
| --- | --- |
| Anisotropic diffusion | Smoothing whose strength and direction vary with local image structure. GRAYCstoration estimates a diffusion-tensor field and integrates samples along its curved paths. |
| Anscombe transform | A square-root variance-stabilizing transform for Poisson-like counts. NL-Bayes optionally uses a fixed unit-gain forward transform and a closed-form low-count inverse; it does not read camera gain or read-noise metadata. |
| Banding | Row- or column-aligned brightness offsets, often associated with sensor/readout behavior. AfterNight's Banding Reduction corrects one constant per row or column and does not model diagonal or within-line variation. |
| Binarize | Convert each stored grayscale or RGB channel to the lowest or highest value of its sample format (0 or 1 for a floating-point image) with an inclusive hard threshold; a floating-point sample above 1 becomes 0. |
| Closing | A morphological dilation followed by erosion, commonly used to fill small dark gaps in bright structures. |
| CLAHE | Contrast-limited adaptive histogram equalization: a local-contrast method that clips each tile histogram, redistributes excess counts, builds cumulative mappings, and interpolates neighboring tile results. AfterNight's default RGB path applies it to Lab lightness. |
| Cumulative distribution function (CDF) | The running sum of histogram counts from the lowest bin through the current bin. Histogram equalization rescales this cumulative value into an output lookup mapping. |
| Dilation | A morphological local-maximum operation that grows bright structures and shrinks dark ones. |
| Cycle spinning | Repeating a shift-sensitive transform after different image shifts, undoing each shift, and averaging the results. Wavelet Denoise uses pseudo-random periodic shifts drawn from a fixed seed, so one image and one set of settings always give one result, to reduce decimated-Haar block/translation artifacts. |
| DA3D | Data Adaptive Dual Domain Denoising, an optional final NL-Bayes refinement that uses the noisy input and denoised guide to shrink shape/data-adaptive patches in the Fourier domain. |
| Dark-structure mask | Dark Structure Enhance's mono mask: the difference between a selected coarse starlet residual and the source (largest where the source is darker), averaged over the channels with equal weight, rescaled by its own minimum and maximum, denoised by a fixed three-layer starlet (finest detail dropped, next detail soft-thresholded), and rescaled again. A frame with no measurable dark structure gives an all-zero mask. |
| DeepSNR | Separately installed, learned astrophotography denoiser that AfterNight invokes through a versioned command-line interface. |
| Erosion | A morphological local-minimum operation that shrinks bright structures and grows dark ones. |
| Haar wavelet | A compact, decimated transform that represents neighboring samples as averages and differences at successively coarser scales. |
| High-pass detail layer | A source-minus-low-pass residual recentered around a neutral value. AfterNight's High Pass Filter writes `clip(source - blur + 0.5)`; it does not blend that layer back into the source or directly produce a sharpened image. |
| Local contrast | Contrast enhancement over a limited spatial scale instead of the whole histogram. |
| Line-integral convolution (LIC) | Averaging samples along integral curves of a vector/tensor field. GRAYCstoration's bundled CImg path uses LIC-like curved integration for stable anisotropic smoothing. |
| Mask | A runtime link from a target to another open image view. For a normal enabled AfterNight mask, normalized white admits the processed sample, black preserves the original, and intermediate values blend them; Invert reverses that weighting. Show and rendering mode affect only the viewer display. |
| Morphology | Shape operations such as erosion, dilation, opening, closing, morphological selection (a chosen rank between the erosion and the dilation), or mask cleanup. |
| Opening | A morphological erosion followed by dilation, commonly used to remove small bright structures. |
| Multiscale Linear Transform (MLT) | AfterNight's undecimated Gaussian-pyramid option in Multiscale Linear Transform. It blurs the original image at successively wider dyadic or linear-spaced scales, stores differences between adjacent levels, and retains the last coarse level as the residual. It is distinct from the process's configurable recursive Starlet option. |
| Multiscale residual | The final coarse image left after all requested detail layers are separated. In Multiscale Linear Transform it can be enabled, biased, or tone-mapped with LSTF before reconstruction, but it does not accept per-layer noise reduction. |
| Multiscale alpha sharpening | Detail enhancement that decomposes an image over several blur scales, raises the opacity of the detail layers, and alpha-composites them over the residual base. AfterNight exposes this G'MIC model as **Multiscale Alpha Sharpen** (window title **Multiscale Sharpen Alpha**). |
| NAFNet | Nonlinear Activation Free Network, the image-restoration architecture family cited by DeepSNR. It does not document DeepSNR's private weights or training behavior. |
| NL-Bayes | Non-Local Bayes, a two-stage denoiser that groups similar nearby patches, estimates local Gaussian means/covariances, filters each group, and overlap-aggregates the estimates. |
| ONNX | Open Neural Network Exchange model format; current DeepSNR packages use ONNX model checkpoints on supported ONNX Runtime platforms. |
| Patch PCA | AfterNight's bundled local patch principal-component denoiser. It uses every position in a square lookup window, removes trailing eigen-components below `Strength × estimated noise`, and Gaussian-overlap-aggregates reconstructed patches; it does not similarity-rank the lookup patches. |
| Profiled star mask | A mono mask rasterized from a saved Star Profiler v1/v2 list. Profiled Starmask Recombination can use filled binary areas or one of six circular shaped profiles, combining overlaps by maximum. |
| Fuzziness | Range Selection's boundary-ramp fraction. For nonzero interval width `U-L`, each linear ramp has width `(U-L)×Fuzziness/2`; it is distinct from the later Gaussian Smoothness operation. When `U=L`, no ramp division executes and only the exact shared-limit value is selected. |
| Range mask | A mask built from an intensity interval. AfterNight Range Selection includes both endpoints and can output hard/fuzzy weights or source-weighted Screening values before optional smoothing and inversion. |
| Screening | In Range Selection, multiplication of the selection weight by the normalized source sample (`xw`). It is not a screen compositing blend mode. |
| Split Bregman | An iterative variable-splitting method for optimization problems with L1 terms. Total Variation Denoise's Bregman method uses AfterNight's own split-Bregman solver for isotropic or anisotropic ROF denoising; the Chambolle method is the alternative. |
| SOS denoising | Strengthen–Operate–Subtract, a repeated-denoiser scheme. NL-Bayes exposes pass count (up to eight) and Rho; its current implementation stops if the measured input noise rises by more than 1% and keeps the last converging denoised result. |
| Small-star protection mask | Star Reduction's derived grayscale mask made from a matched target/starless pair. It is attached as a normal non-inverted AfterNight mask: dark pixels retain the target, while bright pixels admit the reduced result. |
| Star mask | A mask designed to isolate stars or protect non-star structures. |
| Structuring element | The binary neighborhood, also called a kernel, whose active cells define the samples used by a morphological operation. |
| StarNet2 | Separately installed neural star-removal command-line tool that AfterNight's Starnet++ processes invoke. AfterNight requires CLI version 2.5.3 or newer and does not implement or bundle its model inference. |
| Starless image | An image with stars removed for separate processing of nebula/galaxy structure. |
| Starlet transform | An undecimated multiscale transform made by repeatedly convolving with a kernel and retaining detail differences plus a coarse residual. Multiscale Linear Transform offers dyadic or linear spacing and 15 normalized scaling kernels; Dark Structure Enhance uses fixed/selected native Linear or B3-spline starlet kernels. |
| Stride | Pixel spacing between neighboring processing windows. DeepSNR uses a fixed 512-pixel window, where smaller stride means more overlap/repeated inference. StarNet2 also accepts an even stride from 2 to 512, but its exact window blending is upstream-private. |
| Subtractive star mask | StarNet2's native optional `--mask` output. It is distinct from the native unscreen star layer; when AfterNight pre-stretches linear input, this mask does not receive the output pseudoinverse. |
| Structure tensor | A local matrix formed from image gradients whose eigenvectors/eigenvalues describe dominant structure directions and strengths. GRAYCstoration sums channel contributions for RGB input. |
| Total variation (TV) | The sum or integral of image-gradient magnitudes. TV denoising reduces this quantity while penalizing departure from the source, often producing piecewise-smooth or stair-stepped regions. |
| Unscreen star layer | StarNet2's native optional `--unscreen` stars output, separate from its subtractive mask. AfterNight pseudoinverses this layer when it pre-stretched a linear source. |
| Unsharp mask | A Gaussian residual sharpening model, `source + amount × (source - blur)`, with the result held to the image's full-scale range. Positive Amount sharpens, zero is identity, and negative values blur: Amount runs from -10 to 10, and -1 returns the Gaussian-blurred image. |
| Wavelet | A multiscale representation used for denoise, sharpening, or structure separation. Wavelet Denoise uses decimated Haar analysis; Multiscale Linear Transform uses a different undecimated model. |

### File Formats And Metadata

| Term | Meaning |
| --- | --- |
| `.anseq` | AfterNight's image-list sequence file: an XML manifest naming, in order, the separate image files that make up a sequence. |
| EXIF | Camera-oriented tags embedded in supported image formats. AfterNight's EXIF window lists every field the file states, in the file's order, with its tag name, the whole value, and the full key; it hides binary or internal blocks such as the maker note, the embedded thumbnail, the colour profile, and embedded XMP/IPTC. |
| FITS | Astronomy image format that can store pixel data, header keywords, WCS, and SIP metadata. |
| FFmpeg | Library used for video-backed sequence support. |
| LibRaw | Library used for camera RAW decoding. In release builds AfterNight's private RawSpeed build decodes the camera formats it is qualified for, and LibRaw handles the rest. |
| OpenFITS | Library used for FITS read/write support. |
| OpenXISF | Library used for XISF read/write support. |
| RAW | Camera vendor raw file decoded through LibRaw. |
| SER | Sequence format commonly used for planetary/lucky imaging. |
| Temporary sequence | A session-managed on-disk sequence produced by processing. It can back a new or replacement view but is not a durable export until explicitly saved or converted to a user-owned location. |
| TIFF | High-bit-depth interchange format useful for external editors. |
| XISF | Extensible Image Serialization Format, common in astrophotography processing workflows. |
| Sample type | How one sample is stored: AfterNight processes eight - unsigned and signed 8-, 16-, and 32-bit integers and 32- and 64-bit floating point - and opens, shows, converts, and saves all eight where the format supports them. A FITS file of 64-bit integers opens as 64-bit floating point, and one holding a stored integer beyond 2^53 in magnitude is refused rather than rounded; any other format's sample outside the eight is refused with `the file holds samples outside the eight this engine processes`. A sample type says how many bytes a sample takes, not what it means. |
| Numeric domain | What a sample's number means, stated with the image rather than guessed from its sample type: a fraction of full scale (linear), a fraction through a nonlinear encoding such as sRGB, a sensor code, or a physical quantity. Changing the sample type does not change the domain. |
| Sensor code | The number a camera reported, in its own units, before calibration says what it stands for. An integer FITS file and an undebayered camera RAW mosaic open as sensor codes. |
| Physical quantity | A value in stated units, such as electrons or a flux, held in floating point. A floating-point FITS file, and a floating-point XISF image that declares a range other than 0-1, open as physical quantities and are not rescaled. |
| Fraction of full scale | The reading the viewer and the processes give every sample: an unsigned integer divided by its largest value, a signed one counted from its smallest value to its largest, and a floating-point sample as it is, so 0 is black and 1 is white. TIFF, PNG, JPEG, a full-colour RAW frame, and an XISF image of the normal 0-1 range open as fractions; a sensor-code or physical-quantity image is read this way by the viewer and by a process, and a process that changes an image in place writes its result back in the image's own sample type, rounded to the nearest code. |
| Validity rule | Which samples of an image mean something, stated with the image and never inferred from its pixels: **all valid** (the default, and what an opened file is), **non-finite invalid** (a NaN or infinity means nothing, as a calibration division by a zero flat leaves), **exact zero no-data** (a compatibility convention used only where something states it), **explicit mask** (a one-bit-per-sample mask beside the image, such as the pixels a stack did not reach), or **closed range** (a sample outside a stated range means nothing). Measurements leave invalid samples out, processes leave them as they were, and the viewer shows a non-finite sample black or transparent. A saved file carries the sample values only; Image History snapshots keep the rule and the mask. |
| Processing lineage | The append-only list of operations an image went through, kept with the image and in Image History snapshots: each entry names the engine operation and its parameters, such as a PixelMath run's evaluation path and random seed or an external tool's program, version, and arguments. No image file format stores it, and **Embed processing history** writes nothing. |

### Extensions

| Term | Meaning |
| --- | --- |
| Extension host | Separate AfterNight process that runs extension Python code and native extension windows. |
| Lite host | Protocol-only extension host (`AfterNightExtensionHostLite`) for packages that use no direct engine bindings or native AfterNight controls; it loads no part of AfterNight's image engine. |
| Runtime backend | The `runtime` extension backend: a module `AfterNightExtensionHost` loads by name, giving Python direct engine bindings and owning the engine session the host's native windows borrow. |
| Protocol backend | Engine-free backend using protocol-safe app/view services through `AfterNightExtensionHostLite`. |
| Manifest | `extension.json`, the package file that declares identity, version, backend, entry point, processes, dependencies, and capabilities. |
| Package hash | SHA-256 digest used to verify repository extension packages before installation. |
| Settings schema | Extension-declared settings fields cached by the host and rendered in the Installed tab. |
| Extension environment | Managed Python environment selected by a package's private, shared-group, or shared-host policy and accepted for launch only when its immutable readiness receipt matches the current fingerprint. |
| Full hosted | `runtime` backend mode: the package runs in `AfterNightExtensionHost` with direct engine bindings and native AfterNight controls. |
| Lite hosted + RPC | `rpc` backend mode parsed and displayed by the manager but currently unavailable because its engine RPC sidecar is deferred. |
| Shared buffer | Host-owned image transport whose descriptor declares a role and read/write access; changing its bytes does not commit a canonical view update. |
| Signature state | Package trust classification shown as **Signed** (internal `Verified`), **Unsigned**, **Unknown**, or **Signature failed** (internal `Failed`), used with the package hash and trusted-key policy to decide whether installation is allowed. |
| Unsigned extension | Package explicitly classified **Unsigned**, not **Unknown** or **Signature failed**. Repository and local-archive installation of an Unsigned package requires the global unsigned opt-in plus a separate package confirmation; Unknown and failed signatures remain blocked. |

## How to use this reference

1. Search for the exact UI term, abbreviation, algorithm, file type, or state
   named on the page you are reading.
2. Read the complete definition, especially any AfterNight-specific boundary or
   “does not” clause.
3. Follow the authoritative page in **Related pages** or return to the process
   page for controls, equations, output, and troubleshooting.
4. When two terms seem interchangeable, compare their definitions before
   choosing a channel, color model, mask operation, or workflow stage.
5. Recheck the destination page when a definition says behavior is current or
   implementation-specific; menu labels and external runtimes can change.

## Implementation behavior and model

The glossary separates product state from reader guidance. For example,
**linear image** describes proportional scene data, while **recorded processing
state** describes what AfterNight currently remembers about that data. A process
can accept a linear image without enforcing the marker, and a marker can be
missing after an operation that dropped the image's cards. The colour
calibrations are the exception that enforces it: they refuse an image declared
nonlinear, and a person can attest that an imported image is linear.

It separates three facts about a sample that are easy to conflate. The
**sample type** says how a number is stored, the **numeric domain** says what
the number means (a fraction of full scale, a sensor code, or a physical
quantity), and the **validity rule** says whether the sample means anything at
all. None is inferred from another: an integer file is not assumed to be
linear, and a floating-point file is not assumed to lie between 0 and 1.

It also separates three independent classifications. **Registry category** is
internal descriptor metadata used for registration and lookup. **Process
category** names the generated user-visible menu submenu. **Workflow role** is
documentation guidance for when a destination is commonly used; it is not a
registry field. Likewise, **View Channel** and **display transfer** affect
rendering, whereas **processing channel**, Apply, and committed immediate
actions affect image data.

Algorithm definitions summarize the exact current model only where that
distinction helps predict behavior. Detailed equations, symbols, precision,
boundaries, upstream implementations, and citations stay on the authoritative
process/reference page.

## Inputs, outputs, persistence, and metadata

The glossary accepts no image/file input and creates no product output. It does
not change settings, image history, processing state, masks, metadata, or files.
The source inventory is authoring evidence only; the reader-facing definitions
remain useful offline with the bundled guide.

## Performance and resource implications

Glossary lookup is a static page-search/table-navigation task. It performs no
processing, network request, catalog query, or external-tool launch. Very long
definitions intentionally retain current boundaries so readers need not infer
behavior from an abbreviation alone.

## Worked lookup example

- Evidence mode: Source-and-test-derived.
- Question: “Is MTF `0.5` a stretch, and does Exposure Correction use exposure
  stops?”
- Lookup: **Midtones transfer function (MTF)** defines `0.5` as the identity
  midtones setting; smaller values brighten and larger values darken the
  unclipped interval. **Exposure stop** defines standard `2^s` scaling and
  explicitly records that the current Exposure Correction control does not use
  that mapping. The [Histogram Transformation](../processes/stretching/histogram-transformation.md)
  and [Exposure Correction](../processes/stretching/exposure-correction.md) pages
  provide the exact controls, equations, clipping, preview, and state behavior.
- Limitation: No image or GUI was executed; the conclusion is limited to the
  source/test-backed guide contract.

## Warnings and limitations

- A short definition cannot determine whether a process is safe for a specific
  dataset; read the destination page's prerequisites and warnings.
- Recorded linear/nonlinear state can be missing or stale when an operation
  drops an image's cards or a process does not state its result. Inspect the
  actual workflow, not only the marker.
- A sample type does not say what a sample means. A floating-point FITS file
  can hold values far above 1, and an integer file can hold linear counts or a
  stretched picture.
- Hue, lightness, luminance/luma, value, intensity, chroma, and saturation are
  model-specific components and must not be substituted by name alone.
- A current AfterNight implementation definition may be narrower than a general
  standard or paper. The destination page records that distinction and cites
  the primary source where material.
- Extension/runtime terms describe current architecture; installation, trust,
  permissions, and package lifecycle require the extension guide.

## Troubleshooting

| Symptom/question | Verified interpretation or cause | Resolution/next page | Evidence source |
| --- | --- | --- | --- |
| A process menu category seems out of workflow order | Menu placement and workflow role are different concepts | Use the [Process Catalog](../processes/index.md) timing matrix, then the destination page | registry/menu sources; completed pages |
| Preview and Apply differ | Preview can use a copied target, ROI, cache, or approximation; it runs with the parameters held when it was requested | Read [Process Windows](../interface/process-windows.md) and the process's preview limitations | process framework and destination page |
| “Linear” output looks inconsistent with prior work | The recorded marker may be preserved/stale or the data was already nonlinear | Review history and the process page's output/state contract | processing-state definitions and completed pages |
| A color component is not what its name suggested | HSL/HSV/HSI/CIE/YCbCr components have different models and encodings | Use the exact color-model definition and the source process page | color-conversion/process pages |
| A mask appears visible but has no processing effect | Show/rendering state is separate from mask enablement and blend polarity | Read [Masks](../interface/masks.md) and the **Mask** definition | mask source/tests and Masks page |
| An abbreviation is not present | It may be a page-local control, external product term, or unsupported concept | Search the full guide, then use the authoritative destination/reference page | guide navigation/source inventories |

## Related pages

- [Process Catalog](../processes/index.md) — all 77 process/tool destinations,
  exact route families, workflow roles, and linearity/timing guidance.
- [Process Windows](../interface/process-windows.md) — launch/target/channel,
  preview, Apply, mask, history, cancellation, and persistence concepts.
- [Image Viewer](../interface/image-viewer.md) — display channel, autostretch,
  overlays, selections, and display-versus-data behavior.
- [Masks](../interface/masks.md) — current mask relationship, blend polarity,
  rendering, persistence, and process interaction.
- [File Formats](file-formats.md) — authoritative precision, metadata,
  compression, sequence, and save/export terms.
- [Toolbar & Menus](../interface/toolbar-and-menus.md) — current menu/action,
  availability, recent/favorite, context, and shortcut contracts.
- [Troubleshooting And Diagnostics](troubleshooting.md) — symptom routing and
  the distinction between System Information, console text, file logs, workflow
  reports, and crash dumps.

## References and provenance

- `src/AfterNightControls/app/ProcessRegistry.h`,
  `src/AfterNightControls/app/ProcessRegistry.cpp`, and
  `src/AfterNightProcesses/app/ProcessRegistryBuiltins.cpp` establish launch
  modes, availability, guards, and internal registry categories.
- `src/AfterNightControls/app/AfterNightAppUiBuilder.h`,
  `src/AfterNightControls/app/MenuActionManager.cpp`,
  `src/AfterNightControls/app/ActionStateCoordinator.cpp`, and
  `src/AfterNightApp/app/AfterNightApp.cpp` establish exact built-in menu
  placement, action binding/state, and recent/favorite recording/persistence.
- `src/AfterNightEngine/include/AfterNightEngine/IO/ProcessingStateCards.h`,
  `src/AfterNightProcesses/processes/ProcessMetadataUtils.h`, and
  `src/AfterNightProcesses/processes/PointwiseProcessUtils.h` establish the
  processing-state cards, how linearity is read, and how a process records a
  nonlinear result; `src/AfterNightTests/Test_EngineImage/ProcessingStateCardsTest.cpp`
  holds the cards and their FITS/XISF round trips.
- `src/AfterNightEngine/include/AfterNightEngine/Image/SampleType.h`,
  `src/AfterNightEngine/include/AfterNightEngine/Image/ChannelSemantics.h`,
  `src/AfterNightEngine/include/AfterNightEngine/Image/ValidityDescriptor.h`,
  `src/AfterNightEngine/include/AfterNightEngine/Image/Provenance.h`, and
  `src/AfterNightEngine/include/AfterNightEngine/IO/FitsCodec.h` establish
  the eight sample types, the numeric domains, the validity rules, the lineage,
  and the 64-bit FITS reading; `src/AfterNightControls/widgets/ViewPresentation.cpp`
  and `src/AfterNightProcesses/processes/StretchProcessSupport.h` establish how
  the viewer and the processes read every domain as fractions of full scale.
- `src/AfterNightTests/Test_ProcessesShared/ProcessRegistryTest.cpp`,
  `src/AfterNightTests/Test_ProcessFramework/test_process_registry_rules.cpp`,
  `src/AfterNightTests/Test_AfterNightApp/ApplicationMenuTest.cpp`, and
  `src/AfterNightTests/Test_AfterNightApp/test_process_menu_state.cpp`
  establish the focused descriptor/menu/state evidence cited above.
- Completed Image Viewer, Process Windows, Masks, Image History, Session Console,
  and metadata pages establish current application concepts.
- [Using Extensions](../extensions/using-extensions.md) and
  [Extension Marketplace](../extensions/extension-marketplace.md), together with
  their recorded manager, host, environment, protocol, and focused-test sources,
  establish the synchronized extension terminology.
- Primary papers, standards, and upstream documentation remain cited on the
  specific process/reference page where they materially support a definition;
  this glossary does not duplicate those bibliographies.
- The complete glossary and diagnostic cross-reference were re-reviewed on
  2026-10-18, after their reconciliation on 2026-07-20; exact validation is
  retained in the guide's validation record.
