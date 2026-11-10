---
kind: reference
summary: >-
  What a computer needs to run AfterNight, how many threads and how much memory it uses, which optional programs and
  data a workflow needs, and how much memory and disk a large project takes.
---
# System Requirements

This page says what a computer needs to run AfterNight: the systems it is packaged for, the processor, memory, disk,
display and network, the threads and memory it uses, the programs and data some workflows add, and how to estimate a
large project. AfterNight states no smallest number of cores, memory, disk or graphics card: the hard requirement is a
64-bit x86 processor with [AVX2](glossary.md#avx2) on Windows or Linux. The rest depends on your images and on what
you keep of a run. [Installation](../getting-started/installation.md) says how to install it.

## Systems

| System | Package | What the package checks | Built and tested on |
| --- | --- | --- | --- |
| Windows, 64-bit x86 | An installer for your account, which needs no administrator rights, or one for every user of the computer, which does | It refuses any computer that is not 64-bit x86. It checks no Windows version. | Windows Server 2025, where each release is built and its tests run |
| Linux, 64-bit x86 | An AppImage: one file that runs without being installed | It is built for x86_64 and runs on nothing else. | Ubuntu 26.04, where each release is built and its tests run |
| macOS, Windows on Arm, Linux on Arm | None | - | Not built, not tested |

- **Windows.** The Qt 6.10 libraries AfterNight carries support Windows 10 version 1809 or later and Windows 11.
  Windows 11 is the recommended system. Not every Windows version Qt supports is tested, and the installer does not
  refuse an older one.
- **Linux.** An AppImage needs FUSE 2 on most systems: on Ubuntu 24.04 its package is `libfuse2t64`. Where FUSE cannot
  mount the file, start it with `--appimage-extract-and-run`. No other distribution is tested.
- **Another system.** A system Qt supports is not one AfterNight supports: only the packages above are made.

## The computer

| Part | What AfterNight needs | What helps |
| --- | --- | --- |
| Processor | 64-bit x86 with AVX2 and FMA. The engine and every part of the application are built for AVX2, so a processor without it cannot run AfterNight. No number of cores is required. | More cores speed up calibration, registration, stacking and export, where a stage runs in parallel, up to **Max Threads:**. On a processor with the AVX-512 F, BW, DQ and VL subsets, the engine chooses its AVX-512 routines when it runs, where it has them, with nothing to set. |
| Memory | Room for the system, AfterNight, the images open and the work in progress. No amount is required or checked. | Size the memory from your largest frames and your workflow, with room to spare: see [Estimating a large project](#estimating-a-large-project). |
| Disk | Room for the installation, your files, the project, the temporary files, the results and any catalog. No free space is required or checked. Each state the [history](glossary.md#history) keeps is a snapshot file in the session's temporary folder. | A fast local disk for the project and the temporary folder. A network share or a removable disk can be slow, disconnect or lock files. |
| Display | No size is checked. AfterNight is made to stay usable at 1280 x 720. | 1920 x 1080 or more, the size its windows are designed for first, with room for the controls, the views and the reports. |
| Graphics | A working desktop and graphics driver for the windows. No graphics card is required. | A current driver. AfterNight processes images, reads video and codes video on the processor. The **GPU Acceleration** options of the Processing page are kept for a later release, and only the AI tools an extension adds use a GPU, with settings of their own. |
| Network | None, to open, process and save your files, and to read this guide. | Needed for a catalog download, an online plate solve, an extension and the check for its updates, and the online guide when the installed copy cannot be opened. |
| Rights | Reading your files, and writing the project, the results and the temporary folder. | An installation for your account, in folders your account writes. No administrator rights are needed for it. |

## Threads and memory

AfterNight opens one engine session when it starts, after it reads its settings and before it reads an image, and
every image operation runs in it. The session's threads and memory limit stay fixed while AfterNight runs. The
[log file](glossary.md#log-file) records them at the start, as `The engine session is open: <N> threads, a memory
budget of <B> bytes`, and **Edit > Settings... > Processing** shows them under **In use now**.

Max Threads
:   **Auto**, the default, uses every logical core of the computer. With **Auto** off, **Max Threads:** sets the most
    threads AfterNight's processing uses at once, from 1 to the cores the page detects.

Memory Limit
:   **Auto**, the default, sets no memory limit of AfterNight's own, and the system pages when memory runs short.
    With **Auto** off, **Memory Limit:** bounds the memory the engine holds for images at once, in MB. **OK** refuses
    a limit below 1024 MB: "Memory limit of 800 MB is too low. Recommended minimum is 1024 MB." An operation that
    would need more than the limit is refused with a message that names what it asked for and the limit, rather than
    paging.

A changed limit
:   It is stored at once and used from the next start. Open images and their history are not
    touched, and the page says "The thread limit takes effect the next time AfterNight starts.", or the same of the
    memory limit, until AfterNight starts again.

Previews
:   While a [real-time preview](glossary.md#real-time-preview) is open, previews are sure of some threads kept out
    of the session's width: none with one thread, where a preview waits its turn with every other operation, one
    from two to four threads, and a quarter of them, rounded down, above four. While no other work waits, a preview
    also uses every thread nothing else holds but one, and gives them back when it ends. Work that starts meanwhile
    waits for them rather than run on fewer threads. With no preview open, every thread is free for processing, and
    work started while a preview was open regains the kept threads at its next step. The **Previews** line of the
    Processing page says this for your session. No setting changes it.

Admission
:   Every operation is admitted before it starts and given its share of the threads. With one thread, operations run
    one after another in the order they were asked for. Up to 64 requests wait at once without holding up the window,
    and one more is refused: "the admission queue is full". Background work, such as reading the facts of a
    Dashboard file, runs on one thread and only when one is free. Image Integration's **CPU Threads** narrows each of
    its stages and never goes above **Max Threads:**. The viewer and the file windows keep the session's threads.

AfterNight's own memory
:   Beside the engine's images, AfterNight bounds what it holds itself. Undo copies waiting to be written to disk take
    at most the larger of 1 GiB and one copy of the image being edited, two at most at once, and an edit waits for
    room rather than fail. Results kept for reuse take 1 GiB, and entries are dropped before anything is refused. The
    pictures the views paint from take 512 MiB, and the one a visible view needs is never refused. The history keeps
    at most 20 states of an image. Memory that Qt, Python, the codecs or mapped files take is counted by neither
    limit.

## What the packages hold

A package holds everything AfterNight loads. Do not add libraries beside it: a library reported missing at start
means the package is incomplete or damaged, and the cure is to install it again.

| Part | In a package | Built from source |
| --- | --- | --- |
| AfterNight's programs | The application, the two programs that run extensions, the command-line tool and the crash handler | Built in the order of [BUILD.md](https://github.com/emruiz81/afternight/blob/main/BUILD.md) |
| Qt | Qt 6.10.3 | The version the build definitions name |
| Python | Python 3.14.7, for extensions. The programs that run extensions start it in processes of their own, and the application loads none. | Installed by the dependency scripts. No Python of the system is needed. |
| The engine's libraries | OpenFITS, LibTIFF, libpng, libjpeg-turbo, LibRaw with a private build of RawSpeed, OpenXISF, Exiv2, FFmpeg, FFTW in single precision, the GNU Scientific Library and oneTBB. Highway and JSON for Modern C++ are built into the engine. | Installed or built by the numbered dependency scripts |
| The application's libraries | KDDockWidgets, SQLite, cmark-gfm, and the G'MIC library behind its optional adapter | Installed or built by the same scripts |
| Crash reports | Crashpad, on Windows and Linux | Built on both |
| This guide | A copy that opens without a network. A release package always holds it. | Built by the documentation script, before the package |

No AfterNight program loads OpenCV, librtprocess or WCSLIB, and no package holds them. **About > About AfterNight**
lists the libraries of the running copy with their versions.

## Optional programs, services and data

| For | You need | Note |
| --- | --- | --- |
| [Starnet++](../processes/star-operations/starnet.md), [Starnet++ Star Mask](../processes/star-operations/starnet-star-mask.md), Star Reduction's starless step | StarNet2 2.5.3 or newer, with the files and models its installer provides | The **External Tools** page checks the version the program reports, not its models, its runtime or what it produces. |
| DeepSNR | DeepSNR 1.2.0 or newer, with its model weights | As StarNet2. |
| Plate solving with ASTAP | ASTAP and its own star database | Installed and kept up to date outside AfterNight. |
| Local plate solving with Astrometry.net | `solve-field` and its index files, on Linux only | You choose the index files and give them their disk space. |
| Online plate solving | The network and a key of a nova.astrometry.net account | The whole image goes to the service, whose availability, limits and terms apply. Use it only where your rules allow that upload. |
| The internal solver and Photometric Color Calibration | A [Gaia](glossary.md#gaia) pack, downloaded once | Its size is below. A download or a repair needs room beyond it. |
| [SPCC](glossary.md#spcc) | The whole sampled catalog, a regional pack, or the network to fetch the parts it needs, and the response data | The whole catalog is large. A partial cache needs the network for a region it lacks. |
| An extension | What the extension states: a runtime, models, a program, disk or network | Not part of the package's requirements. |
| The online guide | The network | Used only when the installed copy cannot be opened. Setting the environment variable `AFTERNIGHT_DISABLE_ONLINE_USER_GUIDE` to `1` turns it off. |

[Settings](../interface/settings-dialog.md#external-tools) says how to set up StarNet2 and DeepSNR, and
[Catalogs & Data](../interface/settings-dialog.md#catalogs-data) how to install a catalog.

## Catalog sizes

The sizes are those the catalog assistant gives. When a new catalog release changes them, go by the assistant's.
They are what the catalog takes, not what a project needs.

| Catalog | Holds | Download | On disk |
| --- | --- | --- | --- |
| Gaia G <= 12 | About 3.1 million stars | 188 MiB | The smallest pack, not the recommended one |
| Gaia G <= 14 | About 17 million stars | 1,028 MiB | The pack AfterNight recommends |
| Gaia G <= 16 | About 78 million stars | 4,758 MiB | Several GiB, with room for a download or a repair |
| Gaia G <= 18 | About 305 million stars | 18 GiB | A large pack: check the free space before a download or a repair |
| SPCC, whole catalog 2026b | 29,648,017 records | 45,539,563,436 bytes, about 42.4 GiB | 47,690,985,471 bytes, about 44.4 GiB |
| SPCC, regional cache | Only the regions asked for | Grows with the regions | Less disk, and the network for any other region |

Keep room for the installed catalog and for the files a download, a check or a repair writes beside it. Do not start
a large download on a nearly full disk.

## Estimating a large project

One image held whole in memory takes at least

\[ \text{bytes} = \text{width} \times \text{height} \times \text{channels} \times \text{bytes per sample} \tag{1} \]

with 1 byte per sample for 8-bit integers, 2 for 16-bit, 4 for 32-bit integers and floating point, and 8 for 64-bit
floating point. A compressed file can open into a much larger image. A run also makes
[masters](glossary.md#master), calibrated and registered frames, [rejection](glossary.md#rejection) and
normalization data, [drizzle](glossary.md#drizzle) data, previews, reports and results, and a drizzle or a deeper
type takes more again.

1. Work out equation (1) for the largest frame you will use.
2. Multiply it by the number of frames a stage holds at once, not by the number of frames of the project.
3. Add, on disk, every product you keep: calibrated and registered frames, masters, reports and results.
4. Add the catalogs you need, with room for their download.
5. Leave room for the system, the other programs, the threads at work and a run that fails halfway.

When the estimate comes near the memory, lower **Max Threads:** or work on smaller groups where the process allows it.
When it comes near the free space, move the project, the results or the temporary folder to a larger disk before the
run. [Image Integration Reference](../processes/stacking-integration/image-integration-reference.md#what-a-run-costs)
says what a stacking run costs.

As an example, a 6000 x 4000 RGB image in 32-bit floating point takes, by equation (1),
\(6000 \times 4000 \times 3 \times 4 = 288{,}000{,}000\) bytes, about 274.7 MiB. A hundred of them would take about
26.8 GiB, before any master or result.
That is arithmetic: a stage does not hold every frame at once, and what it holds depends on the stage. With the Gaia
G <= 14 pack, add about 1 GiB for its download and its install. With the whole SPCC catalog, add about 42.4 GiB of
download and 44.4 GiB on disk, with room for a repair. This is why no single smallest disk size can be given.

## Check your computer

**About > System Information** shows a report of AfterNight and of the computer:

- **Operating System**: its name, version, kind, build and how long it has run, with the display server and the
  desktop on Linux;
- **Application**: AfterNight's version, the folder it is installed in, its Qt version and its kind of build;
- **Dependencies**: the engine's version and interface generation, and each of its libraries with the version it was
  built against and, when it differs, the version loaded;
- **Hardware**: the processor, the graphics card, and the memory in use and in all;
- **Display**: the displays connected;
- **Storage Devices**: the size, the use and the free space of every disk AfterNight can write to.

**Copy to Clipboard** copies the report, and **Export to File...** writes it as a text file named with the date
and time, such as `AfterNight_SystemInfo_20261110_213000.txt`. The report names your computer's paths, disks and
parts: read it before you share it. It shows what the computer has, not whether a workflow will fit in its time or
memory.

The [Session Console](../interface/log-viewer.md) shows the session's messages, the log file keeps them on disk, and a
[crash dump](glossary.md#crash-dump) is written when AfterNight stops on a crash.
[Troubleshooting And Diagnostics](troubleshooting.md) says what to send with a report.

## Troubleshooting

The Windows installer or AfterNight does not start
:   **Cause.** The computer is not 64-bit x86, its processor lacks AVX2, its Windows is older than Qt supports, or the
    installation is damaged.
:   **Fix.** Use a 64-bit x86 computer with AVX2 on Windows 10 version 1809 or later, Windows 11 preferably, and
    install the Windows package again.

The AppImage does not start, or reports FUSE
:   **Cause.** The file lacks the permission to run, or the system lacks FUSE 2.
:   **Fix.** Run `chmod +x` on the AppImage, then install the system's FUSE 2 package, `libfuse2t64` on Ubuntu 24.04,
    or start it with `--appimage-extract-and-run`.

AfterNight reports a missing Qt, Python or other library
:   **Cause.** The package or the build's folder is incomplete.
:   **Fix.** Install the package again. For a build from source, run the deploy step again after the libraries and
    the programs are built.

The computer runs out of memory or pages heavily
:   **Cause.** The frames, the stage and the threads at work need more memory than the computer has.
:   **Fix.** Estimate the memory as above, close other large programs, lower **Max Threads:** or the size of a group,
    or use a computer with more memory. A **Memory Limit:** refuses work that would not fit, with what it asked for,
    instead of paging.

A changed **Max Threads:** or **Memory Limit:** has no effect
:   **Cause.** The limits are used from the next start.
:   **Fix.** Restart AfterNight, and read **In use now** on the Processing page.

"the admission queue is full"
:   **Cause.** 64 requests already wait for the session's threads.
:   **Fix.** Let running work finish, or cancel it, then ask again. With **Max Threads:** at 1, every operation waits
    its turn.

A result, a temporary file or a catalog runs out of disk space
:   **Cause.** The disk holding it is full, counting the files a run or a download writes on the way.
:   **Fix.** Free space, or move the result, the temporary folder or the catalog to a larger disk, and count the room
    a download or a repair needs.

StarNet2 or DeepSNR is refused
:   **Cause.** The path does not name the program, or it reports a version older than 2.5.3 for StarNet2 or 1.2.0 for
    DeepSNR.
:   **Fix.** Choose the right program on the **External Tools** page, then try it on a small image.

An online solve, a catalog download, an extension or the online guide fails
:   **Cause.** The network, an account key, a policy of your computer or the service itself stops it.
:   **Fix.** Check the network and the key, and use a local solver, an installed catalog or the installed guide.

## Credits

- [Qt 6.10 supported platforms](https://doc.qt.io/qt-6.10/supported-platforms.html), for the Windows and Linux
  versions Qt supports.
- [AppImage: FUSE](https://docs.appimage.org/user-guide/troubleshooting/fuse.html), for FUSE, its Ubuntu package and
  the start without it.
- [AppImage quickstart](https://docs.appimage.org/introduction/quickstart.html), for the permission to run an
  AppImage.
