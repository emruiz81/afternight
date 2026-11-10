---
kind: tutorial
summary: >-
  Install the packaged AfterNight application on Windows or Linux, or find the instructions for building it from
  source.
---
# Installation

## Purpose, audience, prerequisites, and expected result

Use this page to choose and install the packaged 64-bit Windows or Linux desktop
application, or to hand off to the contributor build playbook. It is for a new
user, a workstation administrator, or a contributor who needs the difference
between a release package and a deployed source build.

You need a compatible x64 host, permission to write the chosen application
location, and enough storage for both the application and your own image data.
At the end, AfterNight should start from the complete packaged/deployed runtime
tree and **Help > User Guide** should open bundled documentation in a published
release package.

## Reachability and availability

| Item | Current behavior |
| --- | --- |
| Distribution route | Obtain a release asset through the project's published release channel or an administrator-provided mirror. This page does not certify an untrusted third-party copy. |
| Windows packages | x64 Inno Setup builds: `AfterNightUserSetup_1.0.1.exe` (current-user/lowest privilege) and `AfterNightSetup_1.0.1.exe` (administrator/all-users intent). |
| Linux package | `AfterNight-1.0.1-x86_64.AppImage`, built from `bin/clang/release`. It is a portable package, not a repository package-manager installation. |
| Published-release contents | Release automation requires the strict-built HTML artifact, packages the deployed application/runtime tree without the development tools a build leaves beside it, and uploads installer/AppImage assets. |
| Local/manual package difference | Local Inno/AppImage builds include the guide only when `docs/user-guide/site/` exists; published release packaging fails when that site is absent. |
| Source-build route | Use the numbered platform scripts and their platform-specific order from `AGENTS.md`/`README.md`; do not substitute direct Make/MSBuild commands before generation/dependency setup. |

The packaged release targets are Windows x64 (MSVC) and Linux x86_64 (Clang).

## Before you start

- Keep acquisition data and any existing AfterNight output outside the
  application installation folder.
- Close running AfterNight processes before replacing their runtime files.
- Prefer the normal Windows user installer unless workstation policy requires
  the administrator build.
- On Linux, confirm that local policy allows executing an AppImage from the
  selected folder. The repository does not declare a universal distro, FUSE,
  desktop-environment, or kernel compatibility guarantee.
- No installer source enforces a RAM, scratch-disk, GPU, or display minimum.
  Plan those resources from your dataset and use [System
  Requirements](../reference/system-requirements.md) as guidance.
- Network access is required to obtain the package and for online Help fallback,
  but not for opening bundled Help or ordinary local processing. Individual
  catalogs, extension repositories, and external tools have their own needs.

## Procedure

### Windows

1. Choose the current-user package for a normal per-user installation, or the
   administrator package only for an all-users/managed-machine deployment.
2. Close AfterNight and start the complete setup executable. Do not copy a
   single `AfterNight.exe` out of the package; it depends on adjacent deployed
   libraries and runtime folders.
3. Review the destination selected by Inno Setup. Keep the always-created Start
   Menu application shortcut; select the optional desktop shortcut only if
   wanted.
4. If the installer identifies the same version, accept its uninstall prompt to
   reinstall, or cancel without changing the installation. For a lexically
   older recorded version, the installer starts the registered uninstaller and
   continues only when it returns success.
5. Complete Setup and optionally allow its post-install **Launch AfterNight**
   action.
6. Checkpoint: AfterNight opens. Then choose **Help > User Guide**. A published
   release should open `{app}/docs/index.html`; the conditional **AfterNight User
   Guide** Start Menu shortcut exists only when that file was packaged.

### Linux AppImage

1. Place `AfterNight-1.0.1-x86_64.AppImage` in a user-controlled applications
   folder. Keep image projects and scratch data elsewhere.
2. Make the package executable:

   ```bash
   chmod +x AfterNight-1.0.1-x86_64.AppImage
   ```

3. Run it from the file manager or a terminal:

   ```bash
   ./AfterNight-1.0.1-x86_64.AppImage
   ```

4. Checkpoint: the AppRun launcher starts `usr/lib/AfterNight`, beside the
   bundled Python, the extension hosts and the crash handler, with its packaged
   library paths. **Help > User Guide** should find
   `usr/share/doc/AfterNight/index.html` in a published release AppImage.

The included `.desktop` file describes the application, icon, categories, and
`Exec=AfterNight` entry inside the AppImage. The packaging script does not
install that desktop file into the host's application menu by itself.

### Contributor source build

Use the exact numbered scripts for one platform. The common invariant is project
generation, dependencies, third-party libraries, then the platform-specific
main-build/deploy sequence. In particular, MSVC deliberately deploys external
dependencies with `04*` before building AfterNight with `05*`, while Linux
builds main projects with `04*` and deploys with `05*`.

Checkpoint: launch only from the deployed `bin/<toolchain>/<config>/` tree. On
Linux use its generated `run_AfterNight.sh` so runtime paths are set. A source
build is not evidence that a published installer contains the same files.

## Complete control and action reference

| UI label/action | Type and unit | Default | Valid values/constraints | Effect | Enabled, hidden, or ignored rules | Trade-off or side effect | Scope | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Windows user installer | Package choice | Recommended | x64 Windows; current-user write access | Installs the deployed MSVC release tree with `PrivilegesRequired=lowest` | Separate build without `/DADMIN` | Does not request all-users elevation and sets no extra permission on `{app}` | Selected Windows scope | Version recorded under HKCU |
| Windows administrator installer | Package choice | Not selected | x64 Windows; administrator approval | Uses the same application ID with administrator/all-users intent | Built with `/DADMIN` | Elevation and wider machine impact; the current package definition grants the Windows **Everyone** principal full control of `{app}` | Selected Windows scope | Version recorded under HKLM |
| Destination folder | Inno Setup path | `{autopf}/AfterNight` resolved for the privilege context | Writable x64 application location | Sets `{app}` for all runtime files and shortcuts | Standard installer validation applies | Moving individual files later can break dependencies/Help | One installation | Stored by installer/uninstaller |
| Create a desktop icon | Installer task | Off | Off/On | Adds an AfterNight desktop shortcut | Windows only | Additional shortcut only | Current install | Chosen during setup |
| Start Menu application shortcut | Installer action | On | Always created | Opens `{app}/AfterNight.exe` | Windows only | None beyond shortcut creation | Current install | Removed by uninstaller |
| AfterNight User Guide shortcut | Conditional installer action | Created when `{app}/docs/index.html` exists | Requires packaged docs | Opens bundled HTML directly | Omitted when docs were not packaged | Local, offline entry point | Current install | Removed by uninstaller |
| Launch AfterNight | Post-install action | Offered | Not run for silent installs; user may clear/skip | Starts the installed executable without waiting | Windows installer completion page | First launch may expose external-tool/catalog readiness gaps | Current install | Not a product setting |
| AppImage executable bit | Filesystem permission | Depends on download/copy | Owner needs permission to change/execute file | Allows the host to start the AppImage | Linux only | Host AppImage support remains required | One AppImage file | Filesystem metadata |
| AppImage launch | File-manager/terminal action | None | x86_64 package on a compatible host | AppRun sets packaged `PATH`/`LD_LIBRARY_PATH` and starts AfterNight | Linux only | Does not register a system package or auto-install a menu entry | One run | No installer database |
| Numbered source scripts | Contributor action | None | Correct platform shell and completed earlier numbered steps | Produces then deploys native build outputs | Not a normal end-user install | Large downloads/build time; local output is not a signed release | One source tree/config | Build tree |

## Behavior and state model

The Windows installer copies an already deployed release directory: the
application, both extension hosts, the command-line tool, the runtime backend,
the style plugin, the crash handler process, and the libraries they load - the
Visual C++ runtime among them, so no separately installed redistributable is
needed. Every program declares itself long-path aware, so where Windows has long
paths enabled, AfterNight reads and writes files whose paths pass 260
characters. It
excludes logs, PDB files, test and fuzz executables, and the development tools a
build leaves beside the product (benchmark and RAW qualification tools, the
measurement driver, the optional Python adapter, and the opt-in GPU backend). It adds shortcuts, records the version in
the registry scope selected at build time, and registers an uninstaller. For the
administrator variant only, the `[Dirs]` rule grants the Windows **Everyone**
principal full control of `{app}`. The uninstaller removes that directory. User
projects must therefore not live under `{app}`.

The Linux packager copies the release executable and the other deployed
runtime files to `usr/lib`, with the same exclusions plus debug-symbol files,
and links `usr/bin/AfterNight` to the executable. The application looks beside
itself for the bundled Python, the extension hosts and the crash handler, as it
does in a deployed tree. The packager keeps the deployment's `qt.conf`, adds
icons and desktop metadata, optionally
copies the built guide, and creates the AppImage. AppRun modifies runtime search
paths only for that launch. Deleting the AppImage removes the package but not
user data stored elsewhere.

Published release jobs build documentation first and pass the uploaded HTML
artifact to both platform packaging jobs. The Windows jobs use `/DREQUIRE_DOCS`;
the Linux job uses `REQUIRE_DOCS_SITE=1`. This is stronger than a local/manual
package build, where documentation is conditional.

## Inputs, outputs, and metadata

The package input is the deployed release tree, not source files or an isolated
executable. Windows produces an application directory, shortcuts, uninstall
registration, and a version registry value. Linux produces one AppImage with an
internal application tree. Neither installer operation changes astronomical
image samples or metadata.

AfterNight settings, projects, catalogs, extension environments, crash dumps,
and processing outputs have lifetimes and locations outside this page's package
contract. Back them up according to their own pages before an upgrade. Debug
symbol archives are separate release artifacts and are not installed with the
normal package.

## Performance and responsiveness

Installation time and package size depend on storage, antivirus/scanning, and
the deployed runtime. No current installer source publishes a numeric duration
or free-space threshold. Processing performance is dominated by the dataset and
chosen process, not by user-versus-administrator install scope. Put projects and
temporary processing data on suitable fast storage; do not put them inside the
program directory.

## Worked example

- Starting state: one trusted 1.0.1 release package and no running AfterNight
  process.
- Actions: install or mark executable, start the complete package, then open
  **Help > User Guide**.
- Checkpoints: the main window opens; bundled Help resolves before online Help;
  source image folders remain outside the installation directory.
- Limitation: source/workflow evidence proves package composition and routes,
  not compatibility with every Windows policy, Linux distribution, AppImage
  runtime, antivirus product, or desktop environment.

## Warnings and limitations

- Package definitions do not establish code-signing, checksum-publication, or
  third-party-mirror trust guarantees. Obtain assets through an approved release
  channel and follow organizational verification policy.
- Windows version comparison uses the installer's recorded version strings.
  The page does not claim semantic-version comparison or settings rollback.
- The administrator installer broadens installation scope; it does not improve
  image quality or performance.
- The administrator installer grants the Windows **Everyone** principal full
  control of `{app}`. Any local account able to use that ACL can alter
  installed executables or runtime files. Install only on a trusted host,
  follow organizational application-directory policy, and do not store unique
  data there.
- The AppImage is x86_64 and packages many runtime dependencies, but it is not a
  universal Linux compatibility guarantee.
- Local/manual installer builds may omit offline docs. Published release
  packaging is configured to fail instead of publishing without them.
- Do not place projects or unique data under the Windows `{app}` directory; the
  uninstaller removes that tree.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Windows asks to uninstall the same version | The current registry scope contains the same recorded version. | Accept to reinstall, or decline to cancel Setup without proceeding. |
| “Uninstaller not found in registry.” | A recorded installation version exists but its uninstall entry cannot be resolved. | Cancel, repair/remove the stale installation record through administrator-approved Windows tools, then retry the intended installer scope. |
| Setup reports an uninstallation exit code | The prior registered uninstaller returned nonzero. | Preserve its message/log, resolve the old installation failure, and rerun Setup; do not delete random runtime files. |
| Linux reports permission denied | The AppImage lacks its executable bit or the location is mounted `noexec`. | Run `chmod +x` in an executable user-controlled location or follow host policy. |
| AppImage starts but a runtime library/plugin is missing | The package is incomplete, corrupted, or incompatible with the host. | Reacquire the complete release asset; do not reconstruct it from individual `bin` files. Record host/package details for support. |
| **Help > User Guide** opens the web | No bundled candidate opened and online fallback is allowed. | For offline use, reinstall a published package containing the docs artifact. |
| Offline Help is missing from a local/manual package | `docs/user-guide/site/` was absent and the local build did not require it. | Build the guide first and rebuild the package, or use online Help. |

## Related workflow steps

Continue with [First Launch](first-launch.md) to verify the dashboard and safe
defaults, then follow [Quick Start](quick-start.md). Use [Supported
Formats](supported-formats.md) for a beginner choice and [File
Formats](../reference/file-formats.md) for the authoritative codec/precision
matrix. Contributors should follow the repository build playbooks rather than
extending this end-user installation page.
