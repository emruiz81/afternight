# Median Networks Provenance

Source snapshot reviewed: 2026-08-29; compared with RawTherapee's `median.h` on 2026-11-03.

`src/AfterNightEngine/src/algorithms/spatial/MedianNetworks.h` holds four selection networks: fixed sequences of
minimum and maximum operations, pruned by hand to those that can still reach the middle rank, that return the median
of a complete 3x3, 5x5, 7x7 or 9x9 Float32 window without a branch. The order filter,
`src/AfterNightEngine/src/algorithms/spatial/OrderFilter.cpp`, is their only consumer.

## Source

The four functions - `median9f`, `median5x5`, `median7x7` and `median9x9` - are copied verbatim, apart from the
namespace and the inlining macro, from the upstream identified below.

## Upstream

Compared statement by statement, the four networks are the scalar `median()` specialisations for 9, 25, 49 and 81
values in the `median.h` that librtprocess (https://github.com/CarVac/librtprocess) carries from RawTherapee
(https://github.com/Beep6581/RawTherapee): the same minimum and maximum operations on the same elements in the same
order, and the same returned expression. Only the spelling differs - the function names, `float` for the template's
element type, nine scalar arguments for the 3x3 window where upstream takes an array, and `(std::min)` and
`(std::max)` in parentheses. The `median.h` compared is the one in the librtprocess snapshot that
[DEMOSAIC_PROVENANCE.md](DEMOSAIC_PROVENANCE.md) describes, whose version is not recorded. Read again on 2026-11-04 at
librtprocess `9a858270acb2096e2e403d932760ee688fcac425` (`0.12.0-6-g9a85827`), where `src/include/median.h` is git
blob `30adbacb384e20775769f11c1ac9a7e4f7b66f30`, unchanged since 2018-09-16, it states:

- `This file is part of RawTherapee.`
- `Copyright (c) 2004-2010 Gabor Horvath <hgabor@rawtherapee.com>`
- `These median implementations from Flössie and Ingo Weyrich are inspired by this work:`
  http://ndevilla.free.fr/median/median.pdf, http://pages.ripco.net/~jgamble/nw.html and
  https://github.com/hoytech/Algorithm-Networksort-Chooser
- the GNU General Public License, "either version 3 of the License, or (at your option) any later version"

The networks are therefore RawTherapee's, by Flössie and Ingo Weyrich, under GPL-3.0-or-later, which is compatible
with the engine and with the product and is not reached by the Apache-2.0 extension-protocol boundary.

## What Changed, And What Is The Engine's

The four functions are unchanged apart from the namespace and the inlining macro. Every input must be a number,
because a minimum or maximum involving a NaN depends on argument order, so the order filter scans a plane for a
non-finite sample before it calls them.

Written independently for AfterNight: the general bounded-scratch selection
(`Algorithms/Kernel/HistogramOrderKernel.h`), which serves every other window, rank and validity rule, and the choice
between the two. `OrderFilterMethod::Automatic` uses a network only where the request admits one - one of the four
windows, the median, and the all-valid rule with no mask - and the general selection everywhere else;
`OrderFilterMethod::SortingNetwork` and `OrderFilterMethod::SelectionKernel` force one or the other, and a forced
network that the request does not admit is refused rather than served by the general selection.

## Where the copied code lives

`src/AfterNightEngine/src/algorithms/spatial/MedianNetworks.h`, a private header in namespace `an::ported_detail`
that only `OrderFilter.cpp` includes; its module comment points to this note. `build/scripts/tools/dependencies.txt`
records the `adapted` record `median-networks`, and `check_dependency_provenance.py` fails the file if it loses the
phrase `Provenance: the four functions below are copied verbatim`.
