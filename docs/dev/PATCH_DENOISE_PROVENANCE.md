# Patch Denoise Provenance

Source snapshot reviewed: 2026-09-01.

Patch denoising has two stages with two provenances. The two-pass patch estimator is adapted from Marc Lebrun's
NL-Bayes implementation published on IPOL, under GPL-3.0-or-later. The guided dual-domain shrinkage that finishes it
is written from the published DA3D method alone: the DA3D implementation that was audited carries no licence text,
so none of it is here.

## NL-Bayes

### Upstream Source

- Work: "Implementation of the Non-Local Bayes (NL-Bayes) Image Denoising Algorithm", IPOL,
  https://www.ipol.im/pub/art/2013/16/
- Author: Marc Lebrun
- Files audited: `NlBayes.h`, `NlBayes.cpp`, `LibMatrix.h`, `LibMatrix.cpp`, `LibImages.h`, `LibImages.cpp`,
  `Utilities.h` and `Utilities.cpp`
- Copyright line, the same in each of them: `Copyright (c) 2013, Marc Lebrun <marc.lebrun.ik@gmail.com>`
- Licence: the GNU General Public License, "either version 3 of the License, or (at your option) any later version",
  in every file header: GPL-3.0-or-later, compatible with the engine and with the product, and not reached by the
  Apache-2.0 extension-protocol boundary
- Version of the copy the port followed, and its file hashes: not recorded
- Read again on 2026-11-04 from the archive the article page publishes, `nl-bayes_20130617.tar.gz`
  (SHA-256 `3d95804054b6e7e7d94708fae9929d33839566d1aea78986ae178014a390357b`; the page also keeps two earlier
  revisions of it, dated 2021-02-05 and 2021-02-20). The copyright line and the licence above are as its files state
  them; the SHA-256 of the files audited:

  | File | SHA-256 |
  |---|---|
  | `NlBayes/NlBayes.h` | `66ad66db53ef777a9ba2e43aec7f9253a1fd4efa469d52dc6d839bd8ba805a6b` |
  | `NlBayes/NlBayes.cpp` | `a369e1db18e987f7197de439c69d5893c5e38b4d4cc6a060f093b69e29211a41` |
  | `NlBayes/LibMatrix.h` | `26a3594e1c039e9cf2f26678eb964ae8be4681cc8ef8b7fc7f4c07c858b3e249` |
  | `NlBayes/LibMatrix.cpp` | `8e1479ce4009b0b561ae75376b363b9b06f4fb40a322b9b07f409754e3b645b7` |
  | `Utilities/LibImages.h` | `86ec097cbf0e403328eca615b1a17d4d9ae2ff42c9bff59e89e13cc0f4707c18` |
  | `Utilities/LibImages.cpp` | `f40f2a994a4ca90493464785508be3eb8295009dd213976f48729f6621476644` |
  | `Utilities/Utilities.h` | `8bb53585fb7693797cc7273a82b42342523ff9cb477458bf077a8249f2fdd813` |
  | `Utilities/Utilities.cpp` | `0f75405903440265fb8f68efbcc47a22379d2ceccb3895f0bc7d6e1932b67ed2` |

### What Was Carried

The mathematics, value for value: the parameter tables that map a noise level onto a patch size, a number of
similar patches and a covariance weight (`PatchDenoisePlan::passParameters`); the colour decorrelation; the
similarity search; the featureless-group shortcut; the barycentre-centred covariance; the Cholesky inverse; the
paste trick; and the aggregation. The tables are written in the code values of an eight-bit image and are part of
the method, so the engine scales the frame onto them rather than rescaling them.

### What Was Not Carried, And What Changed

None of the scaffolding around that arithmetic is here: not the per-group vectors and sub-image copies, the index
arithmetic over a flat channel-major buffer, the exit codes, or the research driver's reporting - a file writer and a
console printer. Four behaviours change deliberately:

- ties in the similarity order are broken by position, a strict total order where a partial sort leaves the order
  unspecified, so two standard libraries give one answer;
- the tile grid is a function of the frame extent rather than of the host's core count, so two hosts give one
  answer;
- the second pass's featureless test measures each channel's own samples, where the published code reads its group
  with the wrong stride;
- a non-finite sample of the basic estimate is replaced by the noisy sample at the same place, where the published
  code copies the neighbour to its left.

Written independently for AfterNight: the request, in which the caller states the noise level and the white level
and the tuning scale is formed once from the two rather than inferred from the pixels; the working memory, one block
per worker from the operation's engine context, sized from the plan before the first patch; cancellation at a tile
boundary, with nothing written to the caller's destination; the noise estimate and the strengthen-and-subtract
iterations; and the variance-stabilising transform, written from Makitalo and Foi's publication with their
closed-form inverse.

## DA3D

### Upstream Source

- Work: "DA3D: Data Adaptive Dual Domain Denoising", IPOL, https://www.ipol.im/pub/art/2017/203/; the
  implementation's README names its support site, http://dev.ipol.im/~pierazzo/da3d/
- Authors: Nicola Pierazzo, Gabriele Facciolo
- Files audited: `DA3D.hpp`, `DA3D.cpp`, `DftPatch.hpp`, `Image.hpp`, `Utils.hpp`, `Utils.cpp`, `WeightMap.hpp`,
  `WeightMap.cpp` and `README.md`
- Licence: none in the copy audited. That implementation carried no licence text - its README is an algorithm
  description and a build recipe, and no file carries a copyright line or a licence; some headers name their author
  and a creation date - and source with no licence text grants nothing
- The archive the article page publishes, `da3d.v3.tgz` (SHA-256
  `0b1b2ed791ec3f5e6d6d02041d0f423e45611c5c2c16c63d8be2c0127c877c42`), read on 2026-11-04, does carry a `LICENSE`:
  `Copyright (C) 2017, Nicola Pierazzo <nicola.pierazzo@cmal.ens-cachan.fr>` and
  `Copyright (C) 2017, Gabriele Facciolo <facciolo@cmla.ens-cachan.fr>` under the two-clause BSD terms
  (BSD-2-Clause). It changes nothing here: no line of the implementation is carried, so no notice is owed, and the
  method below would read the same either way

### What Is Here

No line of `DA3D.cpp`, `DftPatch.hpp`, `Image.hpp`, `Utils.cpp` or `WeightMap.cpp` is present. The implementation
follows the published method through the paper's numbered lines: it finds the pixel with the least aggregation
weight, fits and subtracts a plane so the patch around it is flat, builds a bilateral weight from the guide,
attenuates the noisy patch's spectrum by how much power the guide has at each frequency against the noise, adds the
plane back and aggregates. The method's constants - the transform-weight floor, the shrinkage exponent, the
singularity threshold and the two relaxations of the regression weight - are kept as published.

Written independently for AfterNight, against the engine's structures: the patches are views over context memory;
the transform is the engine's FFTW adapter, one forward and one inverse plan serving every tile of an operation; the
coverage pyramid halves exactly, so its descent is total; the frame stays planar; the exponentials go through the CPU
dispatch table's Float32 exponential, held to five units in the last place, or through the scalar reference; the
tiles come from the frame extent, so the answer does not depend on the thread count; and whether the channels are
shrunk independently or as one shared frame is the caller's stated `GuidedShrinkageChannelCoupling`.

## Where the derived code lives

The estimator is `src/AfterNightEngine/src/algorithms/noisereduction/PatchEstimator.cpp`, and its parameter tables
and the orchestration around both stages are in `src/AfterNightEngine/src/algorithms/noisereduction/PatchDenoise.cpp`;
both credit Marc Lebrun's IPOL implementation and its licence. The guided shrinkage is
`src/AfterNightEngine/src/algorithms/noisereduction/DualDomainShrinkage.cpp`, which credits the published method.
The public surface, `denoiseImagePatches` and `shrinkImageWithGuide`, is
`src/AfterNightEngine/include/AfterNightEngine/Algorithms/NoiseReduction/PatchDenoise.h`. Each of the three files
points to this note.

`build/scripts/tools/dependencies.txt` records the `adapted` record `nl-bayes-patch-estimator` and the `replaced`
record `da3d-guided-shrinkage`, and `check_dependency_provenance.py` fails a file that loses its record's marker -
the upstream copyright line `Copyright (c) 2013, Marc Lebrun`, or `DA3D: Data Adaptive Dual Domain Denoising`.
