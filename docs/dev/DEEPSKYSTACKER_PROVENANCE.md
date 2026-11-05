# DeepSkyStacker Provenance

Source compared: 2026-11-04.

Two pieces of the engine are a derived work of DeepSkyStacker: the auto-adaptive weighted mean the sample-history
combination offers, and the six falloff shapes of a shaped star mask. Both are adapted, and both are distributed
under the GPL with DeepSkyStacker's BSD notice kept.

## Upstream Source

- Project: DeepSkyStacker, https://github.com/deepskystacker/DSS
- Read at: `4dc1c5c45b4bd7bf8d1a5830f1780ddafee1d43f`, the commit whose `LICENSE` the engine's
  `src/AfterNightEngine/THIRD_PARTY_NOTICES.md` cites. The first import into AfterNight did not record the revision
  it followed, so this commit identifies the notice, not a byte-identical copy; the routine and the shapes below are
  as this commit states them.
- Files:
  - `DeepSkyStackerKernel/DSSTools.h`, `AutoAdaptiveWeightedAverage`: written by David C. Partridge on 29 July 2019,
    with Michael A. Covington and Simon C. Smith, as its comment states, after pages 44 to 51 of Peter B. Stetson's
    "The Techniques of Least Squares and Stellar Photometry with CCDs" (1989)
  - `DeepSkyStackerKernel/StarMask.h` (the `CStarMaskFunction` shapes) and `DeepSkyStackerKernel/StarMask.cpp`
    (`CStarMaskEngine::CreateStarMask2`, the painting)
- Copyright lines, as the repository's `LICENSE` states them (no file of the two carries its own):
  - `Copyright (c) 2006-2019, LucCoiffier`
  - `Copyright (c) 2018-2023, David C. Partridge, Tony Cook, Mat Draper, Simon C. Smith, Vitali Pelenjow, Michal
    Schulz, Martin Toeltsch` (upstream breaks it over three lines)
- Licence: the three-clause BSD licence (BSD-3-Clause), compatible with the engine and with the product, which
  distribute the adapted work under the GPL with the notice kept, and not reached by the Apache-2.0
  extension-protocol boundary. The notice in full is in `src/AfterNightEngine/THIRD_PARTY_NOTICES.md`, and the About
  dialog's licence page reproduces it, as the second condition asks of a binary distribution.

## What Was Carried

**The auto-adaptive weighted mean.** The samples scaled by 65,535, their plain mean as the first estimate, the root
mean squared deviation from the running estimate, the weight `1 / (1 + (r / sigma)^2)` of a sample `r` away from it,
the re-weighted mean accumulated as `(v * w) / total`, the iteration - five times, DeepSkyStacker's default count, which it takes as a setting and the engine fixes - the escape to the plain mean when the first
deviation is five units or less, and the plain mean for fewer than three samples.

**The star-mask shapes.** The bell, `exp(-d^2 / (2 r^2))`; the bell scaled by `1 / exp(-0.5)` and clipped at one; the
cone `1 - d / (3 r)`; the cone scaled by one and a half and clipped; the cubic `1 - (d / (3 r))^3`; and the quartic
`1 - (d / (3 r))^4`, which DeepSkyStacker, like the engine, calls quadratic. The painting covers three radii about a
star's centre and keeps the largest contribution where stars overlap.

## What Changed

- The weighted mean reads only the samples the frames' validity rules admit, applies the sample scale to every
  column, and gives a column whose largest sample is not positive its robust mean, where the routine answers zero.
- A compiled row runs the same arithmetic on every lane at once and is held to the scalar row bit for bit.
- A star's radius is its larger full width rather than DeepSkyStacker's scaled mean radius, the coverage is Float32
  or Float64 in [0, 1] in caller storage rather than an eight-bit grey image, and the answer depends on neither the
  order of the list nor the thread count.
- The solid ellipse painting, with its stated boundary rule, is the engine's and has no DeepSkyStacker counterpart.

## Where the derived code lives

The weighted mean: `src/AfterNightEngine/src/kernels/reference/StackMethodRow.inl` (the scalar row),
`src/AfterNightEngine/src/backends/cpu/StackMethodCpu.cpp` (the compiled row) and the benchmark oracle
`src/AfterNightBench/common/StackingOracle.cpp`. The shapes:
`src/AfterNightEngine/include/AfterNightEngine/Algorithms/Stars/StarMask.h` and
`src/AfterNightEngine/src/algorithms/stars/StarMask.cpp`. Each carries the credit, both copyright lines, the licence
and a pointer to this note. `build/scripts/tools/dependencies.txt` records the `adapted` records
`deepskystacker-weighted-mean` and `deepskystacker-star-mask`, and `check_dependency_provenance.py` fails a file
that loses the copyright line `Copyright (c) 2006-2019, LucCoiffier`.
