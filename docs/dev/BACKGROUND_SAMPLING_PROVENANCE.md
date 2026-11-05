# Background Sampling Provenance

Source compared: 2026-11-04.

Two parts of the engine's background model are a derived work of Siril's background extraction: how a grid of sample
sites is measured and which sites are kept, and the thin-plate spline's kernel and smoothing. The polynomial fit, the
solvers, the evaluation lattice and the correction fused into the write are the engine's.

## Upstream Source

- Project: Siril, https://siril.org, https://gitlab.com/free-astro/siril
- File: `src/algos/background_extraction.c` (`generate_samples`, `get_sample`, `computeBackground_RBF`), with
  `SAMPLE_SIZE` from `src/algos/background_extraction.h`
- Compared at: `07d942814b1fc1052765cf18fa9c524b4e8a269c` (`1.4.0-rc1-724-g07d942814`), git blob
  `0da12bd5988aefe7488d621c0a49e2e56a8f4c50`, and at `ef048278814067d24178f01067870d8896d9633d`
  (`1.4.0-rc1-431-gef0482788`), the revision the user guide's Background Extraction page reviewed. The carried parts
  are the same at both.
- The copy the port was written against: not recorded.
- Copyright lines, as the file states them:
  - `Copyright (C) 2005-2011 Francois Meyer (dulle at free.fr)`
  - `Copyright (C) 2012-2026 team free-astro (see more in AUTHORS file)`
- Licence: the GNU General Public License, "either version 3 of the License, or (at your option) any later version":
  GPL-3.0-or-later, compatible with the engine and with the product, and not reached by the Apache-2.0
  extension-protocol boundary

## What Was Carried

- The site: a square box of twenty-five pixels a side by default (`SAMPLE_SIZE`), measured by its median.
- The acceptance: a site is kept while its level is at or below a median plus the tolerance times the median absolute
  deviation of the sites' levels from that median.
- The thin-plate kernel: half the squared distance times its logarithm, zero at the origin.
- The smoothing: `1e-4 * 10^(3 * (smoothing - 0.5))` times the mean row sum of the kernel matrix, added to its
  diagonal, with the smoothing at 0.5 by default.

## What Changed

- The acceptance statistics are taken over the sites' own luminances; Siril takes the median of the whole picture
  and the median absolute deviation of the sites' levels from it.
- A site reads only the samples the frame's validity rule admits, and every channel's median is exact.
- The sites are measured in parallel and kept in grid order, so the answer does not depend on the thread count.
- The spline carries the constant and the two linear terms that span the kernel's null space; Siril's carries the
  constant alone. The origin's cut-off is the engine's own, a squared distance of 1e-12 rather than 1e-5.
- The system is solved by elimination with partial pivoting in Float64 on the calling thread, and a refusal is a
  typed failure.

## Where the derived code lives

`src/AfterNightEngine/src/algorithms/background/BackgroundSampling.cpp` (the sites and their acceptance),
`src/AfterNightEngine/src/algorithms/background/BackgroundModel.cpp` (the spline's system and its smoothing) and
`src/AfterNightEngine/src/algorithms/background/BackgroundInternal.cpp` (the kernel), each of whose module comments
carries the credit, both copyright lines, the licence and a pointer to this note. `build/scripts/tools/dependencies.txt`
records the `adapted` record `siril-background-sampling`, and `check_dependency_provenance.py` fails a file that
loses the copyright line `Copyright (C) 2005-2011 Francois Meyer`. The About dialog's licence page credits the work,
and the Background Extraction page of the user guide names it in its references.
