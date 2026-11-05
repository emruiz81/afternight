# Star Detection Provenance

Source compared: 2026-11-04.

The engine's star detector - the candidate scan over a smoothed plane, the checks a candidate passes and the fit that
measures it - is a derived work of Siril's star finder and of its point-spread-function fit. The banded scan, the
order it keeps whatever the thread count, the validity rule, the counted rejection reasons, the dispatched evaluation
and the typed failures are the engine's.

## Upstream Source

- Project: Siril, https://siril.org, https://gitlab.com/free-astro/siril
- Files: `src/algos/star_finder.c` (`peaker`, `reject_star`, `candidate_is_duplicate`) and `src/algos/PSF.c` (the
  initial guess, the Gaussian and Moffat models and the fit), with the constants of `src/algos/PSF.h`
- Compared at: `07d942814b1fc1052765cf18fa9c524b4e8a269c` (`1.4.0-rc1-724-g07d942814`, `master` on 2026-09-25);
  git blobs `9418eb5ad908277f64cdbd1b1a6804f4e688df7b` (`star_finder.c`) and
  `d9408ecc2f9e0c852783add979458620472ebd6d` (`PSF.c`)
- The copy the port was written against: not recorded. The comparison above found every method and constant listed
  below unchanged at that commit.
- Copyright lines, the same in both files:
  - `Copyright (C) 2005-2011 Francois Meyer (dulle at free.fr)`
  - `Copyright (C) 2012-2026 team free-astro (see more in AUTHORS file)`
- Licence: the GNU General Public License, "either version 3 of the License, or (at your option) any later version",
  in both file headers: GPL-3.0-or-later, compatible with the engine and with the product, and not reached by the
  Apache-2.0 extension-protocol boundary
- Lineage, as `peaker`'s own comment states it: a refinement of Kenneth J. Mighell's 1998 peak finder and of Peter
  Stetson's DAOFIND (1987)

## What Was Carried

From `star_finder.c`:

- the scan: a sample above the threshold that no neighbour in the search box exceeds, with the tie rule that a
  neighbour of equal value above or to the left of it wins;
- the support: at least three (grey) or eight (colour) samples of the unsmoothed three-by-three neighbourhood at or
  above the threshold, and their mean as the candidate's brightness;
- the saturated plateau: the fractions of the dynamic range above which a star is taken to saturate (0.7) and within
  which its neighbours form a plateau (0.1), and the walk around the plateau's edge that re-centres the candidate;
- the box: the zero crossings of the smoothed second derivative along both axes, the width and amplitude estimates
  taken from them with the square root of e, and the box radius cut where the energy density falls to 0.001;
- the rejections: a candidate whose width or amplitude estimates differ by more than a factor of two between its two
  sides, one within a fifth of a box radius of an earlier one, and a fitted star whose residual exceeds a fifth of
  its amplitude unless it is saturated.

From `PSF.c`:

- the starting point, from the trace of the pixels above half the box's central peak walked outwards from the centre,
  and from that trace's zeroth, first and second moments for the centroid, the two widths and the angle;
- the Gaussian and Moffat models with a rotation, and their bounded parameterisation: the axis ratio as
  `0.5 * (cos(p) + 1)` and a free Moffat exponent as `10 * 0.5 * (cos(p) + 1)`, the ceiling of ten included.

Siril fits through the GNU Scientific Library's nonlinear least-squares driver with the Levenberg-Marquardt trust
region. The engine follows that method - the scaled normal equations, Nielsen's damping update, the "more" diagonal
scaling, a step accepted on the ratio of actual to predicted reduction and a stop after fifteen consecutive
rejections - in its own solver, which accumulates the normal equations without forming a Jacobian and allocates
nothing per star; no GNU Scientific Library code is carried.

## What Changed

- The scan runs in bands of rows and merges them in scan order before duplicates are rejected, so the candidate list
  is the one a single-threaded scan would produce.
- A sample the frame's validity rule rejects is replaced by the measured sky before the plane is smoothed, so a
  no-data border is not a step in the smoothed surface.
- The smoothing sigma, the search radius and the thresholds are a stated request rather than settings read from the
  application, and every rejection is counted under its reason.
- The model's evaluation is a dispatched kernel with a compiled form, `PsfFitCpu.cpp`, held to the scalar text at the
  distance of the exponential.

## Where the derived code lives

`src/AfterNightEngine/src/algorithms/stars/StarDetection.cpp` (the scan and the rejections),
`src/AfterNightEngine/src/algorithms/stars/PsfFit.cpp` (the starting point, the models and the fit) and
`src/AfterNightEngine/src/backends/cpu/PsfFitCpu.cpp` (the compiled evaluation of the models), each of whose module
comments carries the credit, both copyright lines, the licence and a pointer to this note.
`build/scripts/tools/dependencies.txt` records the `adapted` record `siril-star-detection`, and
`check_dependency_provenance.py` fails a file that loses the copyright line `Copyright (C) 2005-2011 Francois Meyer`.
The About dialog's licence page credits the work, and the Star Profiler and Star Mask pages of the user guide name it
in their references.
