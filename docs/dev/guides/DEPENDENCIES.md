# Dependencies

Which libraries the engine uses, where in the tree each is allowed to appear, how the engine reports
the versions it was built against and loaded with, and how a published advisory is answered. The
register is a file and a checker, not this page; this page says how to read them.

## The register

`build/scripts/tools/dependencies.txt` records every third-party library and every ported subsystem
this side of the workspace retains - the engine, its two adapters, the opt-in device backend, the
suites that link them and the benchmark layer - with eight facts for each: the version, where it comes
from, its licence, its upstream, where its security advisories are published, who builds it, who
deploys it, and its **boundary**: the one place in the tree it may appear.

`build/scripts/tools/check_dependency_provenance.py` joins each claim to something that would have to
change with it - the include lines, the generated link lines, the install and deploy scripts, the
engine's own report and the suite that pins it - and fails when any of them names a library the
register does not, or the register names one they do not. With `--installed` it also compares every
packaged version with the host's package database. The join exists because a register written once
stops being true without any build saying so.

## Where each library may appear

A library's headers appear in exactly one layer or directory, and `check_include_layers.py` refuses
an include anywhere else. That is what keeps a consumer that never decodes a RAW file, never runs
Python and never touches a GPU from acquiring any of those toolchains.

| Library | Boundary | Why the engine has it |
|---|---|---|
| oneTBB | `runtime` and `memory` | The task arenas an operation's threads run in |
| Highway | `backends/cpu` | Portable vector code for the compiled kernels |
| CFITSIO, libtiff, libpng, libjpeg-turbo, LibRaw, OpenXISF | each in its own `io/<codec>` directory | The codecs |
| FFmpeg | below `io/sequences/ffmpeg` alone | Video sequence sources and sinks |
| Exiv2 | `interop/exiv2` | Reading and writing the Exif and XMP blocks the raster codecs carry |
| FFTW (single precision) | `interop/fftw` | Phase correlation |
| GSL | `interop/gsl` | One function: the inverse Student-t distribution the rejection estimators need |
| nlohmann-json | `interop/json` | The persisted star-profile and colour-calibration JSON documents |
| Python | the Python adapter's own directory | The optional Python adapter |
| G'MIC | the G'MIC adapter's own directory | The optional G'MIC adapter |
| A GPU SDK | the opt-in device backend, and `backends/gpu` | Nothing that ships |

Thirteen more are included by no source of this engine and linked anyway, behind a library that does
include them: the x264, x265 and libvpx encoders behind FFmpeg; Expat, Brotli and inih behind Exiv2;
zlib behind the FITS, TIFF, XISF and RAW codecs; Little CMS and JasPer behind LibRaw; pugixml behind
RawSpeed and OpenXISF; LZ4 and Zstandard behind OpenXISF; and the private RawSpeed build. They are on the
register because the loader brings them in whether or not a source names them. OpenCV and
GoogleTest are on it too: the engine describes OpenCV's buffers without including OpenCV
(`Interop/OpenCvAdapter.h` checks its packed type codes against the library's own constants in a
suite), and GoogleTest is linked by suites alone.

**A published header includes no external library.** The interop headers describe another library's
buffer in their own field types - `Interop/ExternalBuffer.h`, `Interop/OpenCvAdapter.h`,
`Interop/FftwAdapter.h`, `Interop/GslAdapter.h` - so a consumer that uses none of them includes none of
them, and zero-copy is decided rather than assumed: a refused bridge still yields the foreign buffer's
engine layout, so the copy goes through `Image/ImageConversion.h` and shows up in a profile. A bridge
never converts: one that rescaled some sample types and not others would leave a caller unable to
tell what the numbers mean, so a difference in numeric domain is refused by name.

**Using GSL is a decision each time.** A call into GSL states what was decided about it and why. A
call is replaced when it is measured overhead, forces an allocation, a copy or GSL's global error
handler, or when a library already in the build or an engine kernel does better; it is kept where
nothing comparable exists or where replacing it would change results for no benefit. One function
remains, the inverse Student-t distribution; the engine computes its Akima interpolation itself.

## The optional adapters

Python and G'MIC are not engine dependencies at all. Each is reached through a separate shared
library - `AfterNightEnginePythonAdapter`, `AfterNightEngineGmicAdapter` - that links the
engine's published surface and one external library, while the engine links neither. An adapter's
own published surface includes nothing of its library either, so a consumer compiles against it
without that toolchain.

Generating with `AFTERNIGHT_PYTHON=0` or `AFTERNIGHT_GMIC=0` builds the adapter with no library: it
reports itself absent (`pythonAdapterCapabilities`, `gmicAdapterCapabilities`) and refuses every call
by name. Both configurations of each adapter are built and tested. Each adapter is linked by its own
suite, `Test_EnginePythonAdapter` or `Test_EngineGmicAdapter`, and otherwise only by a target that
asks for the library it adapts - `AfterNightProcesses`, for its G'MIC processes - and
`check_build_graph.py` enforces it. The G'MIC adapter runs each call on its own 8 MiB thread, because
G'MIC's standard library filters overflow a default 1 MiB Windows stack. Each adapter holds a lease on
its engine context, so a context with a live interpreter or session refuses to shut down rather than
release what the library still uses; and no engine operation calls an adapter, so the engine behaves
the same whichever adapters are installed.

## Versions, reported by the engine itself

`Core/DependencyReport.h` is the engine's half of the register: `engineDependencyCount()` and
`engineDependencies()` list the libraries inside the loaded binary, each with **two** versions - the
one its headers declared when the engine was compiled and the one the loaded copy reports about
itself at run time. On a shared library those are different facts, and either may be empty; empty is
an answer. `DependencyReportTest` in `Test_EngineCore` holds every loaded library to the release its
headers describe, which is what fails a deployment that copied a stale DLL beside a fresh engine.

## Advisories

`build/scripts/tools/safety_register.txt` holds a `review` of every library the shipped binaries load -
the version whose advisories were read, and an answer for every advisory whose affected range includes
it - and a `finding` per defect, rated by what it can do *in this engine* rather than by a published
score, because a crafted file reaches a loaded library through the engine and a published score rates
the library rather than this engine's use of it. `check_safety_register.py` fails a critical or high
finding that is open or accepted, a repair that names no existing test or pinned release, an advisory
nobody answered, and a review of a version the provenance register no longer pins; with `--binaries`
it reads the shipped binaries' import closure and refuses a module no review names.

When an advisory means a newer release is needed ahead of the vcpkg checkout, the port is pinned
through `build/vcpkg/overlay-ports` and moved with
`vcpkg upgrade <port> --no-dry-run --overlay-ports=..\build\vcpkg\overlay-ports`, not `install`: an
overlay moves only the port that needs a newer release, where moving the vcpkg checkout would move
every port. FFmpeg, Expat and libvpx are pinned this way; the same directory carries the private RAW
ports, LibRaw's X3F feature and a repair to x264's AVX-512 path, each explained in its own README.

## Ported code

Twelve subsystems have a `ported` record, and eleven of them carry code or a method from elsewhere:

| Record | What is carried |
|---|---|
| demosaic-rawtherapee | The demosaic methods, adapted from RawTherapee and its librtprocess extraction ([DEMOSAIC_PROVENANCE.md](../DEMOSAIC_PROVENANCE.md)) |
| nl-bayes-patch-estimator | The patch estimator, adapted from the IPOL NL-Bayes implementation ([PATCH_DENOISE_PROVENANCE.md](../PATCH_DENOISE_PROVENANCE.md)) |
| da3d-guided-shrinkage | The guided shrinkage, written from the published DA3D method; no upstream code ([PATCH_DENOISE_PROVENANCE.md](../PATCH_DENOISE_PROVENANCE.md)) |
| healpix-indexing | HEALPix indexing and the cone query, re-derived from the published definition |
| akima-interpolation | Akima interpolation, re-derived from the method as the GNU Scientific Library computes it |
| median-networks | Four median selection networks, copied verbatim; their operations are RawTherapee's ([MEDIAN_NETWORKS_PROVENANCE.md](../MEDIAN_NETWORKS_PROVENANCE.md)) |
| drizzle-corner-interpolation | The four-corner interpolation, adapted from Siril's routine of the same name ([DRIZZLE_CORNER_INTERPOLATION_PROVENANCE.md](../DRIZZLE_CORNER_INTERPOLATION_PROVENANCE.md)) |
| histogram-percentile-search | The histogram percentile search, re-derived from Ingo Weyrich's routine |
| siril-auto-gradient-removal | The automatic background, adapted from Siril's AutoGradientRemoval script ([AUTO_GRADIENT_REMOVAL_PROVENANCE.md](../AUTO_GRADIENT_REMOVAL_PROVENANCE.md)) |
| cosmic-photons-nbcolormapper | The layered colour mapper, adapted from Cosmic Photons' NBColorMapper ([NBCOLORMAPPER_PROVENANCE.md](../NBCOLORMAPPER_PROVENANCE.md)) |
| setiastro-narrowband-palettes | The twelve narrowband palettes, adapted from Seti Astro Suite Pro ([NARROWBAND_PALETTE_COMPOSER_PROVENANCE.md](../NARROWBAND_PALETTE_COMPOSER_PROVENANCE.md)) |

The twelfth, `video-containers`, records an audit that carried nothing: the AVI, SER and MP4
containers are written here from their published documents. Each record states its upstream, licence
and relation, and every file that carries ported code credits it in its module comment. Porting is a
re-evaluation rather than a translation - correctness, numerical behaviour, design, allocation,
concurrency and provenance are each audited - because no source algorithm is assumed correct: a
translation carries its defects forward, and a port is the moment someone reads the code closely
enough to judge it. Matching the upstream output is where a port starts, and it stops being the goal
once a defect is confirmed; what changed is recorded in the file.

## Licensing

The engine is GPL by design and stays so while any GPL dependency remains - FFTW, GSL, Exiv2, the
RawTherapee demosaic port and FFmpeg as built are among them. OpenXISF, behind the XISF codec, is
Apache-2.0, and the product's About dialog carries its notice. A library is never chosen or replaced on
licence alone: OpenXISF serves the XISF codec for its region reads, bounded memory and verified
checksums.
