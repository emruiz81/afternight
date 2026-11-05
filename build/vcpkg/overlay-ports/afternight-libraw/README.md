# Private reentrant LibRaw package

Owner: AfterNightEngine's RAW boundary. Upstream: LibRaw 0.22.1 and LibRaw-cmake
`eb98e4325aef2ce85d2eb031c2ff18640ca616d3`; archive SHA-512 values are pinned in `portfile.cmake`.
Stock `libraw` remains the decoder of a build without the private packages (`AFTERNIGHT_RAWSPEED=0`)
and the library parity suite's independent oracle. Headers install under
`include/afternight-libraw/libraw`, the CMake package is `AfterNightLibRaw`, and the library is
`afternight_libraw_r` (Debug suffix `d`). No `include/libraw` or `raw_r` file is replaced.

`dependencies.patch` is the existing stock overlay's LCMS CMake adjustment.
`0001-private-bridge.patch` exposes the maintained typed adapter hooks, owns the encoded copy and
wrapper handle until recycle, and removes the upstream RawSpeed attempt's implicit native retry.
AfterNight's adapter reopens a fresh LibRaw object for its one permitted native retry. The decoder's
native output is preserved and RawSpeed's upstream option is disabled. X3F, JPEG/DNG, LCMS and zlib
support remain enabled; OpenMP and the obsolete RawSpeed1 integration remain disabled.

`0002-zero-jpeg-difference.patch` returns zero for a zero-bit lossless-JPEG difference before
inspecting its sign bit. The upstream expression shifts by -1 for this valid code, as exposed by
the instrumented Canon corpus. The complete sample/metadata goldens remain the regression oracle.

`AnRawBackend.cpp` classifies actual decoder member-function identities and compression headers,
not names or extensions. It reports Nikon compression-version bytes and lossless-JPEG precision,
components and predictor, plus tile layout, curve presence and crop geometry. Unknown variants
do not widen the engine's tested profile table. Classification reads only headers and restores the
stream position. Input copying, aligned samples, curves and parser/database allowance enter the
64-bit preflight estimate. The LibRaw MiB guard remains authoritative; the estimate does not bound
all third-party workspace or turn it into engine-counted memory.

Complete patched LibRaw/LibRaw-cmake source and upstream notices accompany the installed recipe.
LibRaw's dual LGPL/CDDL notice is retained, including the separate X3F and other source notices;
the private bridge additions use LibRaw's dual terms. See the adjacent RawSpeed recipe for that
component's source and notice inventory. Static transitive codec copies avoid competing Windows
DLL names; Linux hides all private archive symbols in the engine DSO.

Canon sRAW chroma scaling uses integer multiplication instead of shifting signed negative
samples. The factors fit in int for every short input; the complete mRAW fixture remains
checked against the independent stock sensor and visible-sample goldens.
