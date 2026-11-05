# Demosaic Provenance

Source snapshot reviewed: 2026-08-27.

Every demosaic method of the engine except bilinear, which is its own, is a derived work of RawTherapee's demosaic
code as librtprocess packages it: the gradient-weighted estimate ("Fast"), AMaZE, RCD, LMMSE, the luminance-only
adaptive homogeneity-directed estimate (AHD), VNG4, Markesteijn in both of its passes, and the fast X-Trans average.
The kernels keep the upstream arithmetic, changed where it was defective; the request, the tiling, the memory and
the validity around it are the engine's.

## Upstream Source

- Project: librtprocess, https://github.com/CarVac/librtprocess, the extraction of the raw processing code of
  RawTherapee, https://github.com/Beep6581/RawTherapee
- Snapshot: a source copy of librtprocess that carries no version marker; the upstream commit it was taken from was
  not recorded. The latest date its demosaic files state is `code dated: January 01, 2019` (`demosaic/xtransfast.cc`).
- Read again on 2026-11-04 at librtprocess `9a858270acb2096e2e403d932760ee688fcac425` (`0.12.0-6-g9a85827`, the
  copy Siril carries as a subproject): every credit, copyright line and licence statement below is as that commit
  states it, and none of the demosaic files has changed since 2021-11-10. The git blob identifiers there:

  | File | Blob | Last changed |
  |---|---|---|
  | `src/demosaic/bayerfast.cc` | `6d719c3249352ea6324b129d3e74c748d59a09bb` | 2020-09-22 |
  | `src/demosaic/amaze.cc` | `bd35ba04f15061ebe7e9b31fd3c28f914c33f92b` | 2021-11-10 |
  | `src/demosaic/rcd.cc` | `d9addf260c54e3c0b1b63eed46ae272c621056f8` | 2021-11-10 |
  | `src/demosaic/lmmse.cc` | `5b98457db10531dbaef7477228f680f976cae28c` | 2021-03-30 |
  | `src/demosaic/ahd.cc` | `6d7f544ba72520efed7432d8e16bcfb9f65800a4` | 2021-07-13 |
  | `src/demosaic/vng4.cc` | `28e039adfa5991cc8bff02169672976f18c41ce0` | 2019-06-16 |
  | `src/demosaic/markesteijn.cc` | `a70b3c2e7d1b1a8eb305d18734e8698554a0a75d` | 2021-11-10 |
  | `src/demosaic/xtransfast.cc` | `58832ed72861d4c5eaa3dcaeb8e030433df73f99` | 2019-01-15 |
  | `src/demosaic/border.cc` | `307b1cfb0e175e444eec0abad66900d6bfdc62a9` | 2019-06-15 |

- File hashes of the copy the port followed: not recorded
- Licence: every file below declares the GNU General Public License, "either version 3 of the License, or (at your
  option) any later version": GPL-3.0-or-later

| Upstream file | Method | Credit as the file states it |
|---|---|---|
| `demosaic/bayerfast.cc` | Fast | `copyright (c) 2008-2010  Emil Martinec <ejmartin@uchicago.edu>` |
| `demosaic/amaze.cc` | AMaZE | `copyright (c) 2008-2010  Emil Martinec <ejmartin@uchicago.edu>`, `optimized for speed by Ingo Weyrich` |
| `demosaic/rcd.cc` | RCD | `Copyright (c) 2017-2018 Luis Sanz Rodriguez (luis.sanz.rodriguez(at)gmail(dot)com) and Ingo Weyrich (heckflosse67@gmx.de)`, `Tiled version by Ingo Weyrich (heckflosse67@gmx.de)` |
| `demosaic/lmmse.cc` | LMMSE | `Copyright (c) 2004-2010 Gabor Horvath <hgabor@rawtherapee.com>`, `Adapted for RawTherapee - Jacques Desmis 04/2013`, `Adapted to RawTherapee by Jacques Desmis 3/2013`, `Improved speed and reduced memory consumption by Ingo Weyrich 2/2015` |
| `demosaic/ahd.cc` | Luminance-only AHD | `Copyright (c) 2018 Ingo Weyrich (heckflosse67@gmx.de)` |
| `demosaic/vng4.cc` | VNG4 | no copyright line; `optimized for speed by Ingo Weyrich` |
| `demosaic/markesteijn.cc` | Markesteijn | no copyright line; `Frank Markesteijn's algorithm for Fuji X-Trans sensors`, `adapted to RT by Ingo Weyrich 2014` |
| `demosaic/xtransfast.cc` | Fast X-Trans | no copyright line and no author |
| `demosaic/border.cc` | the border rule (`bayerborder_demosaic`, `xtransborder_demosaic`) | `Copyright (c) 2004-2010 Gabor Horvath <hgabor@rawtherapee.com>` |

The methods themselves, as those files cite them: RCD is release 2.3 of Luis Sanz Rodriguez's ratio-corrected
demosaicing (`Original code from https://github.com/LuisSR/RCD-Demosaicing`); LMMSE is L. Zhang and X. Wu's
directional linear minimum mean-square-error estimation (IEEE Transactions on Image Processing, 2005); and AHD is
based on the work of Keigo Hirakawa, Thomas Parks and Paul Lee. The Markesteijn method and VNG4 are of dcraw's
lineage.

## License Notes

GPL-3.0-or-later is compatible with the engine and with the product, and no demosaic code reaches the Apache-2.0
extension-protocol boundary. Inside `rcd.cc`, the comment that introduces the algorithm says
`Licensed under the GNU GPL version 3`; GPL-3.0 is compatible as well.

## What Was Carried, And What Changed

Each reference kernel's module comment states what it keeps, what changed and why; the changes are defects or
unstated assumptions of the original rather than preferences. In brief:

- **Fast**: the scalar dialect's arithmetic - weights from the reciprocal of a squared local gradient, red and blue
  as colour differences against green, the four-neighbour sum clamped at the clip point. The clip point is the
  plan's rather than a sixteen-bit full scale, each row finds its green sites from the mosaic so a tile may start on
  any column, and the weights are divided where the original's vector dialect approximates the reciprocal.
- **AMaZE**: the scalar dialect, pass for pass. The highlight thresholds are fractions of the caller's white level,
  halving and doubling are multiplications rather than additions to the exponent field, every tile reads only what
  was written for it, and the outer sixteen pixels are the family's shared border rule rather than mirrored padding.
- **RCD**: the arithmetic and the original's 194-sample tile stride, unchanged. The normalising scale is the caller's
  white level rather than 65536, all three planes start from the measured sample where the original reads an
  uninitialised one, and every tile starts from cleared scratch.
- **LMMSE**: the fifth-order directional estimates, the Gaussian low passes, the local variances and their
  inverse-uncertainty combination. The hidden gamma pass and the optional median post-passes are not carried, five
  bounded tile planes replace frame-sized buffers, and the variance floor scales with the stated white level.
- **Luminance-only AHD**: the directional candidates, the threshold and the three-by-three homogeneity agreement.
  The choice is made on perceptual lightness alone, and the method is named for that; the 65,536-entry lookup is
  replaced by domain-scaled arithmetic over bounded tile planes.
- **VNG4**: the four-colour first interpolation, its 64 gradient terms, the eight-direction threshold and the two
  green positions. Bounded tile planes rebuilt from a four-pixel halo replace the frame-sized working image, so no
  output depends on which worker ran the row before.
- **Markesteijn**: the structure, in one and three passes. A filter is a named colour rather than a table index, the
  perceptual space is computed rather than read from a table indexed by a sixteen-bit code, the camera matrix is the
  identity and the option that selects that space says so, and every pass reads only what the pass before it wrote.
- **Fast X-Trans**: the weights - the four edge-adjacent neighbours count double the corners - and a site keeping its
  own measurement. The sums are divided by the weight accumulated rather than by constants chosen per case, which
  differs from the original in the last bits at the non-solitary green sites.

- **The border rule**: the like-filtered average of the clipped three-by-three neighbourhood, the measured sample
  kept for its own colour, and the tabulated mosaic's weights - one half for an edge-adjacent neighbour and one
  quarter for a diagonal one - from `border.cc`. One kernel serves both mosaics, and a colour with no site in the
  neighbourhood falls back to the measured sample for that colour alone, where `xtransborder_demosaic` falls back
  for all three only at a green site.
- **The shared Bayer steps**: the fifth-order green estimate with its median limiter, which `lmmse.cc` and `ahd.cc`
  both use, and the luminance-only estimate's per-pixel steps, written once in `DemosaicBayerKernel.h`.

LMMSE, VNG4 and luminance-only AHD complete red and blue with the family's shared colour-difference reconstruction.

Written independently for AfterNight: the bilinear estimate; the request and its plan (`planDemosaic` and
`demosaicImage` in `Algorithms/Demosaic/Demosaic.h`, implemented under `src/AfterNightEngine/src/algorithms/demosaic/`),
with the white level and clip point the caller states; the tiles, run on the operation's workers with
worker-private scratch, every output pixel written by one tile so the result does not depend on the thread count;
the validity rule; and cancellation.

## Where the derived code lives

The reference kernels under `src/AfterNightEngine/src/kernels/reference/` - `DemosaicKernelsFast.cpp`,
`DemosaicKernelsAmaze.cpp`, `DemosaicKernelsRcd.cpp`, `DemosaicKernelsLmmse.cpp`, `DemosaicKernelsLuminanceAhd.cpp`,
`DemosaicKernelsVng4.cpp`, `DemosaicKernelsMarkesteijn.cpp` and `DemosaicKernelsXTransFast.cpp` - each carry the
credit, the licence, what changed and a pointer to this note, and so do the border rule in `DemosaicKernels.cpp` and
the shared Bayer steps in `DemosaicBayerKernel.h`. The compiled kernels under
`src/AfterNightEngine/src/backends/cpu/` - `DemosaicFastCpu.cpp`, `DemosaicAmazeCpu.cpp`, `DemosaicRcdCpu.cpp`,
`DemosaicLuminanceAhdCpu.cpp`, `DemosaicMarkesteijnOneCpu.cpp` and `DemosaicMarkesteijnThreeCpu.cpp` with
`DemosaicMarkesteijnCpuTarget.inl`, and `DemosaicXTransFastCpu.cpp` - spell passes of the same methods lane by lane,
and each carries its method's credit and licence too.

`build/scripts/tools/dependencies.txt` records the `adapted` record `demosaic-rawtherapee`, and
`check_dependency_provenance.py` fails a file it lists that loses the word `RawTherapee`.
