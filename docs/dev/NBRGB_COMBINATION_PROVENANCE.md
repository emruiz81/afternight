# NBRGB Combination Provenance

The native NBRGB Combination process is an original AfterNight implementation
distributed under AfterNight's GPL-3.0-or-later licensing.

Implementation sources:

- AfterNight Engine image storage, metadata, cropping, and pixel conversion
  primitives.
- `combineNarrowbandRgb`, whose continuum correction, boost and least-squares
  fit are AfterNight's own.
- Newly written C++ continuum correction, narrowband injection, validation,
  process UI, and registry/menu glue.

No Cosmic Photons, PixInsight/PJSR, or Pleiades source code, comments, UI
layout text, helper implementations, or license-bound examples were copied,
translated, or paraphrased into this implementation. No GPL-compatible Cosmic
Photons NBRGB source with a usable version, URL, hash and licence grant was
identified, so there is no third-party NBRGB source to record as copied or
ported provenance.

## Where the code lives

The window, `src/AfterNightProcesses/processes/NBRGBCombinationProcess.cpp`,
combines through `combineNarrowbandRgb`
(`src/AfterNightEngine/include/AfterNightEngine/Algorithms/Color/NarrowbandRgbCombination.h`,
implemented in
`src/AfterNightEngine/src/algorithms/color/NarrowbandRgbCombination.cpp`). The About dialog's NBRGB
note says the same, and `VersionDialogsTest` in `Test_ControlsDialogs` holds it
to that.
