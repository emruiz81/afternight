---
kind: reference
summary: >-
  What Using Extensions leaves out: where AfterNight finds packages, what each state means, the two hosts, how a
  session and an environment are run, what an extension receives and returns, what it costs, and every message.
---
# Using Extensions Reference

This page holds what [Using Extensions](using-extensions.md) leaves out to stay readable beside the Extensions window.
Each section can be read on its own. Installing, updating and the trust of a package are on
[Extension Marketplace](extension-marketplace.md) and its reference.

## Where AfterNight finds packages

AfterNight's data folder is `%LOCALAPPDATA%\AfterNight` on Windows and `~/.local/share/AfterNight` on Linux.

| Source on the card | Folder | Put there by |
| --- | --- | --- |
| **Bundled** | `extensions` in the installation folder | The AfterNight installer |
| **Repository** | `extensions\_repository` in the data folder | **Install** on the **Browse** tab |
| **User-installed** | `extensions` in the data folder | **Install local package...**, or by hand |

- **Each package is a folder** directly inside one of these, holding its `extension.json`.
- **One package per identifier.** When two folders declare the same identifier, a user-installed copy wins over one
  from the repository, which wins over a bundled one. The other copy gets no card and no process. The order also
  ranks **Local package** between **User-installed** and **Repository**, but a package installed from a local archive
  lands among the user-installed ones.
- **A package for developers only** declares so in its `extension.json` and is left out, wherever it lies. The example
  packages for authors are of this kind: their presence among AfterNight's files does not make them tools for users.
- **A package AfterNight cannot read** is left out too, with no card and no process. So is one that declares a package
  format, protocol or SDK version other than 1, the versions both hosts and the SDK speak. Installing such a package
  fails with "Only package/protocol/sdk version 1 manifests are supported".
- **Each process a package declares** joins the **Process** menu, at the end of the category it names. A category the
  menu does not have leaves the process out, with a line in the [log file](../reference/glossary.md#log-file).
- **At start,** AfterNight fills the menus from the packages that were ready when it last ran, then looks at every
  package in the background and updates the menus. A process that is still loading is greyed, with
  "This extension action is still loading and cannot be launched yet." Adding or removing a package folder in the
  repository or user folder, and closing the Extensions window, make AfterNight look again.

## What each state means

| The card shows | Because | Its processes | What to do |
| --- | --- | --- | --- |
| **Available** | The package suits this AfterNight and its environment is ready. | Open when the image fits them. | Use them, or change the settings. |
| **Bootstrapping** | AfterNight is preparing the environment, or refreshing a ready one for this build: "Extension environment provisioning is in progress." | Available. A launch waits for the preparation. | Wait, or choose a process. |
| **Install Needed** | The environment does not exist yet: "Extension environment has not been provisioned yet." A changed list of requirements, or another minor version of Python, names a new environment, which starts here. | Available. Choosing one installs the environment first. | Choose a process, or **Install Environment**. |
| **Repair Needed** | The environment was prepared for another build of AfterNight or another revision of the package, or the package's dependencies changed: "Extension dependency fingerprint changed and the environment must be rebuilt." | Available. Choosing one refreshes or rebuilds it first. | Choose a process, or **Repair Environment**. |
| **Validation Failed** | Preparing the environment failed or was interrupted. The card keeps the reason. | Available. Choosing one prepares it again. | Read the reason, then repair or reinstall. |
| **Incompatible** | The package does not suit this AfterNight. | Greyed, with the reason as their tooltip. | Install a release that suits it. No setting overrides it. |
| **Unavailable** | No state of its environment was reported. | Greyed. | Choose **Refresh Status**. |

An environment out of date for this build reads "Extension environment was prepared for another build of AfterNight;
it is refreshed in place before its next launch, without installing anything again." One whose preparation stopped
half-way reads "Extension environment provisioning was interrupted and needs repair before launch.", and one whose
install record is lost reads "Extension install receipt is missing or invalid; repair is required."

The reasons a package is **Incompatible**, the first that applies:

- "Requires AfterNight 1.2.0 or newer. Current AfterNight version is 1.1.0.", with the versions of the case.
- "Extension is incompatible with the bundled Python runtime."
- "Extension does not support the current runtime target."
- "The rpc SDK backend (Lite hosted + RPC) is not available in this build. Lite hosted + RPC extensions require
  AfterNightExtensionHostLite and the deferred AfterNightSdkHost sidecar."
- "Extensions in shared_group '...' declare conflicting dependency fingerprints and cannot share one environment.",
  for every package of the group.

A process that fits its package can still be greyed for the [active image](../reference/glossary.md#active-image):
with no image, with "This process requires an RGB image." or "This process requires a mono image.", or with
"This process supports still images only, not sequences."

## The two hosts

A package's `extension.json` names its `sdk_backend`, which chooses the
[extension host](../reference/glossary.md#extension-host) that runs it. The card shows it as **Host Mode**.

| Host Mode | `sdk_backend` | Runs in | What the package can use |
| --- | --- | --- | --- |
| **Full hosted** | `runtime` | `AfterNightExtensionHost` | AfterNight's image engine directly, and AfterNight's own controls for its window. A package of this kind must be GPL-compatible when it is distributed. |
| **Lite hosted** | `protocol` | `AfterNightExtensionHostLite` | Only the services AfterNight offers for its images and views. It draws its own window and loads neither the engine nor AfterNight's controls. |
| **Lite hosted + RPC** | `rpc` | None | Nothing: the engine service it needs is not part of this AfterNight, so such a package is always **Incompatible**. Do not choose a release whose only kind is this one. |

A Full hosted package reaches the engine through a runtime module installed beside its host. The Lite host never
loads it. Either kind declares whether its processes work on one image, the active image when they open, or run a
workflow over files of their own.

## A session, from launch to close

One host per window
:   Each launch starts a host of its own, with a session of its own. Reading a Lite hosted package's settings at a
    launch, or when AfterNight looks at the packages, can start a short host of its own too.

Its connection
:   AfterNight hands each host a secret for its session. The host must greet it at protocol version 1 and SDK
    version 1, with the backend its package declares, and prove the secret. A host that greets otherwise is refused
    before the session starts. What a host may then ask of AfterNight is limited to its own session and to the
    services of its kind of host.

Its start
:   The host has 5 seconds to connect. It is checked before it starts Python and imports the extension, so the first
    start from a newly prepared environment, which can take far longer than later ones, does not fail the launch.
    The session is usable once the import ends, and a Full hosted window once it has its first state. A start that
    fails reports the host's reason, and one that has not ended within 60 seconds is reported **Unresponsive**.

Its heartbeat
:   Once the session runs, AfterNight asks the host for a heartbeat every 5 seconds. Three unanswered in a row mark
    it **Unresponsive**, with "Extension host missed heartbeat responses for 3 consecutive intervals", and AfterNight
    stops it.

Its cancel
:   A host has 1 second to honour a cancel. After that it fails with
    "Extension host did not honor cooperative cancel within the configured grace period", and AfterNight ends it with
    the programs it started.

Its end
:   A crash, a lost connection, a forced stop, an unanswered heartbeat, a failed launch and an unexpected exit are
    kept for the card's **Last Launch** and **Last Crash / Termination**. When AfterNight closes, it stops every host.

## Environments

The Python of AfterNight
:   An environment is made from the Python installed with AfterNight, never from another Python on the computer.
    AfterNight ignores `PYTHONHOME`, `PYTHONPATH` and your user's own Python packages when it runs it.

Kinds
:   A package declares one. **Private**, the default: an environment for that package alone. **Shared group**:
    packages that declare the same group and the same dependencies share one, and a group whose packages differ is
    refused. **Shared host**: a set AfterNight keeps, to which a package adds no requirement and no package source.
    The one set, `scientific_core`, holds NumPy, SciPy and scikit-image with what scikit-image needs to import -
    lazy_loader, packaging, imageio, tifffile, Pillow and networkx - copied from AfterNight's own Python, not
    downloaded. PySide6 is copied into every environment.

Where they are
:   `ExtensionEnvs` in the data folder. A private environment is `<identifier>\e-<16 characters>\<platform>`; a shared
    one sits under `_shared` or `_host_shared` in a folder named by a 64-character fingerprint, which makes its path
    long.

What names one
:   What it installs: the requirements and their hashes, where they come from, the package's kind of environment, the
    minor version of Python and the platform. A new build of AfterNight, a patch release of Python, a new version of
    the package, or new files of the package that install the same keep the environment.

Refreshed in place
:   The environment records the build of AfterNight and, for a private one, the revision of the package it was last
    prepared for. When either differs, it is refreshed in place before its next launch: pointed at this build's
    Python, with the packages it copies from AfterNight copied again when AfterNight's copies changed, and their
    bytecode compiled. Nothing is installed again. A refresh that fails at a launch leaves the environment as it was,
    and is tried again at the next launch.

When it is prepared
:   In the background after AfterNight starts, for every installed package whose environment is not ready, from a
    package index only while repository networking is allowed. When you choose a process whose environment is not
    ready: that preparation installs from the package's sources whatever the network setting. And with **Install
    Environment** or **Repair Environment**. A process's preview and **Apply** never install anything.

Where packages come from
:   Only from what the package declares: its index addresses, its extra index addresses and its folders or addresses
    of packages. A package with a requirements file must declare their hashes. With no index declared, no index is
    read. A package that declares only extra indexes also reaches the default index of pip.

Bytecode
:   Preparing an environment compiles its Python modules ahead of time, so the package's first start does not. A
    folder it cannot read is skipped and named in the log file, and every other module is still compiled. A
    dependency that cannot be compiled never fails the install, because the extension compiles what it imports when
    it first starts. The package's own modules must compile.

Removed
:   At start, while **Automatically remove unused extension environments** on the **Browse** tab is ticked, an
    environment that a ready one of the same package or shared set replaced goes at once. One no installed package
    uses goes once nobody has written to it for 24 hours. One awaiting repair or being prepared stays. **Clean unused
    environments** removes the unused ones without the wait. Uninstalling a package leaves its environment.

A package copied in by hand
:   It has no install record, so its card offers neither **Install Environment** nor **Repair Environment**. Its
    processes still prepare its environment when you choose them. Install it from the Extensions window to get the
    buttons.

## What an extension receives

Images in shared memory
:   An image crosses to the host through shared memory, not through the connection that carries requests. Each
    buffer states its size, its channels, its [pixel type](../reference/glossary.md#pixel-type), its layout and steps,
    its role - **Source**, **Destination**, **Preview**, **Mask** or **Weight** - and whether the host may only read it
    or also write it. AfterNight checks the sizes and steps, and removes the buffers when the session ends.

What the image is
:   A grey image is `grayscale`. A one-channel image that still carries its
    [Bayer pattern](../reference/glossary.md#bayer-pattern) is sent as that pattern, `bayer_rggb`, `bayer_bggr`,
    `bayer_grbg` or `bayer_gbrg`, as seen from the top-left pixel of the part lent, rather than as `mono`. A color
    image is `linear` while it is linear and `srgb` once it is stretched, once another process has recorded it as
    nonlinear, or when it was read from display values, such as a PNG or JPEG file.

What can be lent
:   Grey and RGB images of 8 or 16-bit unsigned integers, or of 32 or 64-bit floating point. Any other image is
    refused, and the message names what the connection carries: "the shared-buffer protocol carries UInt8, UInt16,
    Float32 and Float64 samples, not ..." for signed and 32-bit integers, "the shared-buffer protocol names the four
    Bayer patterns and no X-Trans mosaic", and "the shared-buffer protocol carries mono and RGB images, not ..." for an
    image with alpha or in another [color model](../reference/glossary.md#color-model).

The image's record
:   Every lent buffer comes with a record of the image, even one that came from no file, so the cards an extension
    sets on a result reach the image. It lists each [header card](../reference/glossary.md#header-card) with the card
    as written, and each [EXIF](../reference/glossary.md#exif) field whole, with its tag number, count, name, label and
    description: a field longer than one card, such as an embedded profile or a thumbnail, is one entry, and comes back
    whole from a result.

The [plate solution](../reference/glossary.md#plate-solution)
:   The record's `astrometry` entry gives the centre of the field as the engine computes it, the rotation as the
    position angle of celestial north, the scale as the geometric mean of the two axes' scales, and the solver, read
    from `PLTSOLVR`, then `PLTSLVR`.

The structural cards
:   `SIMPLE`, `BITPIX`, `NAXIS`, `NAXIS1`, `NAXIS2`, `BZERO` and `BSCALE` describe the buffer, not the file the image
    came from: a one-plane buffer states `NAXIS = 2` and no `NAXIS3`, a three-plane buffer `NAXIS = 3` and
    `NAXIS3 = 3`.

A [preview area](../reference/glossary.md#preview-area)
:   A preview area is lent as that area. Its record states the area's size
    and the solution moved onto it, written as the engine writes one: `RADESYS` without the `EQUINOX` an ICRS frame does
    not read, and `CUNIT1` and `CUNIT2`. A solution that cannot be moved is left out.

A [mask](../reference/glossary.md#mask)
:   The mask linked to the image is lent as it is, whatever its state, as [Masks](../interface/masks.md) says.

## Results and previews

Only what the extension hands over
:   AfterNight keeps the image and its [history](../reference/glossary.md#history). It takes a result only when the
    extension commits it or asks for a new view, so writing into a buffer gives the extension no power over the image. A result may replace the image,
    as one step of its history, open as a new view beside it, or, for a workflow, add the files it lists. Its size,
    type, name, cards and history text are the package's to choose, within what AfterNight checks.

Copied before it shows
:   A committed result is copied into AfterNight's own memory on another thread and shown once the copy is complete, a
    moment after the commit. An `srgb` result becomes a display-encoded RGB image and a `linear` one a linear RGB
    image, so later processes that read what an image is, such as a color process that keeps lightness, treat it
    accordingly.

Nothing half-done
:   A cancelled run commits nothing and leaves its image as it was. A preview a newer one replaces shows nothing. A
    cancelled workflow opens nothing. Closing a package's own dialog without committing commits nothing.

Waiting for the image
:   A preview asked for while another process writes the image waits for that edit and is asked for again. An
    **Apply** asked for then waits too, and goes first once the edit ends, before the waiting preview. While you drag,
    a preview of the part shown at 1:1 while another process writes the image shows nothing, and the next one shows
    it.

What stays after a session
:   The buffers and a workflow's temporary files go with the session. The package's settings and its environment stay.
    What AfterNight keeps about a package - its files, its install record, its environment, its settings, the list of
    ready packages and its recent launches - is kept apart from your images, and opening or refreshing the Extensions
    window changes no image.

## For package authors

What a preview runs over while you drag
:   A Full hosted process states it in the `capabilities` of its `extension.json`. `"preview_scaling": "full_only"`,
    the default, hands `execute_preview` the whole image every time. `"decimated"` is for a preview that changes every
    pixel by a rule that depends neither on where the pixel lies nor on the image's size: it hands it the image
    reduced to what the preview view shows. `"native_region"`, with `"preview_region_margin"` from 0 to 4096 pixels, is
    for a preview whose answer at a pixel reads nothing farther than that margin: it hands it the part of the image the
    view shows, at full scale, grown by the margin.

What it reads
:   A preview handed a reduction or a part reads its width and height and, for a part, `offset_x`, `offset_y`,
    `source_width` and `source_height` in the image's `properties`. The preview after a drag ends, and every
    **Apply**, get the whole image. The [real-time preview](../reference/glossary.md#real-time-preview) example
    among the example packages, and the authors' template of it, declare `decimated`.

The record in Python
:   An image the Full host lends carries its record's entries `fits_header`, `fits_cards`, `exif_header`,
    `exif_entries` and `astrometry` in its metadata beside its header cards. `ImageHandle.clone`,
    `afternight.core.from_numpy` given that metadata, and the runtime operations that take a lent image keep the
    header cards and leave those entries out, so they work on the image of a file with any number of header cards.

Conversions of the runtime
:   A conversion to 8 or 16-bit integers rounds to the nearest code and saturates: in `convert_pixel_type`, in the
    conversion `apply_linear_transform` makes before it transforms, and when a 32-bit floating-point image is written
    to PNG. A signed integer is read and written as a fraction counted from its smallest code. A conversion to a
    32-bit integer saturates: a full-scale sample becomes the largest code and one below zero the smallest.

Metadata and errors of the runtime
:   The metadata dictionary leaves out `SIMPLE`, `BITPIX`, `NAXIS` and the `NAXISn` cards, `EXTEND`, `BZERO` and
    `BSCALE`, which describe a file rather than an image. A refused read raises `RuntimeError`, as
    `load_image() could not read '<path>': <reason>`, with the engine's reason. Detected stars come from the engine's
    detector, each with its `snr`, and a frame that fails to register says why, as
    `Native registration failed for <path>: <reason>`.

Writing a package
:   Manifests, packaging, the SDK and publishing to the repository are explained in the `docs/extension-examples`
    folder of AfterNight's source, not in this guide.

## Safety and privacy

- **No sandbox.** A host runs with your account's rights to your files and the network. AfterNight asks no
  permission for an extension and grants none: the separate program and its private connection protect AfterNight
  from a crash, nothing more. On Windows, every host ends when AfterNight ends.
- **A signature names the author.** A package that checks out against its signature comes from its publisher. That
  says nothing of whether its code is safe, correct or private, or suits your data.
- **Each package is its own.** Its settings, its results, the data it accepts, its licence, the tools it calls and
  the quality of its work follow its own documentation, beyond what AfterNight's connection fixes.
- **Tools and models.** A package that uses a program or a model of its own needs it in place, at the path the
  package asks for, before its first run.
- **Paths and data.** **Reveal Package**, the card's details, the log file and a
  [crash dump](../reference/glossary.md#crash-dump) can show your user name, your folders, file names and the
  package's data. A secret or sensitive field of a package is written as `[redacted]` in its messages. Keep secrets
  and private paths out of pictures, reports and logs you share.

## What it costs

- **Processor.** A host starts Python and, for a Full hosted package, AfterNight's controls and its runtime module.
  That module runs the package's image work in an engine session of the host, which may use every processor thread
  and has no memory budget: the limits of **Settings > Processing** do not apply to it.
- **Memory.** The source, destination and preview buffers, with a mask and a weight when a process takes them, take
  most of it, and a large image can need several buffers of its size at once. Use a smaller preview area, and avoid
  running several extensions on large images at once, when memory or disk is short.
- **Disk.** Each package, its environment, its compiled bytecode and its caches. Cleaning unused environments
  recovers space but never removes one in use. Do not delete a shared-buffer or environment folder by hand while
  AfterNight runs.
- **Waiting.** Looking for packages, reading their status, preparing environments and a settings window's actions
  run in the background. The Installed tab shows the cards it had while it looks again, and runs one operation at a
  time.

## Troubleshooting

The card shows **Install Needed** or **Repair Needed**
:   **Cause.** The environment is missing, or was prepared for another build, revision or set of dependencies.
:   **Fix.** Choose one of the package's processes, or **Install Environment** or **Repair Environment**. Reinstall the
    package if the repair fails.

The card shows **Validation Failed**
:   **Cause.** Preparing or checking the environment failed, and the card kept the reason.
:   **Fix.** Open **Details**, deal with the package, dependency or disk the reason names, then repair or reinstall.

A process of an extension is greyed, or missing from the **Process** menu
:   **Cause.** Its package is **Incompatible** or **Unavailable**, the process is still loading, it does not fit the
    active image, it names a category the menu does not have, or its package is for developers only.
:   **Fix.** Read its tooltip and the card, make an image it fits active, or install a release that suits this
    AfterNight.

**Settings** is missing from the card
:   **Cause.** The environment is not ready, or the package declares no settings, or reading them failed.
:   **Fix.** Bring the card to **Available**, choose **Refresh Status**, and read the banner in **Details**.

**Reveal Package** is greyed
:   **Cause.** The package's folder no longer exists.
:   **Fix.** Choose **Refresh Status**, and reinstall or remove the package.

"Extension runtime bootstrap failed", "Extension runtime did not finish bootstrapping within 60 seconds", "The extension host exited before it connected" or "Timed out waiting for the extension host to connect"
:   **Cause.** The host could not start Python or import the extension, its start outlasted 60 seconds, or it ended or
    stalled before it connected. A slow first start alone causes none of them.
:   **Fix.** Read the reason and the host's output after the message, repair the environment when it names a missing
    or broken dependency, then relaunch.

"Extension host requested unsupported SDK version" or "Peer protocol version is older than the supported host version"
:   **Cause.** The host program comes from another build of AfterNight, and AfterNight refused its greeting. The
    message can also say "newer".
:   **Fix.** Reinstall AfterNight, so that its hosts match it.

"Failed to load the runtime backend module", "Runtime backend module reported interface version" or "The runtime backend's engine session could not be borrowed"
:   **Cause.** The Full host could not load or start the runtime module installed beside it: it is missing, damaged
    or from another build. Lite hosted packages do not use it.
:   **Fix.** Reinstall AfterNight.

A preview or a run is refused with a message that begins "the shared-buffer protocol"
:   **Cause.** The image cannot be lent: a signed or 32-bit integer image, an X-Trans mosaic, or an image with alpha
    or in a color model other than grey or RGB.
:   **Fix.** Convert the image to a type the message names, demosaic it, or make it grey or RGB, then run again.

The run ends and the view does not change
:   **Cause.** The extension never committed a result. Writing into its buffer is not one.
:   **Fix.** Read the Session Console and the package's documentation.

A secret shows in an action's message
:   **Cause.** The package did not mark the field as secret or sensitive, or wrote the value somewhere AfterNight does
    not hide.
:   **Fix.** Stop using the package, remove the secret from anything you share, and tell its author.
