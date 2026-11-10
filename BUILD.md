# Building AfterNight

How to build, run, test and package AfterNight from source on Windows and Linux. [AGENTS.md](AGENTS.md) is the
canonical statement of these rules; this page is the walk-through.

## Requirements

| Platform | Toolchain and shell | Notes |
| --- | --- | --- |
| Windows x64 | Visual Studio 2026 with the C++ desktop workload and the "C++ Clang tools for Windows" component, used from a Developer Command Prompt | The scripts load the Visual Studio environment themselves through `vswhere`, so a plain shell works too. Qt's `msvc2022_64` package is used with the Visual Studio 2026 toolchain. |
| Linux x86-64 | An Ubuntu or Debian-style system with `sudo` | The install script installs Clang, the distribution packages, Qt, Python and the vcpkg ports. Continuous integration builds on `ubuntu-26.04`. |

Both need Git with submodule support and Python 3 on `PATH` for the repository's checks and build helpers; the
install step provisions the Python 3.14.7 that the product embeds. Packaging the Windows installer needs Inno Setup
6.2 or later.

The first build downloads and compiles every dependency from source: expect several gigabytes on disk, and an hour
or more for the Windows dependency step alone. Do not interrupt it.

## Clone

```bash
git clone https://github.com/emruiz81/afternight.git
cd afternight
git submodule update --init --recursive
```

The submodules are `vcpkg` and `kddockwidgets`. OpenFITS and OpenXISF, the FITS and XISF libraries, are not
submodules: the dependency step clones each one's published release, verifies the commit and builds it as a shared
library into `openfits/` and `openxisf/` (`build/scripts/msvc/_build_openfits.bat`, `_build_openxisf.bat`,
`build/scripts/linux/_build_openfits.sh`, `_build_openxisf.sh`), until the vcpkg registry carries them.

## The build order

The order is the same on both platforms and is not optional:

1. Generate the project files with Premake5.
2. Install the dependencies.
3. Build the third-party libraries.
4. Build AfterNight and deploy its runtime dependencies into `bin/<toolchain>/<config>/`.

Nothing is built before the dependencies are installed and the projects generated. On Windows the deployment script
is numbered `04` and the build `05`; on Linux the build is `04` and the deployment `05`. Each platform's numbering is
deliberate - run the scripts in the order given.

AfterNight uses Premake5, not CMake; the Premake binary is under `tools/`. `01-create_project_files` writes
`build/vs2026-msvc/AfterNight.slnx` and `build/vs2026-msvc/tests/AfterNightTest.slnx` on Windows and the Makefiles
under `build/make-linux/` on Linux. Sources are globbed, so a new source file only needs `01-create_project_files`
run again.

## Windows (MSVC)

The whole chain, release configuration:

```cmd
cd build\scripts\msvc
build_all_release.bat
```

Step by step:

```cmd
cd build\scripts\msvc
01-create_project_files.bat
02-install_msvc_deps.bat
03a-build_3rdparty_libs_release.bat
04a-deploy_deps_release.bat
05a-build_afternight_release.bat
```

Run the application from the repository root:

```cmd
bin\msvc\release\AfterNight.exe
```

The `b` scripts (`03b`, `04b`, `05b`) and `build_all_debug.bat` build the debug configuration, whose binaries carry
a `_d` suffix. `build_active_targets.bat release --tests` builds every active target, test suites included.

If an MSVC build is interrupted, run `build\scripts\msvc\check_msbuild_dependency_tracking.bat --fix` before the
next one; the `05` and `09` scripts already do. An interrupted build can leave an object compiled against an older
revision of a header, which [docs/dev/INCREMENTAL_BUILD_DEPENDENCY_TRACKING.md](docs/dev/INCREMENTAL_BUILD_DEPENDENCY_TRACKING.md)
describes.

## Linux (Clang)

The whole chain, release configuration:

```bash
cd build/scripts/linux
./build_all_release.sh
```

Step by step:

```bash
cd build/scripts/linux
./01-create_project_files.sh
./02-install_linux_deps.sh
./03a-build_3rdparty_libs_release.sh
./04a-build_afternight_release.sh
./05a-deploy_all_deps_release.sh
```

Run the application from the repository root; the script sets the library paths:

```bash
./bin/clang/release/run_AfterNight.sh
```

The `b` scripts and `build_all_debug.sh` build the debug configuration. Linux binaries land in `bin/clang/`.

## The command-line tool

`bin/<toolchain>/<config>/AfterNightCLI [options] <input> <output>` reads an image, stretches it for display and
writes the result. `--help` prints its options and exit codes.

## Tests

The test projects are generated with the rest by `01-create_project_files`, one `Test_*` executable per area under
`src/AfterNightTests/`. Build and run them:

```cmd
cd build\scripts\msvc
09a-build_afternight_tests_release.bat
run_tests.bat release
```

```bash
cd build/scripts/linux
./08a-build_afternight_tests_release.sh
./run_tests.sh
```

The runners give every executable a throwaway storage directory of its own. To run one executable by hand, set
`AFTERNIGHT_TEST_STORAGE_ROOT` to an empty directory under the system temporary directory first, so the suite never
writes into the application's own data directory:

```cmd
set AFTERNIGHT_TEST_STORAGE_ROOT=%TEMP%\an_test_image_integration
bin\msvc\release\Test_ImageIntegration.exe --gtest_filter=*Manifest*
```

The engine has a shorter loop of its own, which builds the engine and its direct suites and nothing else:

```cmd
build\scripts\msvc\build_afternightengine.bat release --with-tests
```

```bash
build/scripts/linux/build_afternightengine.sh release --with-tests
```

[docs/dev/LOCAL_VALIDATION.md](docs/dev/LOCAL_VALIDATION.md) lists the repository's checks and when each runs, and
[docs/dev/guides/TESTING.md](docs/dev/guides/TESTING.md) describes the suites, the fixtures and the campaigns.

## Formatting

Every change to C or C++ under `src/` is checked against the repository's `.clang-format` with clang-format 22, the
version Visual Studio 2026 bundles and continuous integration pins:

```cmd
build\scripts\msvc\check_clang_format.bat origin/main
build\scripts\msvc\format_changed.bat origin/main
```

```bash
build/scripts/linux/install_clang_format.sh
build/scripts/check_clang_format.sh origin/main
build/scripts/format_changed.sh origin/main
```

## The user guide

The user guide is MkDocs with the guide's own theme, under `docs/user-guide/`. The platform helpers build it in a
locked environment:

```cmd
build\scripts\msvc\build_user_guide_docs.bat
```

```bash
build/scripts/linux/build_user_guide_docs.sh
```

The output is `docs/user-guide/site/`, which the installers bundle for **Help > User Guide**.

## Installers

Package only after a successful release build, the deployment step and a user guide build.

Windows, with Inno Setup:

```cmd
iscc installer\win_installer.iss /DMSVC
iscc installer\win_installer.iss /DMSVC /DADMIN=1
```

The first builds the per-user installer, the second the one that installs for all users.

Linux AppImage (the script downloads `appimagetool` when it is missing):

```bash
installer/linux_packager.sh
```

A release build passes `/DREQUIRE_DOCS` on Windows and `REQUIRE_DOCS_SITE=1` on Linux, so a missing
`docs/user-guide/site/` fails the package instead of shipping without the offline guide. A local build bundles the
guide when the site directory exists.

## Crash dumps

Crashpad writes minidumps locally and uploads nothing.

| Platform | Location |
| --- | --- |
| Windows | `%LOCALAPPDATA%\AfterNight\CrashDumps\reports\` |
| Linux | `$XDG_DATA_HOME/AfterNight/CrashDumps/pending/`, or `~/.local/share/AfterNight/CrashDumps/pending/` when `XDG_DATA_HOME` is unset |

[docs/dev/CRASH_SYMBOLICATION.md](docs/dev/CRASH_SYMBOLICATION.md) explains how to read one.

## Dependencies

The install scripts install every dependency; `build/scripts/tools/dependencies.txt` is the register of what the
engine uses, with versions, sources and licences.

| Dependency | Used for |
| --- | --- |
| Qt 6.10.3 | The interface, SVG, SQL, networking, and the PySide6 version extensions see |
| KDDockWidgets | Docking |
| OpenFITS, OpenXISF, LibRaw with a RawSpeed build of AfterNight's own, libtiff, libpng, libjpeg-turbo, Exiv2 | FITS, XISF, camera RAW, TIFF, PNG and JPEG files and their metadata |
| FFmpeg | Video sequences |
| oneTBB, Highway | Parallelism and vectorized kernels |
| FFTW (single precision), GSL | Phase correlation, and one distribution function |
| G'MIC | The G'MIC-backed processes, through an optional adapter |
| SQLite, through Qt's driver | Project state and the application's databases |
| nlohmann-json, spdlog, cmark-gfm, libsodium | Documents, logging, report rendering and extension package signatures |
| Crashpad | Local crash dumps |
| Google Test | The test suites |
| Python 3.14.7 | The extension hosts and runtime backend |

OpenCV, the public LibRaw and, on Windows, Eigen are linked only by the library parity suite, as reference libraries
the engine is compared with; none of them ships with the product. The repository's own third-party build compiles
`cgmic` from `3rdparty/src`; the other libraries are vcpkg packages built from source, some through the overlay
ports in `build/vcpkg/overlay-ports/`.

## Continuous integration

The workflows under `.github/workflows/` run the same numbered scripts, so a change to a script reaches CI without a
workflow edit. Linux builds and tests on `ubuntu-26.04`, Windows on the Visual Studio 2026 runner with the
release-only `x64-windows-release` triplet, which `AFTERNIGHT_VCPKG_TRIPLET` names to the install and deploy
scripts. `release.yml` builds both installers and the user guide from a version tag.
