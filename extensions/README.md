Bundled AfterNight extensions live in this tree.

Deploy scripts copy the contents of `extensions/` into the deployed application
layout at `<appdir>/extensions/`.

Current reference packages:
- `an_echo_demo`: compatibility-banner ProcessWindow smoke package
- `an_rt_preview_demo`: native RT preview reference package
- `an_custom_ui_demo`: structured native parameter-panel reference package
- `an_standalone_preview_demo`: extension-owned PySide6 workflow window + manual commit reference package
- `an_sequence_workflow_demo`: workflow launch-mode reference package using `WorkflowContext` and `afternight.sequences`

Reference/demo packages are marked with `"development_only": true` in their manifests. Normal extension discovery skips
them so they are not shown, registered, or schema-inspected in production app flows; development and test harnesses can
explicitly opt into them through discovery options.

Marketplace-only first-party packages are maintained outside this repository in
the official extension marketplace repository. They are installed on demand by
the Extension Manager rather than shipped in the application bundle.

## Extension Settings Persistence

Extension settings are persisted by the host through `afternight.Settings`.
For installed extensions, keys are stored in the global settings table under:

```text
extension/<extension_id>/<setting_key>
```

Use `self.settings.get("key", default)`, `self.settings.set("key", value)`,
and `self.settings.remove("key")` from extension code. Values are scoped to
the extension package, shared across all process classes in that package, and
loaded into schema inspection so `get_params()` can reflect saved settings at
launch time.

Native process windows can declare folder/tool requirements in
`ui.process_window_meta(tool_configuration={...})`. The host renders a
Configure button in the bottom action row, validates the selected folder, shows
a status banner, persists the selected path through `afternight.Settings`, and
keeps Apply disabled until validation passes.

The Configure button opens an asynchronous tool-management dialog, not a raw
blocking folder picker. In addition to `settings_key`, `required_files` or
`required_executables`, extensions may provide:

- `download_page_url` or `platform_download_page_urls`: URL opened for initial
  installs. Platform objects use `linux`, `windows`, `macos`, or `default`.
- `download_button_label`, `download_note`, and `install_instructions`: optional
  user-facing text in the management dialog.
- `dialog_size`: optional fixed management-dialog size as `[width, height]`,
  allowing each extension to reserve enough room for its own controls.
- `show_configured_banner`: optional boolean, default `true`. Set to `false`
  to keep the main process window quiet once the tool is valid; the management
  dialog still shows the positive configuration status.
- `update_page_url` or `platform_update_page_urls`: fallback page opened when
  automatic checks are unavailable.
- `update_api_url` or `platform_update_api_urls`: JSON release metadata endpoint
  with an `assets` array containing `name`, `browser_download_url`, `size`, and
  `updated_at`. The host compares those asset names and timestamps with the
  selected folder, reports missing/outdated files, and downloads update assets
  asynchronously into the configured folder.

## Resize-Aware Commits

Normal `execute()` results are expected to match the source image geometry and
pixel layout. This keeps accidental wrong-sized destination buffers from being
accepted by ordinary image filters.

Extensions that intentionally replace the target image with a different size
must commit the final image manually:

```python
ui.commit_image(result, allow_resized_output=True)
```

Only use this from execute/apply paths, not previews. The opt-in flag tells the
host to validate the committed destination buffer as a self-contained image
instead of requiring it to match the source width, height, strides, and pixel
format.

## Extension Host Responsiveness

Extension native process windows run inside `AfterNightExtensionHost`, which is
heartbeat-supervised by the main application. Never block the extension host's
main Qt event loop from UI callbacks: avoid `QDialog::exec()`, static
`QFileDialog::get*()` helpers, long CPU/IO work, or synchronous waits that do
not continue servicing the control channel.

Use asynchronous UI patterns instead: create dialogs on the heap, connect to
`finished`/`accepted`/`rejected`, and show them with `open()`. Move slow work to
the normal execute/preview pipeline, helper processes, or worker threads with
progress/cancel integration. A user leaving a dialog open must not cause missed
runtime heartbeats.
