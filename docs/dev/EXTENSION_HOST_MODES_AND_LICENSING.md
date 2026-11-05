# Extension Host Modes and Licensing Boundary

This guide defines the distinction between full hosted and lite hosted AfterNight extensions. It is an engineering policy for maintainers and package authors, not legal advice. Final release wording and package acceptance rules should be reviewed by counsel.

## Summary

AfterNight uses `sdk_backend` in `extension.json` to choose both the SDK surface and the host executable:

| Manifest `sdk_backend` | Host process | Engine access | Native AfterNight controls | Package license posture |
|---|---|---|---|---|
| `runtime` | `AfterNightExtensionHost` | Direct `_afternight_runtime` bindings to `AfterNightEngine` | Allowed | GPL-compatible only |
| `protocol` | `AfterNightExtensionHostLite` | None | Not allowed | Non-GPL-capable, subject to package and dependency licenses |
| `rpc` | `AfterNightExtensionHostLite` plus `AfterNightSdkHost` | Through protocol calls executed in GPL `AfterNightSdkHost` | Not allowed for non-GPL packages | Non-GPL-capable subject to legal review when the RPC backend ships |

`runtime` extensions are full hosted extensions. `protocol` and `rpc` extensions are lite hosted extensions.

## Component License Targets

The main AfterNight application, Engine, full extension host, and every component
that links or imports Engine/native-control code remain GPL-3.0-or-later or
GPL-compatible.

The lite/protocol boundary has a different target:

| Component | License target | Boundary requirement |
|---|---|---|
| `AfterNightExtensionHostLite` | Apache-2.0 | Must not link `AfterNightEngine`, `AfterNightExtensionRuntimeBackend`, `libAfterNightControls`, `libAfterNightProcesses`, KDDockWidgets, or GPL native-control UI code |
| `AfterNightExtensionProtocol` | Apache-2.0 | Must not include Engine/native-control headers or expose Engine/native-control types |
| Protocol-only Python SDK modules (`afternight`, `afternight.protocol`, `afternight.lite`, `afternight.ui_protocol`, `afternight.sequences`) | Apache-2.0 | May expose app/view protocol services only; must not import `_afternight_runtime` or Engine-backed modules |
| Engine-backed/native Python SDK modules (`afternight.runtime`, `afternight.ui`, `afternight.core`, `afternight.io`, `afternight.calibration`, `afternight.registration`, `afternight.stacking`) | GPL-3.0-or-later/GPL-compatible | Runtime/full-host only |
| `AfterNightSdkHost` | GPL-3.0-or-later/GPL-compatible | Future Engine RPC sidecar that links Engine code |

New lite/protocol boundary files should carry file-level SPDX identifiers. If a helper cannot
stay Apache-2.0 because it includes or depends on GPL Engine/native-control code,
split the helper at the protocol boundary instead of sharing that file with the
lite host or protocol-only Python SDK. The Apache-2.0 license text is stored in
`LICENSES/Apache-2.0.txt`; the root `LICENSE` remains the GPL text for the
application and Engine-linked distribution.

## Manifest Selection

Packages do not name host executables directly. The manifest declares
`sdk_backend`, and the Extension Manager chooses the process:

- `runtime` spawns `AfterNightExtensionHost`
- `protocol` spawns `AfterNightExtensionHostLite`
- `rpc` will spawn `AfterNightExtensionHostLite` and verify
  `AfterNightSdkHost` once the RPC backend ships

Use explicit backend metadata even when `runtime` is the current default. This
keeps package review and generated repository metadata unambiguous:

```json
{
  "package_format_version": 1,
  "protocol_version": 1,
  "sdk_version": 1,
  "launch_mode": "single_image",
  "sdk_backend": "protocol"
}
```

## Full Hosted Extensions

Full hosted extensions are for first-party, bundled, or community packages that are GPL-compatible and want the tightest integration with AfterNight processing UI.

Use full hosting when an extension needs any of these:

- direct AfterNight Engine calls through `_afternight_runtime`
- Engine-backed Python modules such as `afternight.core`, `afternight.io`, `afternight.registration`, `afternight.calibration`, or `afternight.stacking`
- native process windows built from `libAfterNightControls` or built-in process surfaces from `libAfterNightProcesses`
- `ParamDef`-driven native controls, `ImageRTPreviewControl`, `TypedProcessControl<T>`, or local `ImageViewControl` preview rendering

Manifest example:

```json
{
  "id": "example_full_extension",
  "name": "Example Full Extension",
  "version": "1.0.0",
  "license": "GPL-3.0-or-later",
  "type": "python",
  "launch_mode": "single_image",
  "sdk_backend": "runtime",
  "package_format_version": 1,
  "protocol_version": 1,
  "sdk_version": 1,
  "entry_point": "example_full_extension",
  "process_class": "ExampleFullProcess"
}
```

Full hosted packages must be GPL-compatible because the full host path links or loads GPL components: the host links `AfterNightEngine`, `AfterNightConsumerRuntime`, `AfterNightPlatform`, the native AfterNight controls (`libAfterNightControls`) and the built-in process library (`libAfterNightProcesses`), and it loads `AfterNightExtensionRuntimeBackend`, whose engine session it borrows.

## Runtime Backend And Session

In a `runtime` session Python reaches the engine through `AfterNightExtensionRuntimeBackend` (`an_extension_runtime_backend`), a module the full host loads by name rather than links. The module links `AfterNightEngine`, `AfterNightConsumerRuntime`, `AfterNightPlatform` and the Python interpreter, and no Qt or product library, and it registers the embedded `_afternight_runtime` module that the Engine-backed `afternight` modules wrap.

- **Five C exports at interface version 2:** `an_runtime_backend_abi_version`, `an_runtime_backend_probe`, `an_runtime_backend_initialize`, `an_runtime_backend_shutdown` and `an_runtime_backend_last_error`. The host refuses a module that reports another interface version.
- **One engine session.** `an_runtime_backend_initialize` opens one engine session and starts no interpreter; a second call while the session is open changes nothing. Every `_afternight_runtime` call runs in that session, and an SDK image is an image asset of it whose `to_numpy()` array is writable and not a copy. `an_runtime_backend_probe` starts no interpreter either.
- **The full host borrows the session.** The host opens no session of its own. After its interpreter starts, it takes the private capsule `afternight._afternight_runtime.session` from `_afternight_runtime._session_capsule()` and checks it with `an::sdk::acceptRuntimeSessionCapsule`: the capsule's name, its record's size, the consumer runtime's interface, the engine version and generation the backend was built against, and that the session still admits work. It keeps the session, lets the capsule go, and lends the session to its native windows before any of them exists. A missing or mismatched capsule stops the host's start. The capsule adds no C export and never leaves the host process.
- **Teardown is ordered.** On every way out the host closes admission, destroys its native windows and host-local views, drains its thread pool, lets its borrow go, finalizes the interpreter, and only then asks the backend to shut down, unloading it only when the backend accepts. While anything still holds the session - a capsule, an image or array, running work - `an_runtime_backend_shutdown` refuses and `an_runtime_backend_last_error` names what holds it.
- **No embedded-Python singleton.** In a `runtime` session the only interpreter is the full host's. The engine embeds no interpreter, and neither the backend nor the host keeps an embedded-Python singleton or a Python-free bootstrap.

`_afternight_runtime.available_pixel_types()` names eight pixel types, one per engine sample type: `int8`, `uint8`, `int16`, `uint16`, `int32`, `uint32`, `float32` and `float64`. `convert_pixel_type()` and the deconvolutions' output type also accept `sint8`, `sint16`, `sint32`, `float` and `double`, without regard to case. The shared-buffer protocol carries four of the eight in every backend - `uint8`, `uint16`, `float32` and `float64` - so the application refuses to lend an image of another type as a buffer, and the full host describes such an image to an extension with an empty pixel-type name.

Package format, protocol and SDK are all version 1. The Extension Manager refuses a manifest or a repository release that states another version of any of the three, and the application refuses, before the session starts, a host that greets it at another protocol or SDK version. The backend's interface version 2 is separate from these three.

## Session Start

The application starts one host process per session, listens on a one-time local endpoint and hands the host its session secret through an inherited pipe. Every session the application's launch coordinator starts - a `runtime` session, whose host owns its process window, and every Lite session - asks its host to connect before it starts the extension's runtime, by `AFTERNIGHT_DEFER_EXTENSION_BOOTSTRAP=1` in the host's environment:

1. The host connects and sends `runtime.hello` with `bootstrap_status_messages` set; the application checks the protocol and SDK versions, the backend and the secret's proof and answers `runtime.hello_ack`, mirroring the flag. The application waits for this within the handshake's bound, five seconds.
2. The host then starts the runtime - for the full host the runtime backend, the interpreter and the extension's import; for the Lite host the interpreter and the import - which a heavy extension's first start from a freshly provisioned environment can make last far longer than the handshake.
3. The full host, once it has loaded the extension, sends `runtime.bootstrap_state_ready`, receives the session's initial state ended by `runtime.bootstrap_state_complete` and runs the extension's launch hook; both hosts then send `runtime.ready`, or `runtime.bootstrap_failed` with the reason when the start fails. The Lite host sends `runtime.ready` once the extension is imported, before its launch hook runs, as the session was ready at the acknowledgement when the import came first.

Until `runtime.ready` the session sends no execute, preview, view notification or heartbeat - only the initial state the full host asks for; the application reads the channel on its own pump, so neither event loop waits for the start. A start that has not ended within 60 seconds is reported unresponsive and the host is stopped; a host that exits or closes the channel during it is reported at once. Heartbeats start at `runtime.ready`.

A host started without the variable - a caller of `ExtensionHostManager::launchSession` that leaves `defer_runtime_bootstrap` off, as the suites' direct sessions do - starts the runtime first and connects after. The launch then waits for the connection while the host process runs, up to the same 60-second start bound, and fails at once, with what the host printed, when the host exits first. Either way the messages and fields are protocol 1's own: package format, protocol and SDK stay at version 1, and an extension sees its import, its launch hook and its requests in the same order whichever way its host starts.

## Lite Hosted Extensions

Lite hosted extensions are for packages that should not link or import AfterNight GPL Engine/native-control code. They run in `AfterNightExtensionHostLite` and communicate with the application through `AfterNightExtensionProtocol`.

Use `sdk_backend = protocol` when the extension:

- implements its own processing with its own code and dependencies
- only needs to list open views, read metadata, subscribe to callbacks, exchange snapshots/buffers, present results, update history, use settings, report progress, and log diagnostics
- owns its UI using PySide6, another Python UI toolkit, a web/helper process, or no UI
- does not need AfterNight Engine operations

Manifest example:

```json
{
  "id": "example_lite_extension",
  "name": "Example Lite Extension",
  "version": "1.0.0",
  "license": "MIT",
  "type": "python",
  "launch_mode": "single_image",
  "sdk_backend": "protocol",
  "package_format_version": 1,
  "protocol_version": 1,
  "sdk_version": 1,
  "entry_point": "example_lite_extension",
  "process_class": "ExampleLiteProcess"
}
```

Lite hosted extensions must not import or rely on:

- `_afternight_runtime`
- `afternight.core`
- `afternight.io`
- `afternight.registration`
- `afternight.calibration`
- `afternight.stacking`
- any future Engine-backed module unless it is explicitly documented as protocol-safe
- native AfterNight controls or C++ SDK types

Engine-domain imports and calls in `protocol` sessions must fail with `UnsupportedBackendError` or `ImportError` that explains the backend restriction.

Recommended Python imports for lite packages:

```python
import afternight
import afternight.ui_protocol as ui
from afternight.lite import LiteProcessExtension


class ExampleLiteProcess(LiteProcessExtension):
    def on_launch(self):
        for view in self.list_views():
            self.log_info(f"Open view: {view.get('name')}")
        ui.show_info("Lite process launched")
```

The application-side manifest loader recognizes `sdk_backend = protocol` and
marks it ready only when the protocol backend is available in the current build.
The host manager resolves protocol sessions to `AfterNightExtensionHostLite`
instead of the full host. Package validation statically rejects direct
`_afternight_runtime` imports and imports of Engine-backed `afternight` modules
for protocol packages.

## Future RPC Extensions

`sdk_backend = rpc` is the planned Engine-capable lite mode. The extension still runs in `AfterNightExtensionHostLite`, but Engine-domain calls are serialized through `AfterNightExtensionProtocol` and executed in `AfterNightSdkHost`, a GPL sidecar owned and supervised by the application.

Manifest example:

```json
{
  "id": "example_rpc_extension",
  "name": "Example RPC Extension",
  "version": "1.0.0",
  "license": "Apache-2.0",
  "type": "python",
  "launch_mode": "single_image",
  "sdk_backend": "rpc",
  "package_format_version": 1,
  "protocol_version": 1,
  "sdk_version": 1,
  "entry_point": "example_rpc_extension",
  "process_class": "ExampleRpcProcess"
}
```

Until the RPC backend ships, `rpc` packages are parsed but not launchable. They should appear as incompatible with a clear diagnostic.

## Allowed Imports By Mode

| Python surface | `runtime` full host | `protocol` lite host | `rpc` future lite host |
|---|---:|---:|---:|
| `afternight`, settings, logging, session metadata | Yes | Yes | Yes |
| `afternight.lite` base classes | Not recommended | Yes | Yes |
| `afternight.ui_protocol` result presentation/dialog/theme helpers | Yes | Yes | Yes |
| `afternight.ui` native process-window helpers | Yes | No | No for non-GPL packages |
| `_afternight_runtime` | Yes | No | No |
| Engine-backed modules: `core`, `io`, `calibration`, `registration`, `stacking` | Yes | No | Through RPC only after the RPC backend ships |
| Extension-owned PySide6/PyQt6 UI | Yes, but full-host package remains GPL-compatible | Yes | Yes |

## Standalone And Dev Mode

Both host executables support a socketless development launch:

```bash
AfterNightExtensionHost --standalone --sdk-backend runtime --extension-package-root <package> --entry-point <module> --process-class <class> --environment-root <env> --runtime-root <PythonLib>
AfterNightExtensionHostLite --standalone --sdk-backend protocol --extension-package-root <package> --entry-point <module> --process-class <class> --environment-root <env> --runtime-root <PythonLib>
```

Full standalone mode is a GPL full-host development path and may load the direct
runtime backend. Lite standalone mode provides mock/empty app-view services only
and must not expose loopback Engine services. Lite extensions should treat
`current_session().is_standalone` as a hint that no real application view
registry is attached.

## Migrating From Full Hosted To Lite Hosted

Move an extension from `runtime` to `protocol` only when it can stop using
AfterNight Engine and native-control APIs:

1. Change `sdk_backend` from `runtime` to `protocol`.
2. Change the package license only if the package code and all dependencies
   permit the new license.
3. Replace `afternight.ProcessExtension`, `RTPreviewExtension`, or
   `WorkflowExtension` with `afternight.lite.LiteProcessExtension` or
   `LiteWorkflowExtension`.
4. Remove imports of `_afternight_runtime`, `afternight.core`,
   `afternight.io`, `afternight.registration`, `afternight.calibration`, and
   `afternight.stacking`.
5. Replace native `ParamDef`/`afternight.ui` controls with extension-owned UI
   such as PySide6, another toolkit, a helper process, or no UI. Use
   `afternight.ui_protocol` for result presentation, theme tokens, and
   host dialogs in lite packages.
6. Use protocol-safe app/view services for view metadata, snapshots, shared
   buffers, settings, progress, logging, and result presentation.
7. Test with `AfterNightExtensionHostLite --standalone` and then through the
   Extension Manager so both mock and real protocol services are exercised.

## Protocol Reuse Requirement

The lite host must reuse `AfterNightExtensionProtocol`. It must not define a second transport or duplicate the full host message stack.

Shared protocol code owns:

- local socket/named-pipe framing
- CBOR message schemas
- session handshake records
- shared-buffer descriptors
- app/view service requests and responses
- error payloads and diagnostics

The protocol library must stay free of Engine and native-control types. Use generic view IDs, image IDs, metadata maps, dimensions, pixel formats, file paths, and shared-buffer descriptors instead of engine images (`an::Image`, `an::consumer::ImageAsset`, `an::ui::ImageDocument`), widget pointers, C++ object handles, or Engine structs.

## Validation Rules

Package validation should enforce:

- `sdk_backend = runtime` requires a GPL-compatible package license.
- Non-GPL packages must use `sdk_backend = protocol` or, after the RPC backend ships, `sdk_backend = rpc`.
- `protocol` packages are rejected if static import scanning finds `_afternight_runtime` or Engine-backed `afternight` modules.
- Non-GPL `protocol` packages should import `afternight.ui_protocol` for
  protocol-safe UI/app helpers; importing `afternight.ui` is treated as the
  native/full-host surface and is not suitable for non-GPL packages.
- `protocol` packages are rejected if they declare native-control-only capabilities.
- `rpc` packages are rejected until `AfterNightSdkHost` support is available in the current build.
- The host manager maps `sdk_backend` to the executable. Packages do not choose executable paths directly.

Build validation should enforce:

- `AfterNightExtensionHostLite` links no AfterNight library but `AfterNightExtensionProtocol`: not `AfterNightEngine`, `AfterNightConsumerRuntime`, `AfterNightPlatform`, `AfterNightExtensionRuntimeBackend`, `libAfterNightControls` or `libAfterNightProcesses`, and not KDDockWidgets.
- `AfterNightExtensionProtocol` links no AfterNight library and does not include Engine headers or native-control headers.
- Both are class `none` in `build/scripts/tools/consumer_targets.txt`: the Lite host depends on the protocol library alone and the protocol library on nothing. `build/scripts/tools/check_build_graph.py` holds the generated projects to those records - it fails a link to an AfterNight library the record does not name, an include directory, source or library of the engine, and a source that reaches an engine header - and fails either target reaching `AfterNightPlatform` or `AfterNightConsumerRuntime`.
- Apache-2.0-targeted lite/protocol source files do not include GPL-only Engine/native-control headers or import `_afternight_runtime`.
- Full host and lite host protocol tests run against the same fixtures.

## Python UI Toolkit Notes

PySide6 is the preferred Qt binding for lite hosted custom UI because Qt for Python is available under LGPL/GPL/commercial terms. PyQt6 is GPL/commercial and is not LGPL, so non-GPL extension authors need an appropriate commercial PyQt license if they choose PyQt6.

References:

- GNU GPL FAQ on plugins, interpreters, and IPC: https://www.gnu.org/licenses/gpl-faq.html
- GNU GPLv3 license text: https://www.gnu.org/licenses/gpl-3.0.en.html
- Apache-2.0 license text in this repo: `LICENSES/Apache-2.0.txt`
- Qt for Python licensing: https://doc.qt.io/qtforpython-6.5/commercial/index.html
- PyQt licensing: https://riverbankcomputing.com/software/pyqt/intro
