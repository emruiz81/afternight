---
kind: process
stage: calibrate
menu: Process > Color Spaces > Split CFA
summary: >-
  Opens the positional planes of a Bayer mosaic as separate grayscale images or sequences, including red, averaged
  green and Ha-oriented extractions.
data_in: cfa
---
# Split CFA

## Purpose and expected result

Split CFA opens new grayscale images or sequences derived directly from a Bayer
mosaic. It can expose all four photosite positions, extract the red photosite as
an Ha-oriented plane, average the two green photosites, or derive Ha and OIII
planes for a dual-band OSC workflow. The source is never modified.

The Ha/OIII methods are sensor/filter assumptions, not a spectroscopic
decomposition. They are meaningful only when red photosites primarily record Ha
and green/blue photosites primarily record OIII.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | **Process > Color Spaces > Split CFA**; the window's title is **Split CFA** followed by the target's name |
| Enabled when | An image or sequence view is active. The target selector lists only views shown as a one-channel mosaic; a target that does not state a two-by-two mosaic is refused when **OK** is pressed. |
| Inputs | One undebayered RGGB, BGGR, GRBG, or GBRG mono image/sequence, with the arrangement and phase its description states. An X-Trans mosaic and an image that states no filter array are refused by name. |
| Outputs | One, two, or four new grayscale views/sequences in the mosaic's sample type. Dimensions depend on Method/Resampling. Source remains open and unchanged. |
| Recommended image state | Calibrated linear CFA data before Debayer. Ha/OIII requires appropriate dual-band acquisition. |
| Preview support | None. **OK** executes immediately. |
| Sequence support | Supported; every frame is split and written into new sequences of the source's container, with progress and **Cancel**. |
| Mask support | Unsupported and ignored. |
| External dependencies | None. |

## Before you start

1. Confirm the file is still mono CFA data and its stated Bayer pattern is
   correct. The stated arrangement and phase decide plane names and the Ha/OIII
   positions.
2. Calibrate before splitting when possible. Splitting first changes geometry
   and removes CFA metadata from the outputs.
3. For Ha/OIII, confirm the filter's red leak/cross-talk and sensor response are
   acceptable. This process performs no continuum subtraction or spectral fit.
4. Plan to plate-solve an output again if astrometry matters: no output states
   the source's solution, because its pixel grid is no longer the mosaic's. A
   positional plane keeps the mosaic's other header cards, but drops its
   world-coordinate cards and the solver's `PLTSOLVR` card with the solution.

## Procedure

1. Select a CFA mono view and open **Process > Color Spaces > Split CFA**.
2. Verify **Target Image** and choose a **Method**.
3. For **Extract Ha/OIII**, choose whether to keep native unequal dimensions,
   upsample Ha, or downsample OIII.
4. Click **OK**. The window closes after success; sequence work shows progress.
5. Verify output names, dimensions, and sample values.
6. Use [Merge CFA](merge-cfa.md) only with four unmodified, matching positional
   planes in CFA0–CFA3 order. Ha/Green/OIII products are not lossless merge inputs.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Method | Selector | **Split CFA Channels** | None | **Split CFA Channels**, **Extract Ha**, **Extract Ha/OIII**, **Extract Green** | Chooses four positional planes, the red photosite plane, derived Ha+OIII, or the mean of both green photosites. | Enables Ha/OIII Resampling only for **Extract Ha/OIII**. | Four-plane and Ha/OIII sequence modes write more sequences; derived modes are not reversible. | Execute only; still/sequence; no mask | Window lifetime; reopening restores default. |
| Ha/OIII Resampling | Selector | **No Resampling** | None | **No Resampling**, **Upsample Ha**, **Downsample OIII** | Keeps Ha at half width/height and OIII full size; or resamples one by a factor of two with the engine's Lanczos filter to match the other. | Enabled only for **Extract Ha/OIII**. | Upsampling invents samples and costs memory; downsampling discards OIII resolution; Lanczos can ring near sharp transitions. | Ha/OIII outputs only; still/sequence | Window lifetime. |
| Target Image | CFA mono image/sequence selector | Launching view | None | Open views shown as a one-channel mosaic | Chooses the source without modifying it. | Only compatible views listed; a target that no longer states a two-by-two mosaic is refused when **OK** is pressed. | Sequence length controls runtime/disk. | Execute only | Current window only. |
| OK | Button | — | None | Valid target | Checks the target's mosaic, reads the dialog, runs the extraction on a worker, opens the output view(s), and closes after success. | Disabled while running. | Output count is 1/2/4; sequences write that many new sequences. | Still/sequence; no preview/mask | No persistence. |
| Close | Button | — | None | Click action | Closes without output. | Disabled during execution. A window closed during a run stays hidden until it lands. | No source change. | Process window | No persistence. |

## How the algorithm works

### Conceptual pipeline

1. Each 2×2 Bayer cell is indexed by position, independent of pattern label:
   CFA0 `(even x, even y)`, CFA1 `(odd x, even y)`, CFA2 `(even x, odd y)`,
   CFA3 `(odd x, odd y)`.
2. The mosaic's stated arrangement and phase map those four positions to Red,
   Green1, Green2, and Blue; the greens are numbered in plane order.
3. The chosen method copies positional samples or derives Ha/Green/OIII through
   the engine.
4. Each output is a new image in the mosaic's sample type. Sequence frames run
   the same operation and are written into new sequences.

### Model and equations

The colour at each position under the four arrangements at zero phase is:

| Pattern | CFA0 | CFA1 | CFA2 | CFA3 |
| --- | --- | --- | --- | --- |
| RGGB | Red | Green1 | Green2 | Blue |
| BGGR | Blue | Green1 | Green2 | Red |
| GRBG | Green1 | Red | Blue | Green2 |
| GBRG | Green1 | Blue | Red | Green2 |

A mosaic cropped at an odd offset states a shifted phase, and the names follow
it: an RGGB mosaic cropped one column in is read as GRBG and its planes are
named Green1, Red, Blue, Green2.

**Split CFA Channels** copies each position into a half-width, half-height plane.
For odd source dimensions, the last column and/or row is discarded. Because the
operation is pure copying, those four planes can reconstruct the retained even
rectangle exactly when their order/type/dimensions are preserved.

**Extract Ha** copies the red position, as the stated phase places it, from each
2×2 cell, also at half width and height, in the mosaic's sample type. **Extract
Green** computes

\[
G=\frac{G_1+G_2}{2}
\]

for each cell in normalized sample space; an integer result is rounded to the
nearest code, ties to even.

**Extract Ha/OIII** works in these units: an unsigned integer as a fraction of
its largest value, a signed one counted from its
smallest value, a floating sample as it is, in 32-bit float, or 64-bit float for
a 64-bit float mosaic and for 32-bit integers. Odd dimensions are cropped to an even top-left rectangle. Ha
copies the red position into a half-resolution plane. For OIII, the global means
of the finite Green1, Green2 and Blue samples are `μ₁`, `μ₂`, `μ_B`; their shared
target mean is

\[
\bar\mu=(\mu_1+\mu_2+\mu_B)/3.
\]

Each non-red photosite is multiplied by `μ̄/μᵢ`; a position whose mean is
within double-precision epsilon of zero is left unscaled. Each red position is
filled from the equalized OIII samples among its eight neighbours, the four
axial ones weighted by √2 and the four diagonal ones by 1. A non-finite sample
counts toward neither a mean nor a fill and is written as zero. Optional
matching resamples one product by a factor of two with a separable,
eight-tap Lanczos filter (`sinc(d) sinc(d/4)`, `|d| < 4`) on half-pixel centres,
reading the nearest edge sample beyond the picture. Both products are then
written back in the mosaic's sample type in the same units they were read in -
an unsigned integer as a fraction of its largest value, a signed one counted
from its smallest value - an integer to the nearest code.

### Boundary and failure behavior

- Every method reads only complete 2×2 blocks; an odd last row/column is not
  used.
- The target is checked when **OK** is pressed. An image that states no filter
  array is refused with `the image states no colour filter array`, an image of
  more than one channel with `a mosaic has one channel`, and an X-Trans mosaic
  with `the XTrans filter array is not a two-by-two arrangement, which is all this
  process takes apart`, each under **Error running Split CFA**. There is no
  run-anyway question.
- **Split CFA Channels** refuses a mosaic whose valid samples are stated in a
  mask with `an image whose valid samples are stated in a mask cannot be split`.
  **Extract Ha**, **Extract Green** and **Extract Ha/OIII** refuse, before any
  work, a mosaic that states any sample invalid - a validity rule or a mask -
  with `an image that states some of its samples invalid cannot have a
  narrowband channel extracted`. Ha/OIII refuses a mosaic in which a green or
  blue position holds no finite sample.
- The Lanczos result is not clamped: beside a sharp edge a floating product can
  fall slightly outside 0–1, and an integer product saturates at its range.
- A failure is reported in a message titled **Error running Split CFA** that
  begins `An error occurred when executing the Split CFA process:` followed by the
  reason, and in the log. No output view is opened.
- A sequence another run holds, or a still image whose edit is still being
  written, shows the busy warning. **Cancel** on a
  sequence's progress window stops the run quietly; nothing is opened and the
  files it was writing are removed.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| Pattern metadata + Split | Verify pattern from acquisition metadata; use four planes first | Labels/colors do not match sensor positions | Wrong pattern silently relabels derived channels | Verify metadata -> inspect CFA0–3 -> derive channels |
| Ha/OIII + Resampling | No Resampling for diagnosis | Dimensions prevent later comparison/composition | Upsampling looks sharper without adding real resolution; downsampling loses detail | Inspect native -> choose one target geometry -> resample once |
| Ha/OIII + filter assumptions | Use only with reviewed dual-band response | Cross-talk/continuum remains | Derived planes are mistaken for spectrally pure masters | Verify filter/sensor -> inspect native planes -> compare externally if needed |

## Inputs, outputs, and metadata

| Method | Outputs | Native dimensions for source `W×H` | Names |
| --- | --- | --- | --- |
| Split CFA Channels | 4 grayscale images/sequences | `floor(W/2) × floor(H/2)` each | `<target> - CFA0 - <color>` through CFA3 |
| Extract Ha | 1 grayscale image/sequence | `floor(W/2) × floor(H/2)` | `<target> - Ha` |
| Extract Green | 1 grayscale image/sequence | `floor(W/2) × floor(H/2)` | `<target> - Green` |
| Extract Ha/OIII, No Resampling | 2 grayscale images/sequences | Ha `floor(W/2)×floor(H/2)`; OIII even-cropped `W×H` | `<target> - Ha`; `<target> - Oiii` |
| Extract Ha/OIII, Upsample Ha | 2 matching full/even size | Ha resampled to OIII | Same |
| Extract Ha/OIII, Downsample OIII | 2 matching half size | OIII resampled to Ha | Same |

Each output's history begins `Split CFA: <what> extracted from RAW image
'<target>'`, where `<what>` is `CFA0` to `CFA3`, `Ha`, `Green` or `OIII`; a
sequence's says `RAW sequence`. Each new view opens beside the source.

Every output keeps the mosaic's sample type, any of the eight AfterNight stores.
A positional plane keeps the mosaic's acquisition record, meaning and header
cards, states no filter array, and states no plate solution; its lineage names
the engine's block split and the plane. The solution goes with the cards that
state it: the mosaic's world-coordinate cards and the solver's `PLTSOLVR` card
describe the mosaic's grid and not the half-size plane's, so a plane carries
none of them; plate-solve the plane again if you need a solution. An Ha, Green
or OIII product keeps the
acquisition record, drops the plate solution and the header cards, withdraws any
decision about how it is written, and is marked as a resolved measurement
(demosaiced). The source remains unchanged.

A sequence's frames are read as recorded, every frame of the sequence, and each
output sequence is written in the source's container in AfterNight's temporary
directory, published only when every frame is written. Save or export it when a
durable file is wanted.

## Performance

Simple split/Ha/Green work is linear in pixels and output count. Ha/OIII adds a
floating working copy of the mosaic, two global passes, the red-site fill and,
when asked, a 64-bit float working plane for the Lanczos resampling; a sequence
computes the pair once per frame. Test one still before a long sequence and
budget disk for up to four new sequences.

## Worked example

- Input condition/provenance and settings: a small 16-bit RGGB mosaic in a view
  named `target`, split with each method; the same mosaic cropped one column in;
  a 32-bit float grey image with no filter array.
- Expected checkpoint: four planes named `target - CFA0 - Red`,
  `target - CFA1 - Green1`, `target - CFA2 - Green2`, `target - CFA3 - Blue`; after
  the crop, `Green1`, `Red`, `Blue`, `Green2`. The grey image is refused under
  **Error running Split CFA** naming the missing filter array, and nothing opens.
- Inspect at 100%/statistics/history: verify positional values, dimensions and
  history; a plane keeps the mosaic's `OBJECT` card, states no solution and
  carries no world-coordinate or `PLTSOLVR` card, an
  Ha product is marked demosaiced.
- Limitations: tests do not validate dual-band spectral quality.

## Warnings and limitations

- There is no preview, mask, channel selector, or Reset.
- Wrong Bayer metadata produces wrong labels and derived channels.
- Ha/OIII is a dual-band heuristic, not continuum subtraction or spectroscopy.
- No output states a plate solution or carries the cards of one; solve the
  derived image again if needed.
- Odd dimensions lose the final row/column. Resampling uses Lanczos and can ring.
- Split/derived outputs are new views; only the four untouched positional planes
  are losslessly mergeable back to the retained even rectangle.
- X-Trans mosaics cannot be split. Demosaic one with Debayer under **From
  metadata**, or open the RAW file with automatic debayer, which demosaics it by
  the X-Trans counterpart of the RAW setting's method, instead.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Split CFA target absent | View is not shown as a one-channel mosaic. | Open undebayered data and correct CFA metadata. |
| `the image states no colour filter array` | The image states no Bayer pattern. | Reopen with correct RAW/CFA metadata; do not guess if the pattern is unknown. |
| `the XTrans filter array is not a two-by-two arrangement...` | The mosaic is X-Trans. | Split CFA takes apart only a two-by-two mosaic. Use Debayer with **From metadata**, which demosaics an X-Trans mosaic by the chosen method's X-Trans counterpart. |
| `an image whose valid samples are stated in a mask cannot be split` | The mosaic carries a validity mask. | Use a mosaic without one. |
| `... cannot have a narrowband channel extracted` | **Extract Ha**, **Extract Green** or **Extract Ha/OIII** on a mosaic that states some sample invalid, by a validity rule or a mask. | Use a mosaic that states every sample valid. |
| Ha/OIII Resampling is disabled | Method is not **Extract Ha/OIII**. | Select that method if the acquisition supports it. |
| Output loses last row/column | Source dimension is odd. | Crop intentionally to even dimensions before splitting and account for the offset. |
| Output has no astrometric solution | No output states a solution or keeps the mosaic's world-coordinate cards, which describe the mosaic's grid. | Plate-solve the derived output. |
| The busy warning | Another run holds the sequence. | Wait for it to land and run again. |

## Related workflow steps

Run [Image Calibration](image-calibration.md) before splitting. Use
[CFA Integer Resample](cfa-integer-resample.md) when you need a CFA-preserving
integer geometry change, [Debayer](debayer.md) for full-color reconstruction,
and [Merge CFA](merge-cfa.md) only for the four positional planes. Derived Ha/OIII
masters can feed narrowband composition only after their assumptions and geometry
are validated.
