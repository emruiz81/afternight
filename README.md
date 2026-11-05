# AfterNight

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C++-20-blue.svg)](https://isocpp.org/)
[![Qt](https://img.shields.io/badge/Qt-6.10.3-green.svg)](https://www.qt.io/)

AfterNight is a desktop application for processing astronomical images, for Windows and Linux. It calibrates,
registers and stacks a night's frames into a master, and then carries the master through the processes that finish
it: background extraction, colour calibration, stretching, noise reduction, sharpening, star operations and the rest.
It is written in C++20 and Qt 6, on an image engine of its own.

![Image Integration Lite with a calibrated Hα dataset of M16 ready to run](docs/user-guide/docs/en/assets/images/interface/quick-start-step1.png)

![The integrated M16 master open in the image viewer with its display stretched](docs/user-guide/docs/en/assets/images/interface/quick-start-result.png)

## What it does

- **Integration.** The ImageIntegration workflow takes bias, dark, flat and light frames, groups and matches them,
  builds the calibration masters, calibrates, detects stars, registers, aligns, normalizes and stacks the lights,
  with rejection, weighting and drizzle, and writes a report of the run. A stage whose inputs and settings have not
  changed is reused rather than run again. Image Integration Lite is a simplified front end over the same workflow.
- **Processes.** About eighty processes in families: background extraction, colour calibration (including
  photometric and spectrophotometric colour calibration against Gaia DR3), colour adjustment and composition,
  stretching, noise reduction, sharpening, multiscale, masking, geometry, pixel math, star operations and analysis.
  Most preview their result on the open image before it is applied, and every applied step is kept in the image's
  history.
- **Astrometry.** Plate solving against Gaia catalogs, WCS with SIP distortion, and overlays of the solved field.
- **Files.** FITS, XISF, camera RAW, TIFF, PNG and JPEG images, and SER and video sequences.
- **Extensions.** Python extensions run in a separate, supervised host process, so an extension that fails does not
  take the application with it. [docs/extension-examples/](docs/extension-examples/README.md) shows how to write one.
- **The command line.** `AfterNightCLI` reads an image, stretches it for display and writes the result;
  `AfterNightCLI --help` lists its options.

## Downloads

Each release publishes a Windows installer and a Linux AppImage on the
[Releases page](https://github.com/emruiz81/afternight/releases). AfterNight 1.0.0 is the first public version; when
no release is listed there yet, build from source as [BUILD.md](BUILD.md) describes.

## Documentation

- The [user guide](https://emruiz81.github.io/afternight/) covers installation, the interface, every process and the
  common workflows. The installers carry it too, and **Help > User Guide** opens that copy without a network
  connection.
- [BUILD.md](BUILD.md) is how to build, test and package AfterNight from source.
- [AGENTS.md](AGENTS.md) is the contributor guide: the build order, the coding standards, the theme rules, the test
  policy and the definition of done. [docs/dev/README.md](docs/dev/README.md) indexes the developer documentation,
  and [docs/dev/guides/](docs/dev/guides/README.md) describes how the engine and the product are built and why.

## Building

AfterNight is built with Premake5-generated projects and the numbered scripts under `build/scripts/`, in a fixed
order: generate the projects, install the dependencies, build the third-party libraries, build AfterNight, deploy
its runtime dependencies. On Windows, from a Visual Studio 2026 Developer Command Prompt:

```cmd
git clone https://github.com/emruiz81/afternight.git
cd afternight
git submodule update --init --recursive
build\scripts\msvc\build_all_release.bat
bin\msvc\release\AfterNight.exe
```

On Linux:

```bash
git clone https://github.com/emruiz81/afternight.git
cd afternight
git submodule update --init --recursive
build/scripts/linux/build_all_release.sh
./bin/clang/release/run_AfterNight.sh
```

The first build installs and compiles every dependency and takes a long time, an hour or more on Windows.
[BUILD.md](BUILD.md) has the requirements, the step-by-step order, the tests and the installers. Continuous
integration builds and tests on Linux (`ubuntu-26.04`) and on Windows (Visual Studio 2026).

## Repository layout

```text
src/AfterNightEngine/        the image engine: images and metadata, codecs, processing, astrometry, stacking
src/AfterNightPlatform/      logging, the crash handler and process services
src/AfterNightConsumerRuntime/
                             the engine session, operation admission and image assets the product shares
src/AfterNightControls/      reusable Qt controls, application services and the theme
src/AfterNightProcesses/     every user-facing process and the ImageIntegration workflow
src/AfterNightApp/           the application
src/AfterNightCLI/           the command-line tool
src/AfterNightExtension*/    the extension protocol, the two extension hosts and the runtime backend
src/AfterNightTests/         the Google Test suites, one Test_* executable per area
src/AfterNightBench/         the benchmark runners
build/                       Premake definitions, platform scripts and the repository's checks
docs/user-guide/             the user guide (MkDocs)
docs/dev/                    developer documentation
docs/extension-examples/     extension authoring examples
python/modules/              the Python SDK extensions import
installer/                   the Windows installer and the Linux AppImage packaging
```

## Contributing

Contributions are welcome. [CONTRIBUTING.md](CONTRIBUTING.md) explains how: the Developer Certificate of Origin
sign-off, the test-first rule, the formatting and licence-header checks, and what a pull request carries. Everyone
taking part is expected to follow the [code of conduct](CODE_OF_CONDUCT.md).

## Security

Report a vulnerability privately, as [SECURITY.md](SECURITY.md) describes, not in a public issue.

## Licence

AfterNight is distributed under the GNU General Public License, version 3 or (at your option) any later version; see
[LICENSE](LICENSE). The extension protocol boundary - the protocol library, the Lite extension host and the protocol
half of the Python SDK - is distributed under the Apache License 2.0
([LICENSES/Apache-2.0.txt](LICENSES/Apache-2.0.txt)), so an extension that uses only the protocol is not bound by the
GPL. Every source file states its licence in an SPDX line.
[docs/dev/EXTENSION_HOST_MODES_AND_LICENSING.md](docs/dev/EXTENSION_HOST_MODES_AND_LICENSING.md) describes the
boundary in full.

## Acknowledgments

AfterNight builds on Qt, KDDockWidgets, oneTBB, Highway, FFTW, GSL, LibRaw, RawSpeed, G'MIC, CFITSIO, OpenXISF,
FFmpeg, Exiv2, Crashpad, Google Test and the wider open-source astronomy and imaging community.

Code derived from other projects carries their credit and licence in the files that hold it, in a provenance note
under [docs/dev/](docs/dev/README.md) and in the About dialog:

- the demosaic methods and the median networks are ported from RawTherapee, as librtprocess carries them;
- the star finder and its profile fit, and the background sampling and model, are adapted from Siril;
- Auto Gradient Removal is derived from Siril's AutoGradientRemoval script, by Cyril Richard;
- the drizzle's corner interpolation is derived from Siril's routine of the same name, itself derived from STScI's
  drizzle under its BSD 3-Clause licence;
- the narrowband palettes are derived from Seti Astro Suite Pro, by Franklin Marek;
- the NBColorMapper layer mapping is derived from Cosmic Photons' NBColorMapper, by Mike Cranfield;
- the patch denoiser's estimator is derived from the IPOL implementation of Non-Local Bayes;
- the auto-adaptive weighted mean and the star mask's shapes are derived from DeepSkyStacker, under its BSD 3-Clause
  licence;
- the busy indicator is QtWaitingSpinner, under its MIT licence.
