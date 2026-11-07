# Auto Gradient Removal Provenance

Source snapshot reviewed: 2026-07-09

## Upstream Source

- Local source path: `C:\Development\siril-scripts\processing\AutoGradientRemoval.py`
- Siril scripts repository commit: `ca21c1425b14eeaff2f94e315e683240e28b86ce`
- Repository local status at review: only `VeraLux/__pycache__/` was untracked
- File SHA-256: `828AF4429945DAD421E18A1E6AE26B4FB74EE6DDC55BD96D9A19E543CC5617D4`
- Script name: `AutoGradientRemoval`
- Script version: `1.0.0`
- Author: Cyril Richard, 2026
- File license declaration: `GPL-3.0-or-later`

## License Notes

The root `siril-scripts` repository license file currently says MIT, but the
script file itself declares `SPDX-License-Identifier: GPL-3.0-or-later`. The
file-level declaration is treated as controlling for this native port unless
upstream clarifies otherwise.

## Native Port Scope

The native AfterNight implementation preserves the script's exposed parameters,
default values, correction modes, robust rejection constants, multiscale
inpaint-lowpass model, simplified polynomial model, and per-channel processing
contract as closely as practical while using AfterNight Engine image containers
and native Qt process controls.

The implementation is a native C++ port based on the characterized numerical
contract and the reviewed Python source. The source files that implement the
behavior preserve Siril, AutoGradientRemoval, Cyril Richard, 2026, and
GPL-3.0-or-later attribution.

## Translation And Characterization Map

The engine's automatic background translates these upstream functions and
formulas directly, restated over the engine's image planes:

- `_box1d`, `_lowpass`, `_mad_sigma`, `_poly_basis`, and `_poly_fit`
- `_inpaint_lowpass`, including the estimator's explicit three-pass override of
  the helper's two-pass default
- `_structure_mask`, `estimate_background`, `_downsample`, and
  `_resize_bilinear`
- `correct_channel` and the per-channel dispatch in `correct_image`

Written independently for AfterNight: in the engine (`AutoBackground.h`), the
restatement over its planes, the validity rule resolved at the reduction, the
reported figures, the correction fused into the write and the threshold in the
image's own units; in the window, the Qt controls, the preview computed when its
button asks, the gradient model and protection mask opened beside the target,
and the refusal of a sequence.

`BackgroundModelTest` (`Test_EngineImageOps`) holds the automatic estimate, and
`MasksRecordTest` (`Test_ProcessesMasks`) holds the window to the
`auto-gradient-removal` behavior records.

## Where the derived code lives

The automatic background is `planAutoBackground`, `estimateImageAutoBackground`,
`removeImageAutoBackground` and `removeAndDrawImageAutoBackground` - the last
the estimate and the removal over one fit, which is the engine's own - in
`src/AfterNightEngine/include/AfterNightEngine/Algorithms/Background/AutoBackground.h`,
implemented in
`src/AfterNightEngine/src/algorithms/background/AutoBackground.cpp`. It departs
from the upstream script in one place: structure protection accumulates across
rounds, where the script recomputes it each round.

Both engine files carry Cyril Richard's copyright line, the GPL-3.0-or-later
licence, what is the script's and what is the engine's, and a pointer to this
note. `build/scripts/tools/dependencies.txt` records the `adapted` record
`siril-auto-gradient-removal`, and `check_dependency_provenance.py` fails either
file that loses the copyright line. The window,
`src/AfterNightProcesses/processes/AutoGradientRemovalProcess.cpp` and its
header, carries the attribution; the About dialog carries the user-visible
credit.