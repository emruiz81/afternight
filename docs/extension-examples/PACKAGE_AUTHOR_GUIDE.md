# Package Author Guide

This guide captures the repository-readiness rules extension authors should follow before a package is published to the official `afternight-extensions` repository.

## Package Layout

Use one directory per extension package:

```text
my_extension/
  extension.json
  my_extension.py
  requirements.lock        # optional, required if dependencies are declared
  wheelhouse/              # optional custom/private artifacts unavailable from PyPI
  assets/                  # optional icons/screenshots
```

`extension.json` must live at the package root. Official release assets are `.tar.zst` archives. Archive fixtures may wrap that directory in a single top-level folder, but the extracted payload must contain exactly one package root with one `extension.json`.

## Template Map

The checked-in examples are package roots that should stay validator-clean:

- `basic_process/` - minimal execute-only process.
- `rt_preview_process/` - native RT preview process with sub-area preview metadata.
- `settings_diagnostics_process/` - settings schema, persisted settings, logs, session metadata, and artifacts.
- `auxiliary_buffers_process/` - mask input, weight input, and output mask buffers.
- `standalone_preview_process/` - a window of its own (`interface: "window"`): non-blocking PySide6 dialog that commits with `ui.commit_image()`.
- `lite_protocol_process/` - a protocol-only lite hosted package that owns its interface (`interface: "window"`), raises its window in `on_raise_window()`, avoids Engine/native-control imports, uses `afternight.ui_protocol` for host result/dialog helpers, and may use a non-GPL license.
- `protocol_panel_process/` - a protocol package whose panel AfterNight draws (`interface: "panel"`): its parameters described with `afternight.ui_protocol`, previewed and applied through `execute_preview()` and `execute()` on the Lite host, under a non-GPL license.
- `protocol_batch_process/` - a protocol package with no interface (`interface: "none"`): its own run processes the image it was started on, reports its progress, commits and closes.
- `contributing_process/` - a protocol package that adds processes while it runs (`afternight.contribute_processes()`): its catalogue process states the entries a person enabled, which AfterNight lists as the extension's processes, and a session started for one reads which from `current_session().contributed_process`.
- `view_requests_process/` - a protocol package with no interface that asks things of its view (`afternight.ui_protocol`'s `overlay_add()`, `set_selection()`, `set_display()`, `undo()` and `redo()`): it draws shapes over the image, reads back the selection area a person draws, zooms, and undoes and redoes the step it committed.
- `locked_wheelhouse_process/` - hash-locked dependency declarations plus the exceptional custom/private wheelhouse path.

## Parameters And Settings

A process's parameters (`get_params()`) and its settings (`get_settings_params()`) are data AfterNight draws.
[The kinds of an extension's panel](../dev/EXTENSION_PANEL_KINDS.md) lists every kind, each field, what it holds,
what its absence means and how long it may be. A list that breaks any of it is refused whole, with the reason, and its
panel is drawn empty.

## Who Draws The Process

`capabilities.interface` in a process's manifest says who draws it:

- `"panel"` - AfterNight draws a panel from the parameters the process describes, in a task page of the main window,
  with the preview, the target, Reset and Apply of a built-in process. The extension's code still runs in its own
  process: its host runs `execute_preview()` for each frame and `execute()` for an Apply over the images AfterNight
  lends, and hears `on_parameters_changed()` and, when the extension has one, `handle_param_action()` for an action
  button. A `runtime` extension is drawn so where the shell takes processes into pages, and in its host's native window
  elsewhere; a `protocol` extension is drawn so in every shell.
- `"window"` - the extension shows a window of its own. A `runtime` extension's host shows its native process window,
  which the extension may hide to show its own toolkit's; a `protocol` extension shows whatever it owns and raises it in
  `on_raise_window()` when a person asks to see it. AfterNight lists the session as a task with Show.
- `"none"` - nothing is drawn. A `protocol` extension's own run at its start (`run()`) does its work, its
  `set_progress()` shown with its task; a `runtime` extension's host shows no window.

Leave it out and AfterNight infers it from the parameters it reads when it finds the package: a `runtime` extension's
list is a panel; a `protocol` extension's is a panel when it states the window's look with `process_window_meta()`, and
a window of its own otherwise. Any other word is refused by name.

The calls that describe a process window are protocol-safe and live in `afternight.ui_protocol`:
`process_window_meta()` states the window's look, `hide_process_window()` puts the process away - its task stays and is
shown again from the rail - and `close_process_window()` closes it, ending the session. `afternight.ui` hands out the
very same functions, so a `runtime` extension imports them as it always did; a `protocol` extension imports
`afternight.ui_protocol` and subclasses `afternight.lite.LiteProcessExtension`:

```python
import afternight.ui_protocol as ui
from afternight.lite import LiteProcessExtension


class Gain(LiteProcessExtension):
    def get_params(self):
        return [ui.process_window_meta(), {"id": "gain", "type": "float", "default": 1.0, "min": 0.0, "max": 4.0}]

    def execute_preview(self, target, src_image, preview_image, params, progress, masks=None, weights=None):
        preview_image.from_numpy(src_image.to_numpy() * float(params["gain"]))

    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, output_masks=None):
        dst_image.from_numpy(src_image.to_numpy() * float(params["gain"]))
```

`protocol_panel_process/` is that package whole. Under the Lite host, `to_numpy()` returns a view of the lent buffer,
valid for the call alone, and `from_numpy()` writes the output AfterNight lent; check `progress.is_cancelled()` in a long
loop, since a cancel reaches the extension when it next reports its progress or asks.

## Processes An Extension Contributes

An extension's processes are the entries of its manifest's `processes[]`. One that keeps a catalogue of its own -
scripts a person enables, models a person downloads - states the entries a person chose while it runs, and AfterNight
lists each as a process of the extension: in its family's menu of the Extensions menu, after the family's own entries,
in a submenu for each `group`; in the Process menu of its `category` when it states one; in the palette, under its
family and its group, with the "extension" tag; among the recent processes and the favourites. Its task is named for
it, not for the package that runs it. Both hosts carry the call, which lives in the protocol half of the SDK:

```python
import afternight

kept = afternight.contribute_processes([
    {"id_suffix": "deepen_shadows", "display_name": "Deepen Shadows", "group": "Tone",
     "category": "tone_brightness", "summary": "Darkens the faint end of the image"},
    {"id_suffix": "lift_highlights", "display_name": "Lift Highlights", "group": "Tone"},
])
```

- The list is the whole list each time: what an earlier call stated and this one does not is withdrawn, and an empty
  list withdraws every entry. The call answers how many entries AfterNight kept - 0 with no application to keep them,
  as in a standalone launch.
- An entry is data, never code. `id_suffix` (letters, digits, `_` and `.`, stated once, and not one of the manifest's
  processes') and `display_name` (at most 80 characters) are required; `group` (at most 40), `category` (one of the
  registry's categories), `summary` (at most 200; the entry's tooltip, and what the palette finds it by) and
  `host_process` (the `id_suffix` of the manifest process whose class runs it; the manifest's first when absent) are
  optional. Texts are plain text: no line break, no control character. At most 512 entries.
- An entry's id is the extension's id, a dot and its `id_suffix`, as a manifest process's is, so a recent process or a
  favourite keeps it by the same key; it has an action of its own and never takes a catalogue action's place.
- A list that breaks any rule is refused whole: the call raises `RuntimeError` with AfterNight's reason, which the
  session console shows too, and the list kept before stays. A list that is not a list of mappings of strings naming
  these fields raises `TypeError` before anything is sent.
- AfterNight keeps the list, so the menus are whole at the next start before any host has started. It goes when the
  extension is removed and stays when it is updated; an entry whose host process an update removed is not listed until
  the extension states its list again.
- Starting an entry starts the extension's host as its `host_process` starts - the same class, environment and
  interface - with the entry named: the session reads it from `afternight.current_session().contributed_process`,
  empty for one of the manifest's processes.

`contributing_process/` is that package whole: a `protocol` package with no interface whose catalogue process states
the entries a person enabled (its setting `enabled`) and whose session started for an entry reads which.

## What An Extension May Do To Its View

An extension reads the view its session is bound to and commits to it. Beyond its pixels it may draw shapes over it, use
its selection area, set how it is shown and ask for an Undo or a Redo - each a request AfterNight validates and may
refuse. Both hosts carry the calls, which live in the protocol half of the SDK:

```python
import afternight.ui_protocol as ui

ids = ui.overlay_add([
    {"points": [(120, 80), (260, 80), (260, 190), (120, 190)], "closed": True, "color": "#ffd54a",
     "label": "Region"},
    {"points": [(40, 300), (400, 320)], "color": (90, 200, 255)},
])
ui.overlay_remove(ids[1:])
area = ui.set_selection()            # on; the next drag on the image draws it
ui.set_display(zoom=200, center=(190, 135))
ui.undo()
```

- A request names no view: it acts on the view the session is bound to, and a session bound to none is refused. For an
  extension drawn in a page, `set_display()` acts on the page's canvas and the shapes are drawn there; the selection
  area and the history are the task's target's, as the page's own image actions are.
- **Shapes** (`overlay_add`, `overlay_remove`, `overlay_clear`): a closed polygon or an open line of points in the
  image's pixels, a colour (`"#rrggbb"`, `"#rrggbbaa"` or an (r, g, b[, a]) tuple), whether a polygon is filled, and a
  label drawn beside it. `overlay_add` answers each shape's id. A session keeps at most 4,096 shapes holding at most
  262,144 points together; a label is at most 120 characters of plain text and every point is finite. The shapes follow
  the view's zoom and scroll, take no click, and are in no saved image, no project and no annotated picture; under Night
  they are drawn red, as everything over an image is. They go when the session ends, when its host stops and when the
  view closes.
- **The selection area** (`set_selection`): the view's one rectangle, the one a crop and a preview's part use. Turned
  on with a rectangle it is set there, kept inside the image; with none, the next drag on the image draws it. A person
  moves and sizes it as always, and the extension is told the rectangle once, when the mouse is released, in its view's
  state (`selection_area`). An extension whose preview runs over a part gets that same rectangle. It is refused while
  another process holds the area, and the session's end turns off an area the session turned on.
- **The display** (`set_display`): a zoom in percent (within the view's range, 1 % to 6,400 %), a point of the image
  to centre, fit to window, a display mode by the name the view's state reports (`"linear"`, `"autostretch"`). A mode
  the view has not is refused by name.
- **The history** (`undo`, `redo`): the view's Undo or Redo, by the path its action takes. Refused with nothing to undo
  or to redo, while a run holds the view, and until the session's own commit has landed - ask again once it has. No
  request clears a history: its steps are the person's.
- AfterNight answers at once and waits for nothing: an Undo is answered once it is asked for, and what the view became
  reaches the extension as every change of its view does. A request refused raises `RuntimeError` with AfterNight's
  reason, which the session console shows too, and changes nothing. With no application - a standalone launch - every
  call does nothing.

`view_requests_process/` is that package whole: a `protocol` package with no interface that draws two shapes and takes
one away, asks for the selection area and reads back the rectangle a person draws, zooms, commits a step, and undoes and
redoes it.

## Manifest Rules

Repository-ready packages must declare:

- identity: `id`, `name`, `version`
- repository metadata: `summary`, `author`, `license`, `publisher_id`
- runtime entry: `type: "python"`, `entry_point`, `category`, `launch_mode`
- host mode: explicit `sdk_backend`
- compatibility versions: `package_format_version: 1`, `protocol_version: 1`, `sdk_version: 1`

Recommended publication metadata:

- `description` for the repository index/detail view
- `repository_url`, `homepage_url`, and `support_url` as HTTPS URLs
- `tags`, `icon`, and `screenshots` where useful

Use `runtime_targets` when the package ships target-specific extension artifacts such as private wheels, custom `.pyd`/`.so` modules, shared libraries, or helper binaries. Current target IDs are:

- `linux-clang-x86_64`
- `windows-msvc-x86_64`

Choose host mode deliberately:

- `sdk_backend: "runtime"` launches `AfterNightExtensionHost`, can use
  Engine-backed modules and native process controls, and requires a
  GPL-3.0-family package license.
- `sdk_backend: "protocol"` launches `AfterNightExtensionHostLite`, can use
  only protocol-safe app/view services, must not import `_afternight_runtime`
  or Engine-backed/native `afternight` modules, should import
  `afternight.ui_protocol` instead of `afternight.ui` - which holds the calls
  that describe a panel AfterNight draws - and may use non-GPL licenses if the
  package and dependency terms allow it.
- `sdk_backend: "rpc"` is reserved until the AfterNight RPC backend ships.

See `docs/dev/EXTENSION_HOST_MODES_AND_LICENSING.md` for the full host-mode
and licensing policy.

## Dependency Rules

The default dependency context is private. Use it unless the package has a clear reason to share an environment.

- `private` - one environment per extension package.
- `shared_group` - one environment shared by related packages with the same dependency fingerprint.
- `shared_host` - read-only host-managed profile, currently intended for curated host profiles such as `scientific_core`.

If `requirements_file` is present:

- `dependencies.pip.require_hashes` must be `true`.
- `requirements.lock` should contain pinned versions with hashes.
- dependencies available from official PyPI should use explicit `dependencies.pip.index_urls` / `extra_index_urls` and hash-locked pins.
- package-local `find_links` paths must exist inside the package and are reserved for extension-specific/private artifacts unavailable from official package indexes.
- use `dependencies.pip.no_deps: true` only when the lock intentionally carries the full install set plus host-staged runtime packages.

New extensions must not redistribute binaries that can be downloaded from official PyPI. Bundle binaries only when they are specific to that extension, such as custom `.pyd`/`.so` modules, private wheels, helper executables, models, or native data with no official package-index source.

## Image Arrays Under The Runtime Backend

With `sdk_backend: "runtime"`, `image.to_numpy()` returns the image's own samples: writable, never copied, and kept alive by the array and by every slice of it after the image object is gone. Edit the array and hand the image to the next call - every native call on the image reads what was written through it.

- `from_numpy()` replaces the samples with new storage. Arrays taken before it keep the old samples; take a new array to see the new ones.
- Native calls are synchronous and hold the GIL. Code that writes an array from another thread, or through a NumPy routine that releases the GIL, while a native call on the same image runs must synchronize itself; that use is outside the shared-array contract.
- A colour image's array is `(height, width, channels)` over planar storage, so its strides are not C-contiguous. Use `np.ascontiguousarray` when a library needs contiguous input, and write the result back with `from_numpy()`.

## Real-Time Preview Scaling

A process with `"preview": true` whose panel AfterNight draws - or a Full hosted one in its native process window -
previews as the built-in processes do:
while a person drags a slider or a curve, frames are shown at the rate the application's Processing page sets, and
letting go runs one settled frame over the whole image. What a frame of the drag hands `execute_preview` is the
process's to state in `capabilities`:

- `"preview_scaling": "full_only"` - the default - hands it the whole image in every frame. Use it whenever the
  preview reads the image as a whole: a statistic, a noise estimate, a normalization, a rank or a blur whose reach
  grows with the image.
- `"preview_scaling": "decimated"` hands it, during a drag, the image reduced to what the preview view paints - each
  pixel the mean of a block - for a preview that changes every pixel by a rule that depends neither on where the pixel
  lies nor on the image's size, as `rt_preview_process/` does.
- `"preview_scaling": "native_region"` with `"preview_region_margin": <0-4096>` hands it, during a drag, the part of
  the image the preview view shows at full scale, grown by the margin, for a preview whose answer at a pixel reads
  nothing farther away than the margin.

A frame over a reduction or a part states its size, and for a part `offset_x`, `offset_y`, `source_width` and
`source_height`, in `src_image.properties`; the manual mask, when one is used, is handed at the frame's size. The
settled frame and every `execute` are handed the whole image, so a scaling that does not suit the preview shows a
wrong drag and nothing else.

The arrays `src_image.to_numpy()` and `preview_image.to_numpy()` return belong to the call that was handed them: the
window lends the next frame of the same size the same buffers, so an array kept past the call reads the next frame's
samples, not the ones it was taken over. Copy what has to outlive the call - `np.array(src_image.to_numpy())` - and keep
derived results rather than the arrays themselves. Each frame's `preview_image` starts with every sample zero and the
source's metadata, whatever the frame before it wrote or set.

## Install-Time Readiness

Repository installs are expected to front-load startup-sensitive work. The Extension Manager creates or repairs the selected environment, installs hash-locked dependencies, precompiles Python bytecode for the environment and package source, writes provenance receipts, primes settings/startup caches, and refreshes installed discovery/menu registration before reporting the package ready.

Package authors should keep first import cheap and deterministic: do not download dependencies, build generated code, compile native modules, or populate heavyweight model caches from `import` time or normal process launch. Put those inputs in the package only when they are extension-specific/private artifacts, or declare them in the lock file so the host can prepare them during install/update/repair.

## Distribution Trust Policy

Initial extension distribution is official-repository-only. User-configured third-party repository URLs, trust prompts, and third-party visual badges remain deferred to the third-party repository/trust roadmap unless explicitly rescheduled.

Every release asset in `index.json` must include `package_hash` with a SHA-256 digest of the downloadable `.tar.zst` file. The installer treats that index asset hash as authoritative and refuses mismatches before extraction.

An official repository package may be unsigned:

- use `signature_state: "unsigned"` and a clear `signature_detail`
- show the package as hash-verified but unsigned in UI/provenance

Verified official packages use Ed25519 detached signatures generated by the
maintainer release workflow. A verified index asset must include
`signature_algorithm: "ed25519"`, `signature_key_id`, and `signature`, and the
AfterNight build must trust that public key id. The installer checks the
SHA-256 package hash before extraction, verifies the detached signature over the
canonical repository asset payload, and then rejects archives whose extracted
manifest id/version does not match the signed index entry.

`signature_state: "failed"` must block installation. `signature_state:
"unknown"` should not be published by the official repository.

## Validation Workflow

For current repository work, build `Test_ProcessFramework` and run the package-focused filters:

```bash
make -C build/make-linux/tests -j4 Test_ProcessFramework
LD_LIBRARY_PATH=bin/clang/release ./bin/clang/release/Test_ProcessFramework --gtest_filter='ExtensionManagerTest.PackageValidatorAcceptsBundledReferenceExtensions:ExtensionManagerTest.PackageValidatorAcceptsAuthorDocumentationExamples'
```

When testing fixture-feed installation/update behavior:

```bash
LD_LIBRARY_PATH=bin/clang/release ./bin/clang/release/Test_ProcessFramework --gtest_filter='ExtensionManagerTest.FixtureRepositoryIndexInstallAndUpdateReplacePackageAtomically'
```

To run each checked-in template on the host its `sdk_backend` names, with both hosts and the Python runtime built and deployed:

```bash
LD_LIBRARY_PATH=bin/clang/release ./bin/clang/release/Test_ProcessFramework --gtest_filter='AuthorTemplateExecution.*'
```

The package is not repository-ready until manifest validation, repository index validation, archive hash verification, safe extraction, provenance receipt writing, and post-install discovery all pass.
