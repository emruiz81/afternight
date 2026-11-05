# Extension Examples

These examples are author-facing templates for packages that should pass the same manifest, package-layout, repository-metadata, and dependency-context rules used by AfterNight's first-party extensions.

They are intentionally small. Copy one template into a new package directory, replace the placeholder IDs/names, and then validate the resulting package before publishing or handing it to the Extension Manager.

## Templates

- `basic_process/` - minimal runtime-backed Python process with manifest metadata required for repository readiness.
- `rt_preview_process/` - `ui.RTPreviewProcess` template with native RT preview, sub-area preview metadata, and execute/preview parity.
- `settings_diagnostics_process/` - extension settings schema, persisted settings reads/writes, structured logs, session metadata, and artifact output.
- `auxiliary_buffers_process/` - mask input, weight-map input, and writable output-mask handling.
- `standalone_preview_process/` - extension-owned PySide6 dialog using `open()`, `ui.ImageView()`, `ui.open_image()`, and `ui.commit_image()`.
- `lite_protocol_process/` - non-GPL-capable protocol-only template that selects `AfterNightExtensionHostLite` with `sdk_backend = protocol` and uses `afternight.ui_protocol` for host result/dialog helpers.
- `locked_wheelhouse_process/` - dependency declaration template showing `requirements.lock`, `pip.require_hashes`, and the exceptional package-local `wheelhouse/` path for custom/private artifacts.

## Current Maintainer Validation

Until a standalone package-validator CLI lands, the checked-in validation path is covered by `Test_ProcessFramework`:

```bash
LD_LIBRARY_PATH=bin/clang/release ./bin/clang/release/Test_ProcessFramework --gtest_filter='ExtensionManagerTest.PackageValidatorAcceptsBundledReferenceExtensions:ExtensionManagerTest.PackageValidatorAcceptsAuthorDocumentationExamples'
```

For local fixture-feed install behavior, use:

```bash
LD_LIBRARY_PATH=bin/clang/release ./bin/clang/release/Test_ProcessFramework --gtest_filter='ExtensionManagerTest.FixtureRepositoryIndexInstallAndUpdateReplacePackageAtomically'
```

To run each template on the host its `sdk_backend` names - the full host for `runtime`, the Lite host for `protocol` - with both hosts and the Python runtime built and deployed, use:

```bash
LD_LIBRARY_PATH=bin/clang/release ./bin/clang/release/Test_ProcessFramework --gtest_filter='AuthorTemplateExecution.*'
```

## Repository-Ready Checklist

- `extension.json` includes `id`, `name`, `version`, `summary`, `author`, `license`, `publisher_id`, `type`, `entry_point`, `category`, `launch_mode`, `package_format_version`, `protocol_version`, and `sdk_version`.
- `sdk_backend` is explicit: `runtime` examples use GPL-compatible licenses and launch in the full host; `protocol` examples may use non-GPL licenses only when they avoid Engine/native-control imports and use `afternight.ui_protocol` instead of the native `afternight.ui` surface.
- `entry_point` resolves to a package-local `.py` file or Python package directory.
- `processes[]` entries use safe `id_suffix` values and declare the class names exported by the entry-point module.
- Every package-relative path declared in the manifest exists and stays inside the package root.
- Public Python dependencies available from official PyPI use hash-locked requirements and explicit pip index URLs, not bundled wheels.
- Compiled helpers, private wheels, custom `.pyd`/`.so` modules, or native data that are specific to the extension may be bundled under `binary_vendor_paths` or package-local wheel sources, with `runtime_targets` declared where platform-specific artifacts are present.
- Repository release assets provide an authoritative `package_hash` SHA-256 digest. Manifest-level hashes are generated/display metadata only.
- Initial official repository packages may use `signature_state: "unsigned"` when they are hash-pinned and served from the official repository.

## Choosing a Starting Point

- Use `basic_process/` for execute-only image transforms that need no preview, auxiliary buffers, or dependencies.
- Use `rt_preview_process/` when users should tune parameters against the native preview surface before committing.
- Use `settings_diagnostics_process/` when the extension needs persisted defaults, structured diagnostics, or per-run artifacts.
- Use `auxiliary_buffers_process/` when the process consumes the current selection mask, consumes a weight map, or returns a generated mask.
- Use `standalone_preview_process/` when the stock process shell is not the right interaction model and the extension owns a small PySide6 dialog. Keep dialogs non-blocking with `open()` and signal handlers.
- Use `lite_protocol_process/` when the extension must be non-GPL-capable and can implement processing/UI without AfterNight Engine modules or native controls.
- Use `locked_wheelhouse_process/` only when the package needs a reproducible private or shared dependency context populated from extension-specific/custom wheels unavailable from official PyPI.
