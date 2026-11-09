# System Requirements

## Purpose, scope, audience, prerequisites, expected result, and authority

Use this page to decide whether a computer can run a packaged AfterNight 1.0.1 build, what optional software or data a workflow needs, how many threads and how much memory AfterNight uses, and how to estimate resources for a large project.

The authoritative hard requirements are deliberately narrow: the distributed artifacts are 64-bit x86 packages compiled for AVX2, and their operating-system/runtime boundaries come from the package, build target, and bundled Qt version. The project does **not** currently publish or enforce a certified minimum CPU core count, RAM capacity, free-disk amount, or discrete-GPU model. Values derived from image dimensions and catalog manifests are labeled as calculations or planning guidance rather than benchmarked application limits.

This page applies to users selecting or diagnosing a current packaged host and
to contributors reproducing the documented source-build environments. Begin
with the platform matrix, add any optional workflow requirements, and finish
with a decoded-image and storage estimate.

## Reachability or applicability

### Supported platform and package matrix

| Platform | Current package and verified target | Compatibility boundary | Notes |
| --- | --- | --- | --- |
| Windows | Primary MSVC x64 installer; built by the Windows release workflow | The installer enforces x64. Qt 6.10 supports Windows 10 version 1809 or later and Windows 11 on x86-64; the installer itself does not encode a narrower Windows-version check. | A per-user install is the default; an administrator variant can be built. Windows 11 is the recommended current host. |
| Linux | x86_64 AppImage built and tested on Ubuntu 24.04 | x86_64 is enforced by the packager. Ubuntu 24.04 is the verified/recommended distribution target; compatibility with older distributions is not claimed. | Make the AppImage executable. A FUSE 2-compatible runtime is normally needed; AppImage's `--appimage-extract-and-run` fallback can be used where FUSE mounting is unavailable. On Ubuntu 24.04 the compatibility package is `libfuse2t64`. |
| macOS, Windows on Arm, Linux on Arm | No current package | Unsupported release target | Building or running on these targets is not covered by the current scripts or release validation. |

!!! note "Qt compatibility is necessary, not a release certification"
    The Windows floor above is inherited from the bundled Qt 6.10 platform support. AfterNight's release workflow does not test every Qt-supported Windows edition, and the installer does not itself reject an older Windows version.

## Complete reference matrix

### Minimum and recommended host planning

| Resource | Minimum contract | Recommended planning guidance |
| --- | --- | --- |
| CPU | x86-64 with AVX2 (with FMA) instruction support. The engine and every application target are compiled for AVX2; no minimum core count is encoded. | A current multi-core x86-64 CPU improves CPU-bound calibration, registration, stacking, and export work. More cores help only where the selected stage is parallel, and the Processing page's **Max Threads** bounds how many AfterNight uses. On a processor with the AVX-512 subset of Skylake-SP and later parts (F, BW, DQ, and VL), the engine selects its AVX-512 kernels at run time where it has them; nothing needs to be configured. |
| Memory | Enough for the operating system, AfterNight, the active decoded images, and processing intermediates. No fixed RAM minimum is published or checked. By default AfterNight sets no memory limit of its own; a limit set on the Processing page bounds what the engine holds for images and refuses an operation that would exceed it. | Size memory from representative decoded frames and the intended workflow, then leave substantial headroom for intermediates and other applications. See [Large-project resource planning](#large-project-resource-planning). |
| Storage | Enough for the installed package plus source data, project state, temporary/intermediate products, outputs, and any selected catalogs. No fixed application-only free-space minimum is enforced. Image History writes each retained step as a snapshot file in the session's own temporary directory. | Use fast local storage for active projects and temporary files. Budget the source set plus every retained calibration, registration, alignment, rejection, drizzle, report, and final-output product; add catalog and repair staging space separately. |
| Display | The project documents 1280 x 720 as a best-effort usability target, not a blanket runtime certification or installer-enforced resolution floor. | 1920 x 1080 is the primary UI design target and gives process controls, image views, and reports more working room. |
| GPU | No discrete GPU model is required by the package contract. A working desktop graphics stack is required for the Qt UI. | Current graphics drivers and compositor support are recommended. The engine's processing runs on the CPU, and video is read and encoded on the CPU. The Processing page's **GPU Acceleration** options are stored but no built-in process uses them; the page says GPU acceleration is planned for a future release and that only extension-provided AI tools, which have their own GPU settings, use a GPU. Do not purchase hardware on the assumption that an image-processing stage is GPU accelerated. |
| Network | Not required for local files, local tools, bundled help, and local processing after installation. | Required for optional downloads, online help fallback, web astrometry, managed catalogs, marketplace/extension retrieval, and other network-backed services. |
| Permissions | Read access to inputs and write access to project, output, and temporary locations. | Prefer ordinary per-user installation and writable user/project locations. Administrator rights are not normally required for the default Windows package. |

These recommendations are operational guidance, not measured throughput guarantees. Actual requirements depend on image size, channel count, sample type, frame count, enabled stages, concurrency, codec behavior, and how many products are retained.

### Threads and memory

AfterNight opens one engine session when it starts, after it has read its settings and before it reads any image, and every image operation of the application runs in that session. The session's thread count and memory limit are fixed for as long as AfterNight runs. The log records them at start as `The engine session is open: <N> threads, a memory budget of <B> bytes`, and **Edit > Settings > Processing** shows them as **In use now**.

| Setting or rule | What AfterNight does | Where to see it |
| --- | --- | --- |
| **Max Threads** (Processing page, **Parallelism**) | **Auto**, the default, uses every logical core of the computer. Otherwise it is a number from 1 to the detected logical cores: the most threads AfterNight's processing uses at once. | **In use now: N threads.**; **Detected logical CPU cores** |
| **Memory Limit** (Processing page, **Memory**) | **Auto**, the default, sets no memory limit of AfterNight's own. A stated limit, in MB, bounds the memory the engine holds for images at once; the page refuses a limit below 1024 MB (`Memory limit of N MB is too low. Recommended minimum is 1024 MB.`). An operation that asks for memory the limit cannot hold is refused, and the refusal names the memory asked for and the limit. | **In use now: N MB.** or **In use now: no memory limit.** |
| A changed limit | Stored at once and applied the next time AfterNight starts. Open images and their history are not affected, and the page shows `The thread limit takes effect the next time AfterNight starts.` or the memory equivalent while a stored limit differs from the running session. | Processing page notices |
| Previews | While a preview is open, previews share threads kept out of the session's width: none with one thread, where a preview waits its turn with every other operation; one thread up to four threads; and a quarter of the threads above four. Those are the threads a preview is sure of: while no other work waits, a preview also uses every thread nothing else holds but one, and gives them back when it ends; work that starts meanwhile waits for them rather than run on fewer threads. With no preview open, all threads are available to processing. Work started while a preview is open regains the kept threads at its next step. There is no separate preview-thread setting. | **Previews:** line on the Processing page |
| Admission | Every operation is admitted before it starts and is given its share of the threads; with one thread, every operation runs in the order it was asked for. Up to 64 requests wait at once without blocking the window; one more is refused with `the admission queue is full`. Background work, such as reading a Dashboard file's facts, runs on one thread and only when one is free. Image Integration's own parallel-cores setting narrows each of its stage operations further and never raises them above **Max Threads**; the viewer and open dialogs keep the session's threads. | The refused action's message |
| AfterNight's own memory | Beside the engine's images, AfterNight bounds what it holds itself: undo snapshots waiting to be written (within the larger of 1 GiB and one copy of the image being edited, at most two at once; an edit waits for room rather than failing), results kept for reuse (1 GiB, entries dropped before anything is refused), and the buffers views paint from (512 MiB; the buffer a visible view needs is never refused). Undo steps are bounded by the history's 20 steps. Memory that Qt, Python, codecs, or mapped files allocate is counted by neither limit. | - |

### Packaged runtime and source-build dependencies

| Component | Packaged release | Source or contributor build |
| --- | --- | --- |
| AfterNight binaries | Included | Build in the platform script order documented in the repository |
| Qt | Qt 6.10.3 runtime is deployed with the package | Install the exact version selected by `QT_VERSION` in `build/premake/afternight-common.lua` |
| Python | Python 3.14.7 is deployed with the package for extensions; the extension hosts run it in processes of their own, and the application process loads no Python interpreter | Installed/deployed by the platform dependency scripts; a separate system Python is not a packaged-runtime prerequisite |
| Image/science/media libraries | The engine's libraries are deployed: OpenFITS, LibTIFF, libpng, libjpeg-turbo, LibRaw with AfterNight's private RawSpeed build, OpenXISF, Exiv2, FFmpeg, single-precision FFTW, GSL, and oneTBB; Highway and JSON for Modern C++ are compiled into the engine. The application adds Qt, KDDockWidgets, SQLite, and the G'MIC library behind its G'MIC adapter. No AfterNight program loads OpenCV, librtprocess, or WCSLIB, and the packages do not include them. | Installed or built by the numbered dependency and third-party scripts |
| Crash reporting | Crashpad is packaged for Linux and Windows MSVC builds | Available on both supported release platforms |
| Offline user guide | Included in release packages; the release jobs fail if the built guide artifact is missing | Build it with the platform documentation helper when preparing a manual package |

Do not install random library versions beside a packaged release to “complete” its runtime. A missing-library startup error indicates an incomplete or damaged deployment; reinstall the matching package or rerun the repository's deploy step for a source build.

### Optional tools, services, and data

| Feature | Additional requirement | Boundary |
| --- | --- | --- |
| StarNet2 integration | StarNet2 command-line tools version 2.5.3 or later, including the files/models required by that installation | AfterNight's version check validates the reported CLI version; it does not prove model availability, hardware suitability, or a successful inference run. |
| DeepSNR integration | DeepSNR command-line tools version 1.2.0 or later, including its required runtime/model files | The version check alone is not an end-to-end execution test. |
| Local ASTAP solving | A compatible ASTAP installation and its own star database | Installed and maintained outside the AfterNight package. |
| Local Astrometry.net solving | `solve-field` plus matching index files | Primarily a Linux/local-tool workflow; index coverage and storage are user-managed. |
| Astrometry.net web solving | Network access and a valid service API key/account where required | Subject to the external service's availability, limits, privacy policy, and terms. |
| Gaia catalog workflows | A selected managed Gaia catalog pack, or network access for supported on-demand retrieval | Full-pack storage is listed below. Download and repair need additional temporary space. |
| SPCC catalog workflows | The supported SPCC catalog or regional cache, plus response/configuration data required by the process | The full catalog is large; regional retrieval trades local storage for network dependency. |
| Extensions and marketplace content | A compatible extension plus any runtime, model, tool, storage, or network requirements declared by that extension | Extension requirements are additive and are not covered by the base-package contract. |
| Online user-guide fallback | Network access and permission to open the published guide | Release packages include offline help; setting `AFTERNIGHT_DISABLE_ONLINE_USER_GUIDE=1` disables the network fallback. |

### Catalog storage matrix

The sizes below come from the current catalog descriptors. They are payload/install estimates, not total project-disk requirements.

| Catalog choice | Advertised content | Download size | Installed size / planning note |
| --- | --- | --- | --- |
| Gaia G <= 12 | About 3.1 million stars | 188 MiB | Smallest pack; not the recommended general starter pack |
| Gaia G <= 14 | About 17 million stars | 1,028 MiB | Recommended starter pack |
| Gaia G <= 16 | About 78 million stars | 4,758 MiB | Budget several GiB plus download/repair staging |
| Gaia G <= 18 | About 305 million stars | 18 GiB | Large local pack; verify free space before download or repair |
| SPCC Full 2026b | 29,648,017 records | 45,539,563,436 bytes (about 42.4 GiB) | 47,690,985,471 bytes installed (about 44.4 GiB); currently advertised as the recommended full pack |
| SPCC regional cache | Only requested sky regions | Grows with requested regions | Reduces the initial local footprint but requires network access for uncached regions |

The catalog selection UI is authoritative if its descriptor differs from this page after a catalog release. Keep enough free space for the final installation **and** temporary download, extraction, verification, backup, or repair files. Do not begin a large managed download on a nearly full volume.

## How to use this reference

### Large-project resource planning

A useful lower-bound calculation for one uncompressed decoded image is:

```text
decoded bytes = width x height x channels x bytes per sample
```

This is only one image buffer. A workflow can also create calibration masters, calibrated frames, registration data, aligned images, normalization/rejection buffers, drizzle data, previews, caches, reports, temporary files, and final outputs. Codecs can decode a small compressed file into a much larger in-memory image. Drizzle and higher-precision processing can increase the working set further.

Plan a representative workflow before importing a large project:

1. Calculate the decoded size of the largest representative frame.
2. Multiply by the number of simultaneously resident frames for the stage being considered, not merely the total frame count.
3. Add distinct intermediate and output products that will be kept on disk.
4. Add selected catalog storage and room for catalog download/repair staging.
5. Leave headroom for the operating system, the UI, codec libraries, parallel tasks, and failure recovery.

If the estimate approaches available memory, reduce concurrency or split work into smaller groups where the process supports it. If the disk estimate approaches free space, move the project or temporary/output locations before execution. Network shares and removable media may add latency, disconnections, locking, or naming constraints; fast local storage is the safer active-work location.

### Check the current machine

Open **About > System Information**. The dialog reports:

- operating-system name, version, architecture/type, build, and uptime;
- Linux display-server and desktop information where available;
- AfterNight version and executable path;
- the engine's version and interface generation, and each of the engine's libraries with the version it was built against and, when it differs, the version loaded;
- CPU, GPU, and memory information;
- connected display information; and
- writable mounted-storage capacity.

Use **Copy to Clipboard** for a support conversation or **Export to File...** for a timestamped text report. The report includes application/build details, OS and hardware information, displays, available memory, writable storage, and paths. Review it before sharing: machine names, paths, hardware details, mount points, and other environment information may be sensitive.

The dialog is diagnostic, not an acceptance test. A reported CPU, GPU, memory value, or writable volume does not guarantee that a particular workflow will finish within a given time or peak-memory limit.

Use [System Information](troubleshooting.md#diagnostic-and-support-evidence) for
machine and build context, the [Session Console](../interface/log-viewer.md) for
the retained visible session, the configured rotating file log for persistent
messages, and a local crash dump only for a native crash on a supported
Crashpad build. These artifacts are complementary; none by itself proves that
the host meets a workflow's unmeasured time or peak-memory needs.

## Implementation behavior and model

AfterNight's base requirement model combines four authorities: package and
build rules define architectures, the AVX2 instruction-set floor, and deployed runtimes; the bundled Qt version
defines an upstream compatibility boundary; optional integrations add their own
version, data, network, and storage needs; and the active workflow determines
decoded-image and intermediate resource use. A value is a hard minimum only
when the current package or implementation enforces it. A CI runner or UI design
target is reported as a verified/recommended target, and calculated image sizes
remain planning lower bounds rather than observed application peaks.

The thread and memory ceilings are a fifth authority, stated by the user: the
Processing page's stored limits become the engine session's when AfterNight
starts, and they bound every operation of that run. A preview reservation is a
share inside that width, not an extra pool of threads.

## Inputs, outputs, persistence, and metadata

| Area | Contract |
| --- | --- |
| Inputs | Local project/image inputs require readable files and supported formats. Optional tools and extensions can impose additional format, model, executable, or data requirements. |
| Outputs | The chosen project, output, catalog, report, and temporary locations must remain writable and have sufficient free space throughout the operation. |
| Persistence | Settings, project state, caches, catalogs, logs, crash data, offline help, and extension data can consume space outside the source-image directory. |
| Network | Local processing does not inherently require a network. Downloads, web solving, online help, managed remote catalogs, and marketplace/extension services do. |
| Privacy | Local inputs remain subject to the chosen workflow. Sending coordinates, images, metadata, diagnostics, or credentials to an external service changes that boundary; review that service and the report contents first. |

## Performance and resource implications

- More CPU cores do not remove serial stages, storage bottlenecks, codec costs, or memory pressure.
- Insufficient memory can cause allocation failure or operating-system paging; paging can make an otherwise valid workflow impractically slow.
- Free-space checks must include intermediates and temporary staging, not only the expected final image.
- Catalog lookup performance depends on catalog choice, storage, cache state, and whether network retrieval is needed.
- AfterNight's own processing, video reading, and video export run on the CPU. Treat GPU use as a property of a specific external tool or extension, not of the base package.
- A memory limit on the Processing page turns a working set that would not fit into a refusal that names the memory asked for and the limit, instead of paging; **Auto** leaves the operating system to page.

## Worked lookup example

- Evidence mode: **Source-and-test-derived plus transparent arithmetic**.
- Question: what lower-bound memory and catalog storage should be considered for
  100 6000 x 4000 RGB float buffers with the Gaia G <= 14 starter pack, or with
  SPCC Full 2026b?

A 6000 x 4000 RGB image held as 32-bit floating-point samples has the following lower-bound decoded size:

```text
6000 x 4000 x 3 x 4 = 288,000,000 bytes, about 274.7 MiB
```

One hundred such buffers would be about 26.8 GiB before application overhead or any masters, aligned images, rejection buffers, drizzle products, previews, or outputs. That is arithmetic, **not** a claim that AfterNight keeps all 100 frames resident; actual residency depends on the stage and implementation.

If the same project also selects the Gaia G <= 14 starter pack, budget its roughly 1,028 MiB download/install footprint plus staging. If it selects SPCC Full 2026b, add about 42.4 GiB of download data and about 44.4 GiB installed, again with extra room for staging or repair. This is why a single fixed “minimum disk” number would be misleading.

## Warnings and limitations

!!! warning "AVX2 is required; fixed capacity minima are not published"
    The x86-64 production targets are compiled for AVX2. Treat unsupported numeric core-count, RAM, free-space, or discrete-GPU minima copied from older notes, issue discussions, or informal recommendations as non-authoritative.

!!! warning "External tools are separate products"
    A successful executable/version check does not validate licenses, models, databases, drivers, credentials, service availability, or end-to-end output. Follow each tool's own requirements and test it with representative data.

!!! warning "Do not infer platform support from Qt alone"
    Qt may support a platform that AfterNight does not package or validate. Current release artifacts are Windows x64 MSVC and Linux x86_64 AppImage only.

## Troubleshooting

| Symptom | Check | Action |
| --- | --- | --- |
| Windows installer or executable will not run | Architecture, AVX2 support, and Windows version | Use an x86-64 CPU with AVX2. Prefer current Windows 11; the bundled Qt boundary is Windows 10 1809 or later. Reinstall the matching MSVC release package. |
| AppImage will not mount or start | Execute bit and FUSE availability | Run `chmod +x` on the AppImage. Install the distribution's FUSE 2 compatibility runtime, or try `--appimage-extract-and-run`. |
| Packaged build reports a missing Qt/Python/library file | Package/deployment completeness | Reinstall the release package. For a source build, repeat the correct platform deploy step after dependencies and main projects are built. |
| Process runs out of memory or pages heavily | Decoded-frame size, active stage, concurrency | Use the sizing formula, close other large applications, lower **Max Threads** or the group size, or move to a machine with more memory. A **Memory Limit** set on the Processing page refuses an operation that would not fit, naming the memory asked for and the limit, instead of paging. |
| A change of **Max Threads** or **Memory Limit** seems to have no effect | The Processing page's restart notice and **In use now** lines | Limits apply at the next start. Restart AfterNight; open images and history are not affected by the change itself. |
| An action reports `the admission queue is full` | Running and waiting work | Let running work finish, or cancel it, then repeat the action. With **Max Threads** at 1, every operation waits its turn. |
| Output or catalog operation reports insufficient space | All selected output, temp, cache, and catalog volumes | Free space or move the affected location. Include download/extraction/repair staging, not just installed size. |
| External tool is rejected | Executable path and reported version | Configure the correct CLI. StarNet2 must report 2.5.3 or later; DeepSNR must report 1.2.0 or later. Then run a representative workflow to validate the complete installation. |
| Web solving, catalog retrieval, marketplace, or online help fails | Network, credentials, policy, and service status | Confirm the feature is allowed, credentials are valid, and the external service is available. Use a local/offline alternative where supported. |
| Support needs environment details | **About > System Information** | Copy or export the report, review it for sensitive paths/environment data, and attach only what is needed. |

## Related pages

- [Installation](../getting-started/installation.md)
- [File Formats](file-formats.md)
- [External Tools settings](../interface/settings-dialog.md#external-tools-settings)
- [Catalogs & Data settings](../interface/settings-dialog.md#catalogs-data-settings)
- [Image Integration performance](../processes/stacking-integration/image-integration.md#performance)
- [Installation troubleshooting](../getting-started/installation.md#troubleshooting)
- [Session Console](../interface/log-viewer.md)
- [Troubleshooting And Diagnostics](troubleshooting.md)
- [Glossary](glossary.md)

## References and provenance

- [AfterNight repository README](https://github.com/emruiz81/afternight/blob/main/README.md) — release/platform and source-build overview; reconciled 2026-07-20.
- [AfterNight shared Premake configuration](https://github.com/emruiz81/afternight/blob/main/build/premake/afternight-common.lua) — x64 configuration and authoritative Qt version; reconciled 2026-07-20.
- [AfterNight main Premake configuration](https://github.com/emruiz81/afternight/blob/main/build/premake/afternight-pm5.lua) — AVX2 compile target and platform-specific compiler flags; reconciled 2026-07-20.
- [AfterNight Windows installer definition](https://github.com/emruiz81/afternight/blob/main/installer/win_installer.iss) — architecture, package source, and install privilege modes; reconciled 2026-07-20.
- [AfterNight Linux packager](https://github.com/emruiz81/afternight/blob/main/installer/linux_packager.sh) — x86_64 AppImage packaging and bundled runtime behavior; reconciled 2026-07-20.
- [Qt 6.10 supported platforms](https://doc.qt.io/qt-6.10/supported-platforms.html) — Qt's Windows and Linux platform boundaries; rechecked 2026-07-20.
- [AppImage FUSE documentation](https://docs.appimage.org/user-guide/troubleshooting/fuse.html) — FUSE requirements, Ubuntu 24.04 package naming, and extract-and-run fallback; rechecked 2026-07-20.
- [AppImage quickstart](https://docs.appimage.org/introduction/quickstart.html) — executable permission and launch procedure; rechecked 2026-07-20.
- `src/AfterNightControls/controls/SystemInformationDialog.cpp` and `SystemInformationDialogUiBuilder.h` — diagnostic fields, copy/export actions, and exported-report contents; `src/AfterNightControls/controls/EngineVersionReport.cpp` — the engine's version and library list shown there and in About.
- `src/AfterNightControls/controls/settings/ProcessingLimits.h`, `ProcessingLimits.cpp`, and `ProcessingSettingsPage.cpp`, with `src/AfterNightApp/app/ApplicationSession.cpp` — the stored limits, the session AfterNight builds from them at start, and the page's notices; `src/AfterNightConsumerRuntime/include/AfterNightConsumerRuntime/EngineSession.h`, `OperationScope.h`, and `ConsumerMemory.h` — admission, the preview reservation, and AfterNight's own memory allowances; `src/AfterNightTests/Test_ControlsDialogs/ProcessingLimitsTest.cpp` and `EngineVersionReportTest.cpp` hold them.
- `build/scripts/tools/package_profiles.txt` and `build/scripts/tools/dependencies.txt` — what the packagers ship and which library each program loads; `src/AfterNightEngine/include/AfterNightEngine/Runtime/CpuCapabilities.h` — the AVX2 floor and the run-time instruction-set choice.
- `src/AfterNightControls/controls/settings/StarNetCliTools.cpp` and `DeepSNRCliTools.cpp` — minimum accepted external-tool versions.
- `src/AfterNightControls/app/GaiaCatalogSettings.h` and `SpccCatalogSettings.h` — catalog descriptors, recommendations, record counts, and storage values.
- Original focused validation evidence and claim limits, and the consistency and diagnostic checks, are kept in the guide's validation records.

Evidence record: this page was reconciled on 2026-07-20 and re-reviewed on 2026-10-18.
