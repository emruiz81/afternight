# NBColorMapper Provenance

Source snapshot reviewed: 2026-05-20.

The native AfterNight NB Color Mapper process is a GPL-compatible native port
based on the Cosmic Photons NBColorMapper script behavior. It is implemented
with AfterNight Engine primitives and native Qt controls.

## Source Snapshot

- Local source path: `C:\Program Files\PixInsight\src\scripts\CosmicPhotons\NBColorMapper`
- Script: `NBColorMapper.js`
- Title: `NB Color Mapper`
- Version: 2.2
- Author/copyright: Mike Cranfield, 2023, 2024
- License: GNU General Public License version 3
- Signature metadata: `NBColorMapper.xsgn`, script id `NBColorMapper`,
  developer id: Mike Cranfield, timestamp `2024-12-17T00:22:29.584Z`

## SHA-256 Hashes

| File | SHA-256 |
|---|---|
| `NBColorMapper.js` | `3C811E4923566723BF3A98A707B02CED3303939B9EA32F1E8B057B24522CD5CE` |
| `NBColorMapper.xsgn` | `CAC5C29BF208086942C60A3DC69C9CAF8DE195CC72E3650A40C4D587219EAB86` |
| `lib/NBCMColorPicker.js` | `DF3585092D571747935149D4FC4C7C5082520E20DF65A959CE37C57CE3E3535A` |
| `lib/NBCMDialog.js` | `9FDDBF9677EC1146B9A89DB61187ED2C119FC9E99FF3BE429A14BDF17C5426A5` |
| `lib/NBCMEngine.js` | `7F17C60F6FB33C9C39B322A6BFE5719F6539C7F88A2AFEAF58AD34D6DA920F12` |
| `lib/NBCMHistogram.js` | `56997C74483F8C7B3C5ED6B2899E948463FBA73F6C585C3CF3D27AEAD14494CD` |
| `lib/NBCMLayer.js` | `DC6A14ACFA572EA8B9357686F59A387E25F21484B16964C649CC93787906E9B8` |
| `lib/NBCMNewPaletteDialog.js` | `F67C29B30EF0E1A0E914C65C6A19AC36CC776F9D1DF4AAF591F51C130926C24D` |
| `lib/NBCMNoticeDialog.js` | `BE1DEE4A3307D508E9E3B3BCC3F67855C87D40198E18041656BAE3019340C43B` |
| `lib/NBCMPaletteViewer.js` | `9CAAF38327FD057E9FD373A288270592A0EE8A1B667B40C32BAA31FA95CDF8B4` |
| `lib/NBCMPreview.js` | `6FE3A4CDADB80B13C0086516A45130EC588C948A82D5281212700F75543B1C3B` |
| `lib/NBCMSwatchIdDialog.js` | `9850A09C1B8346BF8605917BA70AEA24306BEE48C5A1F1E05D9787139D84CB9D` |
| `lib/NBCMUtilities.js` | `91728F8C28F8B2E0643FD398F34C07C297EA5C17372089B8661324EFCB9EDB0B` |

## Porting Boundary

The native implementation separates characterization-derived behavior from
directly translated GPL script logic:

- Characterization-derived behavior includes public controls, parameter names,
  validation expectations, output policy, and small numeric fixtures generated
  from independent analysis.
- Any directly translated nontrivial GPL script logic must remain in GPL
  AfterNight Engine or native-control code with a nearby attribution notice.
- PixInsight/PJSR/Pleiades source, APIs, UI assets, and generated resources are not copied into AfterNight.
  Script-owned bitmap assets from `lib/` are not reused; native controls must
  draw through AfterNight's theme system.

## Where the derived code lives

The mapping is `mapNarrowbandColor` in
`src/AfterNightEngine/include/AfterNightEngine/Algorithms/Color/NarrowbandColorMap.h`,
implemented in
`src/AfterNightEngine/src/algorithms/color/NarrowbandColorMap.cpp`. Its
formulas and constants are translated from the upstream script, and the layer
mapping fuses what is pointwise into one pass.

The window is `src/AfterNightProcesses/processes/NBColorMapperProcess.cpp`.

Both engine files carry the nearby notice the porting boundary asks for: Mike
Cranfield's copyright line, the GNU General Public License version 3, what of
the layer model is the script's and what is the engine's, and a pointer to this
note. The window's header credits the script, and the About dialog carries the
user-visible credit. `build/scripts/tools/dependencies.txt` records the
`adapted` record `cosmic-photons-nbcolormapper`, and
`check_dependency_provenance.py` fails either engine file that loses the
copyright line.