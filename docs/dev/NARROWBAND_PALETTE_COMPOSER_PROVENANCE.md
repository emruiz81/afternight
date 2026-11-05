# Narrowband Palette Composer Provenance

AfterNight's native Narrowband Palette Composer process is a C++/Qt implementation
based on the SetiAstro SASPro narrowband palette workflow. It uses
AfterNight Engine image primitives and native controls; Python runtime objects,
PyQt widgets, icons, and other SASPro UI assets are not copied into AfterNight.

## Reference Snapshot

- Local source repository: `C:\Development\setiastrosuitepro`
- Upstream: https://github.com/setiastro/setiastrosuitepro
- Main process source: upstream SetiAstro SASPro narrowband palette process source
- Stretch source: `src\setiastro\saspro\imageops\stretch.py`
- Package metadata: `pyproject.toml`
- Original author/project attribution: Franklin Marek, SetiAstro SASPro
- License in the referenced package metadata: GPL-3.0

## Snapshot Hashes

| File | SHA256 |
|---|---|
| Upstream narrowband palette process source | `FB4999B0F4E03AD60CD2FECE1A77BE4CDE29D2075CB63F7AF848EA5C9B61E987` |
| `stretch.py` | `DAA50EB3616588AB80BD598F2F86F1E0A1D94738448E17CA2466FC9A49F675D4` |
| `pyproject.toml` | `6A77EF2B7F420F32DC9B9621D77E8ADD19E672DC548E8C67F5B9D59254E56123` |

## Porting Policy

- Keep the SetiAstro palette formulas, OSC synthesis rules, missing-channel
  substitution behavior, output clipping, and output normalization under
  deterministic engine tests.
- Use AfterNight's unlinked autostretch for input preparation so previews and
  outputs match the application's standard linear-data stretch behavior.
- Attribute SetiAstro/Franklin Marek in user-visible license surfaces.
- Do not copy Python, PyQt, themed-button, image-loading, or generated assets
  from SASPro.

## Where the derived code lives

The palettes are `composeNarrowbandPalette` and `composeNarrowbandPaletteGrid`
in
`src/AfterNightEngine/include/AfterNightEngine/Algorithms/Color/NarrowbandPalette.h`,
implemented in
`src/AfterNightEngine/src/algorithms/color/NarrowbandPalette.cpp`, whose
palette table keeps the SetiAstro names. The twelve palettes are one bounded
operation, and their equations are the upstream palettes' equations, translated.

The window,
`src/AfterNightProcesses/processes/NarrowbandPaletteComposerProcess.cpp`,
prepares each input with each channel's own automatic screen transfer
(`autostretchFrame`). It composes the grid of every palette on the thread pool,
the latest request winning, and `NarrowbandPaletteComposerPreviewGridWidget`
shows it.

Both engine files carry the attribution - Seti Astro Suite Pro and Franklin
Marek, GPL-3.0, what of the palettes is SASPro's and what is the engine's, and a
pointer to this note - and the window's header credits SASPro. The About dialog
carries the user-visible credit. `build/scripts/tools/dependencies.txt` records
the `adapted` record `setiastro-narrowband-palettes`, and
`check_dependency_provenance.py` fails either engine file that loses the
author's name.