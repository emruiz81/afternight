# Drizzle Corner Interpolation Provenance

Source snapshot reviewed: 2026-09-09.

The square drop of drizzle needs the destination positions of the four corners of each source pixel's footprint.
The engine does not map the corners through the transform: it interpolates them from the coordinate map's mapped
pixel centres, and the arithmetic of that interpolation is adapted from Siril's `interpolate_four_points`, which is
itself the single-precision form of the STScI drizzle library's routine of the same name.

## Upstream Source

- Routine: `interpolate_four_points`, Siril, `src/drizzle/cdrizzlemap.c`, https://siril.org,
  https://gitlab.com/free-astro/siril
- Its lineage: the routine of the same name in the STScI drizzle library, `src/cdrizzlemap.c`,
  https://github.com/spacetelescope/drizzle. Siril's file opens with the drizzle code's notice and says that the code
  as integrated into Siril is modified from the original AURA code by team free-astro. The STScI routine computes in
  double precision; Siril's in single precision, over its own map structure. Both have the same nine weights, the
  same four sums and the same tap order.
- Copyright line, as both files state it: `Copyright (C) 2011,2014 Association of Universities for Research in
  Astronomy (AURA)` (one line here; upstream wraps it after `Research in`)
- Licence: the drizzle code's notice, the three-clause BSD terms (BSD-3-Clause), in the STScI library and in the Siril
  file that carries the routine; Siril as a whole is distributed under GPL-3.0-or-later. Both are compatible with the
  engine and with the product, which distribute the adapted routine under the GPL with AURA's notice kept; the notice
  in full is in `src/AfterNightEngine/THIRD_PARTY_NOTICES.md`, and the About dialog's licence page reproduces it. The
  routine is not reached by the Apache-2.0 extension-protocol boundary.
- The copy the port was taken from: not recorded. Its documentation said it matches Siril's
  `interpolate_four_points()`, and it named no Siril version.
- Read again on 2026-11-04: Siril at `07d942814b1fc1052765cf18fa9c524b4e8a269c` (`1.4.0-rc1-724-g07d942814`),
  `src/drizzle/cdrizzlemap.c` git blob `357c3a869473bcc18ade5f6bc20f4b1c9c45bf8f`, last changed in `ce3f66379` on
  2026-05-14; and the STScI library's `src/cdrizzlemap.c` at its tag `v0.5` and at `main`
  (`cab6f0cf8eec92dfea7feee9eeed73629d2ba0a8`, 2026-10-28), whose `LICENSE.rst` states the same notice.

## What Was Carried

The interior form of the routine. For a square drop of half-size `h` around a source pixel, the mapped coordinates of
its 3x3 neighbourhood are weighted by `(1 - h)^2` at the centre, `(1 - h) * h` at the four edge-adjacent cells and
`h^2` at the four diagonal ones, and each corner is the sum of the four weighted taps of its quadrant. The weights
and the sums are Float32, added in the routine's tap order, and the corners are interpolations of the mapped
centres rather than the transform evaluated at each corner. Because the interior form narrows the half-size to
Float32 and shares its nine weighted taps, it can differ from four independent point interpolations by Float32
rounding.

## What Was Written Here

The checked map view the interpolation reads (`DrizzlePixmapView`); the check of the complete one-row halo before an
interior footprint is read; the border cells, where each corner is a checked bilinear interpolation over the cell
beside the source centre, clamped at the edge of the map and extrapolating at most one pixel; the rejection of every
non-finite tap, a zero-weighted one included; and the typed failures. `interpolateDrizzleCorners` returns the corners
as Float32 `DrizzleCoordinate` pairs in the order (-h,+h), (+h,+h), (+h,-h), (-h,-h), and
`interpolateDrizzleCornerRow` serves a row of footprints to the accumulation: it validates once per row, writes NaNs
and counts the footprint as unmapped where a tap is invalid, and checks cancellation every 4096 footprints.

## Where the derived code lives

`src/AfterNightEngine/src/algorithms/drizzle/DrizzlePixmap.cpp` - `footprintWeights` and `interiorFootprint` - whose
module comment carries the attribution, AURA's copyright line and both licences, and points to this note; its
published header, `src/AfterNightEngine/include/AfterNightEngine/Algorithms/Drizzle/DrizzlePixmap.h`, says the
footprint calculation is adapted from Siril's routine and the STScI one it comes from.
`build/scripts/tools/dependencies.txt` records the `adapted` record `drizzle-corner-interpolation`, and
`check_dependency_provenance.py` fails the file if it loses the upstream copyright line
`Copyright (C) 2011,2014 Association of Universities for Research in Astronomy (AURA)`.
