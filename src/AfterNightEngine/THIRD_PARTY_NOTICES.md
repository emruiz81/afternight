# Third-party notices

The optional private RAW backend owns two pinned vcpkg packages. LibRaw 0.22.1 and its
`eb98e4325aef2ce85d2eb031c2ff18640ca616d3` CMake recipe retain their complete upstream COPYRIGHT,
LGPL/CDDL and per-source notices. RawSpeed is pinned to
`de70ef5fbc62cde91009c8cff7a206272abe631e`, including its matching camera database, root LGPL 2.1
text and all individual source notices. The wrapper's camera database construction derives from
LibRaw's RawSpeed3 wrapper; those notices accompany it separately. AfterNight owns the private
boundary, cancellation, exact-curve and packaging changes. The complete patched source, recipes,
patches and notices install into each package's `share` directory; they must accompany distribution
along with the transitive dependencies' notice/source material.

The maintained inventories and patch rationales are
[RawSpeed](../../build/vcpkg/overlay-ports/afternight-rawspeed/README.md) and
[private LibRaw](../../build/vcpkg/overlay-ports/afternight-libraw/README.md), and the public RAW
samples' sources and licences are listed in
[`docs/dev/rawspeed-public-samples.json`](../../docs/dev/rawspeed-public-samples.json).

The auto-adaptive weighted-mean arithmetic and radial star-mask formulas were adapted from
DeepSkyStacker. AfterNight's changes are distributed under the repository's GPL-3.0 license;
the upstream BSD notice below is retained for the adapted work. The files that carry it are
`src/kernels/reference/StackMethodRow.inl`, `src/backends/cpu/StackMethodCpu.cpp`,
`include/AfterNightEngine/Algorithms/Stars/StarMask.h` and `src/algorithms/stars/StarMask.cpp`
under this directory, and the benchmark oracle `src/AfterNightBench/common/StackingOracle.cpp`;
[the provenance note](../../docs/dev/DEEPSKYSTACKER_PROVENANCE.md) says what each carries.

Source: https://github.com/deepskystacker/DSS

Notice reference: https://github.com/deepskystacker/DSS/blob/4dc1c5c45b4bd7bf8d1a5830f1780ddafee1d43f/LICENSE

The original AfterNight import did not record an exact upstream revision. This reference identifies
the upstream notice at the time of that import; it does not assert a byte-identical source copy.
The routine and the shapes were read again at that commit on 2026-11-04 and match what the files
carry.

BSD 3-Clause License

Copyright (c) 2006-2019, LucCoiffier
Copyright (c) 2018-2023,
		David C. Partridge, Tony Cook, Mat Draper,
		Simon C. Smith, Vitali Pelenjow, Michal Schulz, Martin Toeltsch

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the copyright holder nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The drizzle drop's corner interpolation, in `src/algorithms/drizzle/DrizzlePixmap.cpp`, is
adapted from Siril's `interpolate_four_points`, the single-precision form of the routine of that
name in the STScI drizzle library (https://github.com/spacetelescope/drizzle). AfterNight's changes are
distributed under the repository's GPL-3.0 license; the drizzle code's notice below, which both the
STScI library and the Siril file carry, is retained for the adapted work.
[The provenance note](../../docs/dev/DRIZZLE_CORNER_INTERPOLATION_PROVENANCE.md) says what is carried.

Copyright (C) 2011,2014 Association of Universities for Research in
Astronomy (AURA)

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    1. Redistributions of source code must retain the above
      copyright notice, this list of conditions and the following
      disclaimer.

    2. Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials
      provided with the distribution.

    3. The name of AURA and its representatives may not be used to
      endorse or promote products derived from this software without
      specific prior written permission.

THIS SOFTWARE IS PROVIDED BY AURA ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL AURA BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
OF THE POSSIBILITY OF SUCH DAMAGE.

The XISF codec links OpenXISF, distributed under the Apache License 2.0
(https://github.com/astroformats/openxisf). The attribution its NOTICE file states, which a
redistribution carries and the About dialog's licence page shows:

OpenXISF
Copyright 2026 Ezequiel Ruiz

This product includes software developed by Ezequiel Ruiz and the
contributors to OpenXISF.

OpenXISF is an independent implementation of the XISF format. It is not
affiliated with or endorsed by Pleiades Astrophoto S.L. (PixInsight).
