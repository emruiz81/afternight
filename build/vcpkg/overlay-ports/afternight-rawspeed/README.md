# Private RawSpeed package

Owner: AfterNightEngine's RAW adapter. This package is not the general LibRaw dependency.
The source revision is `darktable-org/rawspeed@de70ef5fbc62cde91009c8cff7a206272abe631e`;
the archive SHA-512 is the literal in `portfile.cmake`. `data/cameras.xml` from that same archive
is embedded at build time. C ABI and correction revisions are both 1.

`0001-libraw-compatibility.patch` is a forward patch against that revision. It incorporates the
revision-specific LibRaw integration adjustments: remove `final` from CameraMetaData so the
in-memory database can derive from it, correct allocator rebinding for current C++ libraries,
preserve native LibRaw curve/black handling, avoid external logging, and retain LibRaw's maker-note
and decoder compatibility changes. Exception messages remain available internally. The additional
upstream decoder families in this patch do not authorize engine profiles.

`0002-cancellation.patch` adds borrowed row/tile/correction checkpoints, transfers those callbacks
when DNG replaces its RawImage, and checks identified geometry before allocating samples. Production
builds are serial. Consequently internal worker functions may propagate allocation exceptions to the
C boundary; they must not terminate in a historical OpenMP `noexcept` worker. No C export or callback
loses its exception barrier. Failed aligned allocations throw `bad_alloc`, not a malformed-file error.

`AnRawSpeed.cpp` owns the parser's Buffer object for the entire decoder lifetime, as well as its
borrowed encoded bytes. A handle owns its own immutable camera database; no global database cache
or worker pool exists. Nikon and Sony curves are retained without dithering and looked up exactly
once. Every other family keeps its original correction path. The wrapper validates dimensions,
component count, pitch and CFA before returning a borrowed pointer. LibRaw releases that pointer
through `anRsDestroy`, never through another library's allocator.

Windows uses the nominated VS clang-cl, `/MD` or `/MDd`, and a C-only interface to MSVC LibRaw.
RawSpeed and its private pugixml use `_ITERATOR_DEBUG_LEVEL=0` in Debug: MSVC's checked `std::string`
constructor allocates a proxy inside a `noexcept` function, which terminates under injected OOM.
Assertions and the Debug CRT remain enabled. The private LibRaw and the engine retain their ordinary
Debug container ABI. This distinction is safe because their shared boundary contains no C++ objects.
The Sony decryption boundary uses byte pointers: TIFF metadata offsets need not be word-aligned.
The full Sony corpus is checked under ASan/UBSan as well as against the native pixel oracle.
Linux uses the nominated Clang 21, static PIC archives and engine-level archive symbol hiding.

The exact archive, patched complete source, embedded database, wrapper, patches and CMake recipe
are installed under `share/afternight-rawspeed`. The root LGPL 2.1 text is preserved, as are all
individual upstream source notices, external-source notices and database notices. Camera-database
construction also derives from LibRaw 0.22.1's RawSpeed3 wrapper; its COPYRIGHT, LGPL and CDDL texts
are installed under `libraw-wrapper-notices`. AfterNight's original additions follow this repository's
GPL-3.0 distribution. The port deliberately does not summarize this mixed inventory as one SPDX
licence inferred from one header. Distribution must retain the installed source/notice bundle and
the transitive packages' notice/source material.

`AnRawSpeedTests` compiles the production objects and boundary, exercising invalid ABI arguments,
initial and in-decode OOM, all Bayer phases/green slots, every UInt16 code across curve breakpoints,
odd tails, saturation, pre-allocation geometry refusal and cancellation during lookup. Its optional
`--file PATH FAMILY WIDTH HEIGHT FILTERS [XTRANS_DIGITS]` injects failures at seven positions in a
real decoder allocation sequence. Injection exists only in this executable. No test switch is exposed
by the shipped wrapper. The separate oracle executables exercise the installed DLL's C ABI.

Change the port revision and rerun qualification when the wrapper, patch series, numerical compiler
configuration or camera database changes. vcpkg hashes the complete recipe and the actual clang-cl
executable through the tracked triplet, in addition to its normal compiler/CRT/dependency identity.
