# Using Extensions

<!-- MEDIA_SLOT: UG-MEDIA-112 -->
<!-- MEDIA_SLOT: UG-MEDIA-113 -->

## Purpose, audience, prerequisites, and expected result

Extensions add optional processes to AfterNight without running Python inside the
main application process. This page is for users and administrators who need to
check whether an installed package is usable, configure it, launch it, or recover
from an environment or host failure. Package discovery, trust, installation,
update, and repository controls are covered on
[Extension Marketplace](extension-marketplace.md).

You need an extension-enabled deployed build. A managed package also needs a
manifest that declares package format, protocol, and SDK version 1 and is
compatible with the current AfterNight version, platform runtime target, Python
runtime, SDK backend, and launch mode. The expected result is an
**Available** installed card and one or more launchable actions under the
**Extensions** menu.

## Reachability and availability

| Item | Current behavior |
| --- | --- |
| Path | Open **Extensions > Manage Extensions...**, then select **Installed**. Ready process actions appear under **Extensions > _extension family_ > _process_**. |
| Enabled/visible when | The manager action is always present. An installed action is registered only from a discovered manifest; it is enabled when the package/environment is available and the active image satisfies that process's launch guards. |
| Supported inputs/state | Bundled, repository-installed, local-package, and user-installed manifests; current platform/runtime metadata; single-image and workflow declarations; optional settings schemas, masks, weights, and generated outputs declared by each process. |
| Outputs/side effects | Installed-status cards, host-managed extension settings, separate host processes, shared image buffers, committed image/history results or workflow outputs, logs, and runtime launch/termination records. Exact outputs remain package-specific. |
| Persistence | Non-secret schema fields with `persist=true` use `extension/<extension_id>/<field_id>` settings. Package files, install receipts, environments, settings-schema caches, ready catalogs, and recent runtime diagnostics live outside the image. Image history receives only a successful host-authorized commit. |
| External dependencies | The bundled AfterNight Python runtime and the selected full/lite host executable. Package-declared dependencies may require approved wheel/index sources during install or repair; normal launch never performs `pip install`. |

## Before you start

- Use [Extension Marketplace](extension-marketplace.md) to inspect package
  identity, compatibility, signature state, hashes, publisher, license, and links
  before installation.
- Close running sessions before uninstalling or replacing a package. The warning
  is advisory; package removal does not negotiate with an already running host.
- Keep enough disk space for the package, its private or shared environment,
  bytecode/cache artifacts, and image buffers. Large images can require multiple
  buffer-sized temporary mappings during execution.
- Treat every extension as executable code. Hosts run as your operating-system
  user. Process isolation and authenticated local channels are not an OS sandbox
  and do not grant or revoke filesystem or network permissions.
- If a package uses an external executable or model, prepare its approved path.
  Do not expose secrets or private paths in screenshots, bug reports, or logs.

## Procedure

### Check, configure, and launch an installed extension

1. Open **Extensions > Manage Extensions... > Installed**. The page may show a
   cached status immediately while current discovery runs in the background.
2. Read the summary banner. It counts **Available**, **Bootstrapping**,
   **Incompatible**, and packages needing attention. If no manifests are found,
   the page states that no bundled or user-installed packages were discovered.
3. Find the package card. Its compact row shows the package name, availability,
   version, source tier, host mode, and extension ID.
4. Select **Details**. Confirm **Package Root**, **Environment Path**,
   **Manifest File**, **Runtime Targets**, **Validation Result**,
   **Install / Repair**, **Last Launch**, and **Last Crash / Termination**.
5. If the card shows **Install Needed** or **Repair Needed**, AfterNight prepares
   the environment itself: in the background after it starts, and when one of the
   package's processes is launched. Use the matching environment action to retry
   it now; a success refreshes discovery, and a failure remains on the
   card/banner with its diagnostic.
6. If **Settings** is present, edit the package-defined fields and select
   **Save**. The action exists only for a non-empty cached schema in Ready state.
7. Close the manager. Open **Extensions > _extension family_ > _process_** and
   select the process action. If the package's environment is not ready yet, a
   **Preparing _extension_** window shows its environment being installed,
   refreshed or repaired, and the process opens when it is ready; **Cancel** stops
   the preparation. If the action is disabled or the preparation fails, inspect
   the tooltip/availability reason or the message and return to the Installed
   card.
8. During execution, use the extension window's own Preview, Apply, cancel, or
   workflow controls. AfterNight accepts image/history changes only through the
   host commit path; merely writing a shared destination buffer does not commit.
9. If launch or execution fails, reopen **Installed**, select **Refresh Status**,
   expand **Details**, and use the recorded validation, launch, or termination
   message to choose repair, configuration, reinstall, or support escalation.

### Remove or reveal a package

1. Close any running session for the package.
2. Select **Uninstall** on its Installed card and confirm. The package files are
   removed asynchronously, the installed-status cache is invalidated, and the
   extension menus are refreshed after success.
3. Use **Reveal Package** instead when you only need to inspect the discovered
   directory. It is disabled if the recorded package root no longer exists.
4. Remove orphaned managed environments separately from
   **Browse > More Actions > Clean unused environments**. Shared environments are
   orphaned only after no installed package references them. An environment that
   a ready one of the same package or shared context replaced is removed
   automatically when AfterNight starts.

## Complete control and action reference

| UI label/action | Type and unit | Default | Valid values/constraints | Effect | Enabled, hidden, or ignored rules | Trade-off or side effect | Scope | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **Extensions > Manage Extensions...** | Menu action | Available | One Extensions dialog | Opens the manager on its routed page | Always present in the top-level Extensions menu | Loads repository/status data when its tab is activated | Application | None |
| **Browse** | Dialog tab | Initial tab | Repository-backed browse page | Opens discovery/install/update/remove controls | Always present; networking can be policy-disabled | May refresh the official repository | Dialog session; see Marketplace | Search/filter/sort are session-only; trust opt-in is persisted |
| **Installed** | Dialog tab | Not loaded until first activation | Discovered package manifests | Loads installed status, environment state, settings actions, and diagnostics | Always present; empty state when no valid manifest is discovered | Discovery and schema/status inspection run in the background | Current installation | Cached status is invalidated by package/environment mutations |
| **Refresh Status** | Button | Idle | One discovery refresh | Rebuilds cards from manifests, environment records, cached schemas, and recent runtime status | Disabled during a status operation | Does not install, repair, update, or uninstall | Installed tab | None |
| Availability and version badges | Read-only status | Current discovery | **Available**, **Bootstrapping**, **Incompatible**, **Install Needed**, **Repair Needed**, **Validation Failed**, or **Unavailable**; optional version | Summarizes launch readiness | Always shown on a valid manifest card | A compact state can hide the underlying diagnostic until Details is opened | Package | Cached until refreshed/invalidated |
| **Install Environment** | Button | Hidden | Managed, non-bundled package with receipt and a Missing environment | Provisions and validates the managed environment, then primes startup caches | Shown only when the card is repairable and state is Missing; startup and launch do the same without it | Can use disk, CPU, and approved dependency sources; no launch occurs until Ready | Referenced private/shared environment | Ready markers, fingerprints, receipt, caches |
| **Repair Environment** | Button | Hidden | Managed, non-bundled package with receipt and Stale or Failed environment | Refreshes an environment prepared for another AfterNight build or package revision in place, otherwise rebuilds/revalidates it, then primes startup caches | Hidden for Ready/Bootstrapping, bundled, manual no-receipt, or directly runnable packages; startup and launch do the same without it | A shared environment repair affects every package using that exact environment; a refresh installs nothing | Referenced private/shared environment | Replaces readiness/failure records and caches |
| **Uninstall** | Destructive button | Available when package root exists | Confirm or cancel | Removes the installed package and refreshes discovery | Hidden/disabled when no removable package root exists; all actions disabled while an operation runs | Running sessions should be closed; settings are not the package files | Package installation | Package removal is durable; orphan environments require separate cleanup |
| **Settings** | Button | Hidden | Ready, non-empty cached settings schema | Opens `_extension name_ Settings` | Shown only when the cached schema state is Ready and non-empty | Fields/actions are package-authored; validate paths and secrets before saving | Extension ID | See schema fields below |
| **Reveal Package** | Button | Available when path exists | Existing package root | Opens the package directory in the platform file manager | Disabled if the root is empty or missing | Can expose private local paths | Package | None |
| **Details** / **Hide details** | Toggle | Collapsed | One card detail panel | Shows/hides paths, runtime targets, validation, repair, launch, and termination fields | Available on each valid manifest card; disabled during a status operation | Paths can be privacy-sensitive | Package card | Dialog session only |
| Package-defined value field | Generated control | Manifest `default` | `bool`, `int`, `float`, `slider`/`int_slider`, `choice`, `string`, `file_path`, or `dir_path`; package-supplied bounds/options/conditions | Edits a shared extension setting | `visible`, `enabled`, `visible_when`, and `enabled_when` can change it dynamically | A bad path/value can keep package operations unavailable | Extension ID + field ID | Saved only when non-action, non-secret, and `persist=true` |
| Package-defined status/console field | Read-only generated control | Manifest value | `status` or `console_output` | Shows package status or streamed action output | Present only when declared | Output may contain paths; sensitive values are redacted from action messages | Settings dialog | Not saved as a user setting |
| Package-defined action/button | Generated asynchronous action | Idle | Package action with optional timeout, autorun, status, and tool configuration | Runs the settings action and can stream progress/console updates or patch non-sensitive fields | Present only when declared; other actions wait while one is active | May launch validation or external-tool discovery; cancellation/timeout is action-specific | Extension settings | Action itself is not persisted; allowed returned field updates may be |
| **Save** | Settings-dialog button | Enabled | Schema must snapshot successfully | Persists eligible fields and closes the settings dialog | Failure shows **Error Saving Settings** and leaves the dialog open | Secret fields and action fields are deliberately excluded | Extension ID | `extension/<extension_id>/<field_id>` |
| **Cancel** | Settings-dialog button | Enabled | Any unsaved edit | Closes without saving the dialog snapshot | Does not undo an already completed asynchronous settings action | Discards unsaved field edits | Settings dialog | None |
| **Reset Tab** | Dialog button | Enabled | Confirm **Reset Extensions** | On Installed, forces a fresh status discovery; on Browse, resets its controls/trust policy as documented on Marketplace | Applies only to the selected tab | Browse reset can disable unsigned installs and cause a repository refresh | Current tab | Browse trust setting is reset; Installed has no setting defaults |
| **Close** | Dialog button | Enabled | Close the Extensions dialog | Closes the dialog; loaded pages save only through their own immediate/settings paths | If a loaded page reports unsaved changes, prompts to discard or cancel | Does not stop extension hosts already running | Dialog | No additional state |

## Behavior and state model

### Discovery and availability

AfterNight scans deployed bundled packages, repository-managed packages, and
user-installed packages. When IDs collide, the higher-precedence source wins:
**User-installed > Local package > Repository > Bundled**. Development-only
reference packages are skipped in normal discovery. A manifest AfterNight cannot
read is skipped too: it gets no card and no menu action. That includes a manifest
that declares a package format, protocol, or SDK version other than 1, the
versions both hosts and the SDK speak. A readable package can be present but
not launchable when its AfterNight/Python/platform target is incompatible, its
backend executable is unavailable, or its managed environment is not Ready.

The Installed page maps environment state to user-facing availability:

| Environment/manifest state | Card state | Next action |
| --- | --- | --- |
| Compatible manifest and Ready environment | **Available** | Configure or launch |
| Provisioning lock/current bootstrap, or a startup refresh or provisioning under way | **Bootstrapping** | Wait, or launch: the launch waits for it |
| Managed environment absent | **Install Needed** | Launch, or **Install Environment**; startup provisions it too |
| Changed lock or Python minor version selects an unprovisioned environment path | **Install Needed** | Launch, or **Install Environment**; startup provisions it too |
| Ready environment prepared for another AfterNight build or another revision of the package | **Repair Needed** | Launch, or **Repair Environment**; startup refreshes it too, in place and without installing anything |
| Existing environment receipt records other dependency inputs | **Repair Needed** | Launch, or **Repair Environment** |
| Stored provisioning/validation failure | **Validation Failed** | Read diagnostic; repair or reinstall |
| Minimum AfterNight version, bundled Python, runtime target, launch mode, or backend not supported | **Incompatible** | Read the card's diagnostic, such as **Requires AfterNight _version_ or newer.**; install a compatible release/build; configuration cannot override it |

A package is Available and launchable only when discovery reports its environment
as Ready. Launching one of a managed package's processes before then prepares its
environment first, in a **Preparing _extension_** window, and opens the process
once the environment is Ready. This rule also applies to manual packages without a
package-manager receipt: a Missing, Stale, or Failed environment is not
launchable until it is prepared. A receipt-less package does not gain the managed
install/repair button; install or reinstall it through the manager to restore
managed lifecycle support.

### Host modes

| Card label | Manifest backend | Current boundary |
| --- | --- | --- |
| **Full hosted** | `runtime` | Runs in `AfterNightExtensionHost`, may use direct Engine bindings and native AfterNight controls, and is intended for GPL-compatible distributed packages. |
| **Lite hosted** | `protocol` | Runs in `AfterNightExtensionHostLite`, uses protocol app/view services, owns any custom UI, and cannot load direct Engine bindings or native AfterNight controls. |
| **Lite hosted + RPC** | `rpc` | Parsed and displayed, but currently incompatible because the `AfterNightSdkHost` Engine RPC sidecar is deferred/unavailable. |

Every launched control gets a separate host process. The app authenticates the
local session with a one-time secret, performs a version/backend handshake at
protocol 1 and SDK 1 (a host that greets it at another version is refused before
the session starts), and supervises it with heartbeat ping/pong
(normally every 5 seconds, unresponsive after three misses). The host connects
and is checked before it starts Python and imports the extension, so a heavy
package's first start from a newly provisioned environment, which can take far
longer than later starts, does not fail the launch: the session becomes usable
when the import ends, and is reported failed with the host's reason when the
start fails, or when it has not ended within 60 seconds. Cooperative
cancellation is followed by forced process-tree termination after the grace
period when necessary. A crash, disconnect, forced stop, unresponsive host,
failed launch, or unexpected exit is retained for the Installed card's recovery
guidance.

A Full hosted package reaches AfterNight's engine through runtime bindings. A
conversion to 8- or 16-bit integer samples rounds to the nearest code - in
`convert_pixel_type`, in the conversion `apply_linear_transform` makes before it
transforms, and when a Float32 image is written to PNG - and a signed integer is
read and written as a fraction counted from its smallest code. A conversion to
a 32-bit integer scales in double precision and saturates: a full-scale sample
becomes the largest code and one below zero the smallest. The metadata
dictionary leaves out the structural cards `SIMPLE`, `BITPIX`, `NAXISn`,
`EXTEND`, `BZERO`, and `BSCALE`, which describe a file rather than an image. A
refused read raises `RuntimeError` naming the binding, the path, and the
engine's reason. Detected stars come from the engine's detector, each with its
signal-to-noise ratio, and a frame that fails to register says why, with the
engine's reason.

### Environment lifecycle

Extensions use the bundled AfterNight Python runtime; system Python is not a
fallback. Dependency context is chosen by the manifest:

- **Private** (default): an environment for one package, named by what it
  installs.
- **Shared group**: packages declaring the same group and dependency fingerprint
  reuse one environment; a conflicting dependency set is rejected.
- **Shared host**: packages opt into a host-owned pinned profile and cannot add
  their own requirements or pip sources to it. The one profile, `scientific_core`,
  provides NumPy, SciPy, and scikit-image with the packages scikit-image needs to
  import (lazy_loader, packaging, imageio, tifffile, Pillow, and networkx), taken
  from the bundled runtime rather than downloaded.

An environment is named only by what it installs: the locked requirements and the
sources they come from, the bundled Python's minor version, and the runtime
target. A new AfterNight build, a Python patch release, a new version of the
package, or new package Python/binaries that install the same keep the
environment; its receipt records the build and the package revision it was last
prepared for, and when either differs the environment is refreshed in place - it
is pointed at this build's Python, the runtime packages it copies from AfterNight
are copied again when AfterNight's copies changed, and their bytecode is compiled
- without installing anything again. A changed lock or Python minor version
selects a new environment path, and the environment is provisioned there when
that path is absent. AfterNight does this by itself: in the background after it
starts, for every installed package (provisioning from an index only while
repository networking is allowed), and when a process is launched before its
environment is ready.
Install/repair can resolve only from the declared and approved sources; normal
execute/preview never installs packages.
An environment that a ready one of the same package or shared context replaced
is removed when AfterNight starts; other unused-environment cleanup removes only
inventory entries no installed package references, after a grace period.
Provisioning also compiles the environment's Python modules ahead of time, so the
package's first start does not compile them; a directory it cannot read is
skipped and named in the application log, and every other module is still
compiled.

### Permissions, services, and shared buffers

The current release has no per-extension permission prompt and no full OS-level
sandbox. The process boundary protects the main application from many extension
crashes, but extension code still has the filesystem/network rights of the user
who launched AfterNight. Install only packages whose code and publisher you
trust, even when a signature verifies package identity.

Host services are scoped to the authenticated session and supported backend.
Large images are transported through validated memory-mapped shared-buffer
descriptors instead of the bounded CBOR control channel. Each descriptor declares
dimensions, pixel type/format, strides, role (**Source**, **Destination**,
**Preview**, **Mask**, or **Weight**), and **ReadOnly** or **ReadWrite** access.
AfterNight validates bounds/strides and owns session cleanup. The UI process keeps
the canonical view and history: it copies/accepts a result only through a valid
commit/presentation request, so buffer access alone does not grant authority to
replace a view. A descriptor states what the image is: a one-channel image that
still carries its Bayer pattern is sent as that pattern (`bayer_rggb`,
`bayer_bggr`, `bayer_grbg` or `bayer_gbrg`, as seen from the top-left pixel of
the lent area) rather than as `mono`, and a colour
image's colour space is `linear` while the image is linear and `srgb` once it
has been stretched, or once another process has recorded it as nonlinear. A
colour image read from display-encoded values, such as a PNG or JPEG file, is
`srgb` too; a grey image is `grayscale`. Only grey and RGB images of 8- or
16-bit unsigned integers or 32- or 64-bit floating point can be lent: a signed
or 32-bit unsigned integer image, an X-Trans mosaic, or an image with an alpha
channel or in another colour model is refused, and the message names what the
protocol carries.

Every lent buffer has a metadata sidecar, even for an image that came from no
file, so the cards an extension sets on a result reach the image. The sidecar
lists each header card with its card record; each Exif field whole with its tag
number, count, name, label, and description, a field longer than one card, such
as an embedded profile or a thumbnail, being one entry that comes back whole
from a result; and the astrometric solution, whose `astrometry` record gives the
field centre the engine computes, the rotation as the position angle of
celestial north, the scale as the mean over both axes, and the solver read from
`PLTSOLVR`, then `PLTSLVR`. Its structural cards (`SIMPLE`, `BITPIX`, `NAXIS`,
`NAXISn`, `BZERO`, and `BSCALE`) describe the buffer, not the file the image was
read from: a single-plane buffer states `NAXIS = 2` and no `NAXIS3`, a
three-plane buffer `NAXIS = 3` and `NAXIS3 = 3`.

A preview area is lent as that area, and its metadata states the area's size and
the astrometric solution moved onto it, written as the engine writes one:
`RADESYS` without the `EQUINOX` an ICRS frame does not read, and `CUNIT1` and
`CUNIT2`. A preview requested while another process is still writing the image
waits for that edit and is requested again; an **Execute** asked for then waits
too, and is sent once the edit lets the image go, before a waiting preview is
sent again. A result an extension commits is copied into AfterNight's own memory
on a worker thread and shown once the copy is complete, a moment after the
commit. A committed `srgb` buffer becomes a display-encoded RGB image and a
`linear` one a linear RGB image, so later processes that read an image's
description, such as a colour process that preserves lightness, treat the result
accordingly.

## Inputs, outputs, and metadata

The component reads manifests, install receipts, environment markers,
settings-schema caches, runtime-status records, and the package directories. It
does not alter image pixels merely by opening or refreshing the dialog.

An extension execution can read a source, mask, weight, metadata snapshot, or
workflow context as declared by that process. A successful full/lite host request
can commit a replacement image/history step, present a new view, or register
workflow files. Pixel type, geometry, naming, metadata, and history description
are package/request-specific and host-validated. Cancellation or failed execution
must not publish an incomplete result: a cancelled execution commits nothing and
leaves its image as it was, a preview superseded by a newer request shows nothing
while the newer one is shown, a cancelled workflow opens nothing, and closing a
package's own dialog without committing commits nothing. Shared buffers and
workflow-temporary state are cleaned with the session; package settings and
managed environments remain.

A Full hosted extension's process window previews as the built-in process
windows do (see [Process Windows](../interface/process-windows.md)): while you
drag one of its sliders or curves it shows previews at the rate
**Edit > Settings > Processing > Updates per second while dragging** set when
the window opened - a change applies to the extension windows opened after it -
and letting go runs one more preview over the whole image, the one the window
rests on. An extension whose package says its preview changes every pixel by itself -
the bundled real-time preview demo and author template do - is handed, while you
drag, the image reduced to what the preview view shows, or the part of it the
view shows at 1:1 and closer, so a drag keeps up on a large frame; any other
extension is handed the whole image every time and shows the rate its preview
allows. **Apply** always runs the extension over the whole image, never over a
preview's. A preview a newer change supersedes, or one an **Apply** or the
window's close stops, shows nothing; so does a drag's preview of the part shown
at 1:1 while another operation is writing the image, and the next one, once the
writing ends, shows it.

Closing an extension's window ends its session and stops its host. That
includes a package that hides its process window to show a dialog of its own
and closes the process window when its dialog closes, as the bundled standalone
preview demo and author template do: the session ends when the dialog is
closed, so the extension can be launched over the same image again. While a
session is running, a second launch of the same extension over the same image
is refused (see **Troubleshooting**). A Full hosted native window that closes
itself after **Apply**, because its package does not keep it open, still
delivers that Apply: AfterNight copies the committed result in and shows it,
as an Image History step when it replaces the image, before it stops the
session.

For package authors: a Full hosted process states what its preview may run over
while a person drags in its manifest's `capabilities`: `"preview_scaling":
"full_only"`, the default, hands `execute_preview` the whole image every time;
`"decimated"` is for a preview that changes every pixel by a rule that depends
neither on where the pixel lies nor on the image's size, and hands it the image
reduced to what the preview view shows; `"native_region"`, with
`"preview_region_margin"` (0 to 4096 pixels), is for a preview whose answer at a
pixel reads nothing farther away than that margin, and hands it the part of the
image the view shows, at full scale, grown by the margin. A preview handed a
reduction or a part reads its size and, for a part, `offset_x`, `offset_y`,
`source_width` and `source_height` in the image's `properties`; the preview
after a drag ends, and every **Apply**, are handed the whole image. A process
that states no scaling is handed the whole image in every preview.

For package authors: an image the Full host lends carries its sidecar's records
(`fits_header`, `fits_cards`, `exif_header`, `exif_entries`, and `astrometry`)
in its metadata beside its header cards. `ImageHandle.clone()`,
`afternight.core.from_numpy()` given that metadata, and the runtime operations
that take a lent image keep the header cards and leave those records out, so
they work on the image of a file with any number of header cards.

## Performance and responsiveness

Discovery, repository refresh, package/environment operations, settings actions,
and installed-status refresh run asynchronously. Cached cards can remain visible
while refresh is in progress. Only one manager operation is active at a time;
additional Browse install/update/remove requests can queue, and cancellation can
optionally clear queued work.

Host startup loads Python and, for Full hosted packages, the native-control
libraries and AfterNight's runtime backend. The backend runs the package's image
work in an engine session of that host process, which may use every processor
thread and has no memory budget: the limits under **Settings > Processing** do
not apply to it. Image memory is dominated by source/destination/preview and
optional mask/weight buffers; a 36 MiB RGB floating-point frame crosses a shared
buffer in about 20 ms each way on the project's Windows test machine. Use
smaller preview areas and avoid concurrent large-image extensions when memory
or disk pressure is high. Cleaning unused
environments recovers disk space but does not remove a referenced environment.
Do not delete active shared-buffer or environment directories manually.

## Worked example

- Evidence mode: Source-and-test-derived.
- Starting state, actions, settings, checkpoints, result, and limitations: An
  approved managed package is already installed. Open **Installed**; its Missing
  environment appears as **Install Needed** with **Install Environment**. Complete
  provisioning and refresh until the card is **Available**. Open **Settings**, set
  a non-secret persisted field, and Save. Launch its process from
  **Extensions > _family_ > _process_**. A successful execution sends a validated
  commit/presentation request, after which AfterNight updates the view/history or
  opens the declared output. If the host instead disconnects, Refresh Status and
  inspect **Last Crash / Termination** before repair/reinstall. This example proves
  the current state machine and UI contract only; no repository package, GUI,
  image-quality result, timing, memory peak, or cross-platform run was executed.

## Warnings and limitations

- A verified signature authenticates the signed package asset; it does not prove
  that its code is safe, correct, private, or suitable for your data.
- Unsigned packages and all local archives are blocked by default. The opt-in and
  confirmation flow is documented on [Extension Marketplace](extension-marketplace.md).
- Full hosted and Lite hosted are process-isolation modes, not security sandboxes.
- **Lite hosted + RPC** is not available. Do not choose a package release whose
  only compatible backend is `rpc`.
- Package-defined settings and outputs are not standardized beyond the host
  schema/protocol. Follow the package's own documentation for its algorithm,
  accepted data, licensing, external tools, and result quality.
- Normal discovery hides `development_only` reference extensions. Their presence
  in the source tree does not mean they ship as user-facing production tools.
- On Windows, a shared environment lies under a long fingerprinted folder in
  AfterNight's local data, and some PySide6 QtQuick plugins in it can exceed the
  path length Windows loads a library from. The libraries NumPy, SciPy, and
  scikit-image load stay within it.
- Opening **Reveal Package**, diagnostics, logs, or crash dumps can expose local
  usernames, paths, file names, and package data.

## Troubleshooting

| Symptom or exact error | Verified cause/enablement path | Recovery | Evidence source |
| --- | --- | --- | --- |
| **No bundled or user-installed extension packages were discovered.** | No valid manifest was found in active discovery roots | Install an approved package, verify `extension.json`, then **Refresh Status** | `ExtensionStatusPage.cpp`; status-page tests |
| Card shows **Install Needed** | Managed package receipt exists but its environment is Missing | Select **Install Environment** and wait for Ready | `ExtensionStatusPage.cpp`; `ManagerInstalledPackageWithMissingEnvironmentStillNeedsInstall` |
| Card shows **Repair Needed** | Dependency/package/host fingerprint no longer matches Ready markers | Select **Repair Environment**; reinstall if repair fails validation | environment manager; stale-environment tests |
| Card shows **Validation Failed** | Manifest or environment validation stored a failure diagnostic | Expand **Details**, fix the named package/dependency/runtime problem, then repair/reinstall | status page; provisioning-failure tests |
| **Settings** is absent | Schema is unavailable, empty, failed, or not cached in Ready state | Reach Ready, **Refresh Status**, then inspect validation/schema diagnostics | status-page/settings-section tests |
| **Reveal Package** is disabled | Package root is empty or no longer exists | Refresh; reinstall or remove the stale package record | `ExtensionStatusPage.cpp` |
| Extension action is absent/disabled | Package is not Ready, backend/launch mode is incompatible, active image fails the descriptor guard, or package is development-only | Read card/tooltip diagnostics; repair, select a compatible target, or install a compatible release | `ExtensionManager.cpp`; manager/registry tests |
| Host becomes **Unresponsive** or disconnects | Heartbeat threshold was missed or the local channel closed | Let AfterNight stop the session, inspect termination diagnostics/logs, reduce workload, then relaunch | host manager; heartbeat/disconnect tests |
| **Last Launch** or the log shows **Extension runtime bootstrap failed**, **Extension runtime did not finish bootstrapping within 60 seconds**, **The extension host exited before it connected**, or **Timed out waiting for the extension host to connect** | The host connected but could not start Python or import the extension (the reason follows the message), its start outlasted 60 seconds, or the host process ended or stalled before it connected; a slow first start alone does not cause any of them | Read the reason and the host output after the message; repair the environment when it names a missing or broken dependency, then relaunch | `ExtensionHostManager.cpp`; `ExtensionHostManagerTest.ARuntimeExtensionWhoseImportOutlastsTheHandshakeLaunchesAndExecutes`; `ExtensionHostManagerTest.AProtocolExtensionWhoseImportOutlastsTheHandshakeLaunchesWithoutWaitingForIt`; `ExtensionHostManagerTest.AHostThatExitsBeforeItConnectsIsReportedWithWhatItPrinted` |
| Execution completes without changing the view | No valid host commit/presentation was received | Review extension logs and package docs; do not treat a written destination buffer as committed output | host manager shared-buffer/commit tests |
| Secret appears in an action message | Package marked the field incorrectly or emitted data outside supported redaction | Stop using the package, sanitize the report, and contact its author; correctly marked secret/sensitive settings are filtered/redacted | settings-section tests |
| A package's files are installed but it has no card and no menu action | Its `extension.json` cannot be read, including one that declares a package format, protocol, or SDK version other than 1 | Install a release of the package made for this AfterNight, then **Refresh Status** | `ExtensionManager.cpp`; `ExtensionRegression.APackageOfAnotherFormatProtocolOrSdkVersionIsRefused` |
| **Last Launch** or the log shows **Extension host requested unsupported SDK version** or **Peer protocol version is older than the supported host version** (or newer) | The extension host executable is from another AfterNight build; the application refuses its greeting before the session starts | Reinstall AfterNight so its hosts match the application | `ExtensionHostManager.cpp`; `ExtensionRegression.AHostThatSpeaksAnotherSdkVersionIsRefused` |
| A Full hosted extension fails to start; **Last Launch** or the log shows **Failed to load the runtime backend module**, **Runtime backend module reported interface version**, or **The runtime backend's engine session could not be borrowed** | The Full host could not load or start the runtime backend installed beside it: the module is missing, damaged, or from another build | Reinstall AfterNight; Lite hosted extensions do not use the module | `HostRuntimeSession.cpp`; `src/AfterNightExtensionHost/main.cpp` |
| A preview or execution is refused with a message beginning **the shared-buffer protocol** | The image cannot be lent: a signed or 32-bit unsigned integer image, an X-Trans mosaic, or an alpha or non-RGB colour model | Convert the image to a supported sample type, demosaic it, or make it grey or RGB, then run again | `SharedBufferManager.cpp`; shared-buffer seam tests |
| A message titled with the extension process's name says **This extension already has an active host session for the selected image.** | A session of the same extension over that image is still running: its window or its own dialog is open, or its work has not finished | Close the extension's window or dialog, or let its run finish, then launch it again; a host that stops responding is stopped by AfterNight after missed heartbeats | `ExtensionHostManager.cpp`; `ExtensionNativeProcessWindowTest.ARequestedCloseEndsTheSessionWhetherOrNotTheWindowWasShown`; `ExtensionRegression.TheStandaloneDemosDialogClosedCommitsNothing` |

## Related workflow steps

- [Extension Marketplace](extension-marketplace.md) — inspect trust/compatibility,
  install or update the package, manage repository networking, and remove/clean.
- [Process Windows](../interface/process-windows.md) — shared target, preview,
  Apply, cancel, progress, mask, and history concepts used by Full hosted native
  extension windows.
- [Masks](../interface/masks.md) — native mask state versus raw mask-buffer
  handoff to extensions.
- [Session Console](../interface/log-viewer.md) — application logs and
  diagnostics; package-specific support may also require its own log files.
- [Settings Dialog](../interface/settings-dialog.md) — global settings including
  **Check for extension updates on startup**.

## References and provenance

- AfterNight's implementation and its focused tests are the primary sources.
- `docs/dev/EXTENSION_HOST_MODES_AND_LICENSING.md` defines the maintained host
  modes - the host each `sdk_backend` uses and the licensing boundary between
  them - and the Full hosted native process-window boundary; implemented source
  and tests take precedence.
- Every bundled extension and author template is held to package format 1,
  protocol 1, and SDK 1.
- `docs/extension-examples/PACKAGE_AUTHOR_GUIDE.md` and
  `docs/extension-examples/README.md` are author-facing package contracts, not a
  substitute for the user procedures on this page.
