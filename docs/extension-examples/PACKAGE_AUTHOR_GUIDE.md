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
- `standalone_preview_process/` - non-blocking PySide6 dialog that commits with `ui.commit_image()`.
- `lite_protocol_process/` - protocol-only lite hosted package that avoids Engine/native-control imports, uses `afternight.ui_protocol` for host result/dialog helpers, and may use a non-GPL license.
- `locked_wheelhouse_process/` - hash-locked dependency declarations plus the exceptional custom/private wheelhouse path.

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
  `afternight.ui_protocol` instead of `afternight.ui`, and may use non-GPL
  licenses if the package and dependency terms allow it.
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

A Full hosted process with `"preview": true` previews in a native process window as the built-in process windows do:
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
