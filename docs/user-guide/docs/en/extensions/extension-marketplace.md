---
kind: interface
summary: >-
  Browse, install, update and remove extensions from the official repository or a local archive.
---
# Extension Marketplace

## Purpose, audience, prerequisites, and expected result

The Extension Marketplace is AfterNight's package discovery and lifecycle
surface. In the current product it is the official-repository-backed **Browse**
tab inside the Extensions dialog, plus an approved local-archive install route
and optional startup update checks. It is not a commercial store, an open list of
user-configured repositories, or a package-authoring guide.

This page is for users and administrators deciding whether to trust, install,
update, uninstall, or clean up an extension. Repository actions need an
extension-enabled deployed build, compatible release metadata, and network
access not disabled by policy. The expected result is a package installed
atomically into the managed extension directory and handed to the
[Installed status workflow](using-extensions.md).

## Reachability and availability

| Item | Current behavior |
| --- | --- |
| Path | Open **Extensions > Manage Extensions... > Browse**. Startup update checks are controlled by **Settings > General > Check for extension updates on startup**. |
| Enabled/visible when | Browse is always present. Repository Install/Update and Refresh require repository networking; Uninstall and local controls remain available according to installed source and operation state. |
| Supported inputs/state | Validated official repository index/releases/assets; current installed manifests; approved local `.tar.zst` or `.tar` archives; cached repository snapshots; online, offline, and policy-disabled states. |
| Outputs/side effects | Downloaded/activated package, provenance receipt, managed environment and caches, refreshed menus/status, package removal, orphan-environment cleanup, and persisted trust/update-check settings. |
| Persistence | `extensions/allowUnsignedPackages`, `extensions/lastLocalPackageDirectory`, `general/checkExtensionUpdatesOnStartup`, repository snapshot cache, installed packages/receipts, and environments. Search/filter/sort are not persisted. |
| External dependencies | The official repository endpoint/release assets for network installs; package-declared approved dependency sources during environment provisioning; no third-party repository URL UI. |

## Before you start

- Read the expanded card before acting: package/version, compatibility, author,
  publisher, license, provenance/attribution, source/support links, changelog,
  runtime targets, hash, and signature are distinct decisions.
- Back up important extension configuration or package-owned data before update or
  removal. The manager preserves host settings across an atomic update, but an
  extension can define its own external files and migration rules.
- Close running extension sessions before update/uninstall. An already running
  host is a separate process and should not be assumed to reload replaced files.
- Make enough disk space for the archive, staging/backup package, environment,
  precompiled bytecode, and caches. A failed transaction is rolled back, but disk
  exhaustion still produces a failed operation.
- Leave **Allow unsigned extensions** off unless you have independently verified
  the author and exact archive. Local archives are always treated as unsigned by
  this UI.
- For controlled/offline deployments, set either
  `AFTERNIGHT_DISABLE_EXTENSION_REPOSITORY_NETWORK=1` or
  `AFTERNIGHT_EXTENSION_REPOSITORY_OFFLINE=1` before starting AfterNight. Values
  `1`, `true`, `yes`, and `on` are accepted case-insensitively.

## Procedure

### Inspect and install from the official repository

1. Open **Extensions > Manage Extensions... > Browse**. A valid saved snapshot
   can appear immediately while a current official-index refresh runs in the
   background. With no cache, the page shows Loading until fetch completes.
2. Use **Search extensions**, **Filter**, and **Sort**. These operate on the
   loaded snapshot and do not refetch it.
3. Select **Details** on the candidate. Verify the selected release and asset
   match the current AfterNight/Python/platform target and intended host mode.
4. Verify trust state:

   - **Signed**: the asset must declare Ed25519 metadata, a signing key trusted by
     this build, and a signature that verifies the canonical asset metadata/hash.
   - **Unsigned**: blocked unless the global opt-in is enabled and you confirm
     this specific install.
   - **Unknown** or **Signature failed**: installation is blocked.

5. Select **Install**. The action changes to **Installing...** and the page shows
   phase/progress state. You can select **Cancel**; later queued operations can be
   retained or cleared after cancellation.
6. AfterNight downloads to a temporary directory, verifies the required SHA-256,
   verifies a declared signature, extracts under archive safety limits, validates
   the package/manifest/runtime, writes provenance, provisions its environment,
   primes settings/startup caches, precompiles package bytecode, and atomically
   activates the package. Failure before activation removes staging; replacement
   failure restores the previous package.
7. On success, the card is marked installed before the background refresh, the
   Installed-status cache is invalidated, and extension menus are refreshed.
8. Switch to **Installed** and confirm **Available**. Download success alone does
   not prove runtime readiness.

### Update, uninstall, or clean

1. Choose **Filter > Updates Available** or **Sort > Updates Available First**.
   An update is offered when a repository-installed version compares older than
   the selected compatible repository release.
2. Expand **Details**, review changelog/compatibility/trust again, and select
   **Update to _version_**. Update uses the same staged validation/provisioning and
   atomic replacement path; host-persisted extension settings remain.
3. To remove a repository/user installation, select **Uninstall** and confirm.
   Bundled packages and a card explicitly classified as Local package cannot be
   removed from that repository entry. Use the Installed card or the appropriate
   package owner/source when the Browse action is unavailable.
4. After removal, select **More Actions > Clean unused environments** only when
   you also want to remove unreferenced private/shared environment directories.
   The cleanup does not remove environments still referenced by installed
   packages.

### Install an approved local archive

1. Enable **Allow unsigned extensions** and confirm **Allow Unsigned
   Extensions?**. This setting persists and leaves a warning banner visible.
2. Select **More Actions > Install local package...** and confirm the additional
   unsigned-local-package warning.
3. Choose one `.tar.zst` or `.tar` archive. The selected directory is remembered.
4. The archive follows local validation, environment, cache, bytecode, staging,
   and atomic activation rules. It is not authenticated by an official repository
   signature. Switch to **Installed** to verify readiness.
5. Disable **Allow unsigned extensions** again when the controlled task is done.

## Complete control and action reference

| UI label/action | Type and unit | Default | Valid values/constraints | Effect | Enabled, hidden, or ignored rules | Trade-off or side effect | Scope | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **Extensions > Manage Extensions... > Browse** | Menu/tab route | Browse is initial tab | One Extensions dialog | Opens marketplace/package management | Always visible | Can use cache and start a background official-repository refresh | Application/dialog | Loaded snapshot cached separately |
| **Search extensions** | Text filter | Empty | Name, author, or tag text; case-insensitive match | Filters loaded cards | Remains usable during background refresh/operations | No network request; can create an empty-results state | Current snapshot | Session only |
| **Filter** | Choice | **All Extensions** | **All Extensions**, **Installed Only**, **Updates Available**, **Incompatible**, **Official Extensions** | Limits card states/publishers | Local operation remains independent | **Official Extensions** means trusted official signing key or `publisher_id=afternight`; it is not by itself signature verification | Current snapshot | Session only |
| **Sort** | Choice | **Name (A-Z)** | **Name (A-Z)**, **Name (Z-A)**, **Updates Available First** | Reorders visible cards | Local operation remains independent | Does not select another release | Current snapshot | Session only |
| **Refresh** | Button | Idle | Official index fetch; 10-second fetch timeout | Fetches/validates the official index and rebuilds cards against installed discovery | Disabled by offline policy and while refresh is active; local controls remain | Uses network and replaces successful cache; failure retains/report cache state where available | Repository snapshot | Successful live snapshot is cached |
| **More Actions** | Instant menu | Closed | Two fixed actions | Opens local install and environment cleanup | Disabled while an operation is active | Actions are not repository search | Installation | None |
| **Install local package...** | Menu action/file picker | Last valid directory or empty | Existing `.tar.zst`, `.tar`, or All files selection | Starts unsigned local archive transaction | Globally blocked until unsigned packages are allowed; also requires a second confirmation | No publisher authentication; can replace same-ID managed package after validation | User extension root | Last directory; package/receipt/environment on success |
| **Clean unused environments** | Menu action | Idle | Current discovered package references | Removes orphaned environment inventory entries | Disabled during another operation; cancellable | Reclaims disk; shared environments survive while referenced | Environment root | Directory removal is durable |
| **Cancel** | Button | Hidden | Active install/update/local install/remove/cleanup | Requests cooperative cancellation and disables itself while cancelling | Visible only during an active operation | Can prompt whether queued operations should also be cancelled | Active operation/queue | None |
| Queued operation **Cancel** | Per-item button | Hidden | One pending install/update/remove operation | Removes only that pending operation from the queue | One button appears on each queued-operation row | Does not cancel the current operation or other queued work | Selected queued operation | None |
| Install-state/runtime/version/signature badges | Read-only state | Current snapshot | Not installed, installed/source, update, incompatible; runtime state; selected version; Signed/Unsigned/Unknown/Signature failed | Summarizes the selected release and local state | Always on each card | Compact status is not a substitute for Details | Package entry | Cached with snapshot |
| **Install** / **Update to _version_** / **Installed** / **Unavailable** | Primary card button | Derived | Install only Not installed; Update only Update available | Starts repository install/update or reports terminal state | Network policy disables Install/Update; Installed/Incompatible are disabled; trust checks can still block after click | Downloads, validates, provisions, and atomically changes package files | Selected repository entry | Package/receipt/environment/cache |
| **Uninstall** | Destructive card button | Derived | Installed repository/user source | Confirms and removes package asynchronously | Disabled for not installed, bundled, or explicit Local package source; stays usable when networking is disabled | Close running sessions; environment cleanup is separate | Installed package | Durable removal |
| **Details** / **Hide details** | Toggle | Collapsed | One metadata panel | Expands complete Overview, Provenance, Links, Release, and Trust sections | Always enabled for a card | May expose links, hashes, names, and provenance | Package entry | Session only |
| Expanded Homepage/Source/Support/Original Source links | External links | Package metadata | Valid displayed URLs | Opens package/author-owned external documentation or support | Metadata is displayed but not linkified for an Unsigned asset | Leaves AfterNight; destination privacy/content policy is external | Package entry | None |
| **Allow unsigned extensions** | Checkbox | Off | Off/On with confirmation | Permits per-package unsigned confirmations and local archive selection | Always enabled, including during other operations; cancelling confirmation returns Off | Weakens authentication policy and shows persistent warning banner | Extension manager install policy | `extensions/allowUnsignedPackages`; Reset Tab returns Off |
| **Reset Tab** | Dialog button | Enabled | Confirm **Reset Extensions** | Clears search, restores All/A-Z, disables unsigned installs, and refreshes repository | Applies only to Browse here | Can trigger network refresh and revoke local-install eligibility | Browse tab | Trust opt-in reset immediately |
| **Close** | Dialog button | Enabled | Close dialog | Closes manager | Does not cancel an already independent/running extension host | Manager operation should finish/cancel first | Dialog | No additional state |
| **Check for extension updates on startup** | Settings checkbox | On | On/Off | Schedules background official-repository comparison after startup | Skipped when Off; network policy returns diagnostic without notification | Uses network; never installs automatically | Application startup | `general/checkExtensionUpdatesOnStartup` |
| **Extension Updates Available > Open Extensions** | Notification button | Primary/default | One or more updates found | Opens Extensions on Browse | Notification appears only after a successful check with updates | User still reviews and triggers each update | Startup notification | None |
| **Extension Updates Available > Ignore** | Notification button | Secondary | Current notification | Dismisses without changing packages | Available only in update notification | Same update can be reported on a later check | Notification | None |
| **Disable extension update checks on startup** | Notification checkbox | Off | Off/On | Writes startup-check setting Off when dialog closes | Available only in update notification | Suppresses future automatic checks, not manual Refresh | Application | `general/checkExtensionUpdatesOnStartup=0` |
| Package Details metadata | Read-only matrix | Repository/index values | Complete fields listed in Behavior | Supports compatibility, trust, attribution, license, and support decision | Missing values show **Not declared**; compatibility/signature diagnostic can add a banner | Repository metadata can be inaccurate even when structurally valid | Selected release/asset | Cached snapshot |

## Behavior and state model

### Repository scope, cache, and update checks

The Browse page is hard-wired to the official repository client in the current
UI. User-entered repository URLs, ratings, payments, and a general third-party
catalog are not implemented. A validated successful live snapshot is written to
`repository_snapshot.json`. On a later open, that cache can render immediately
while a live refresh runs. With repository networking disabled, a usable cache
can still render but Install/Update/Refresh remain disabled; installed extension
actions and the Installed tab remain available.

A failed live refresh reports **Extension repository unavailable** or retains a
cached snapshot with the last background-refresh diagnostic. Search/filter/sort
never perform network access. Manual Refresh requests a new official index. The
startup checker is enabled by default, runs separately in the background, and
shows a notification only after a successful comparison finds updates; failures
are logged, not turned into a package mutation.

### Compatibility and installed-state selection

The repository validator requires a coherent index, release, and asset schema.
Every release must declare package format, protocol, and SDK version 1, the
versions both extension hosts and the SDK speak. An index that lists a release
with any other version fails validation as a whole, and Browse handles it as a
failed refresh (above) instead of passing the release over. For the current
runtime it selects a release/asset by minimum AfterNight version,
backend/launch availability, Python compatibility, runtime target, and asset
metadata. A newer repository version is marked Update available only for a
repository-installed older version. A locally/user-installed version is not
silently overwritten merely because a repository entry exists.

### Trust and transaction boundaries

Every repository asset needs an authoritative SHA-256. At install, AfterNight
hashes the downloaded bytes and rejects a mismatch before extraction. Trust then
follows this state machine:

| Signature state | Install result | User decision |
| --- | --- | --- |
| **Signed** | Allowed only when Ed25519 metadata is complete, key ID is trusted by this build, and signature verification succeeds | Still review publisher, license, provenance, links, and code purpose |
| **Unsigned** | Blocked by default; allowed only after global opt-in plus per-install confirmation | Independently verify the exact author/archive; AfterNight cannot authenticate it |
| **Unknown** | Blocked | Wait for corrected repository metadata/package |
| **Signature failed** | Blocked even when unsigned packages are allowed | Do not install; report to repository/package maintainer |

Archive extraction rejects unsafe paths, links/device entries, absolute paths,
excessive depth/count/size, and a non-empty extraction destination. Validation
checks manifest identity/version against the index, current compatibility,
declared artifacts, dependency metadata, license/backend boundaries, and runtime
targets. The package is staged and activated atomically with rollback. A success
requires environment readiness, settings/startup cache readiness, and bytecode
precompile readiness when requested; the UI does not report a partial download as
installed. The precompilation skips a directory it cannot read, naming it in the
application log, and compiles every other module; an environment's dependency
bytecode that cannot be compiled never fails the install, because the extension
compiles what it imports when it first starts.

### Complete Package Details metadata

| Section | Fields shown |
| --- | --- |
| Overview | Description, State, Status, Version, Status Detail, Author, Publisher, License |
| Provenance | Attribution, Original Author, Original Project, Original Source, Upstream Commit, Tags |
| Links | Homepage, Source, Support |
| Release | SDK, Launch, Runtime Targets, Published, Changelog, Asset |
| Trust | Signature, Signature Key, Package Hash, Compatibility; a compatibility/signature detail banner when present |

The **Official Extensions** filter is a browsing convenience, not a replacement
for the Trust section. Likewise, a correct hash proves byte identity against the
index, not safety. A valid signature authenticates the canonical signed asset
metadata with a trusted key; it does not audit extension behavior.

### Documentation ownership

This guide owns AfterNight's manager controls, states, trust policy, host/runtime
boundaries, and recovery handoff. The package author/publisher owns the extension's
algorithm, accepted inputs, settings semantics, examples, output interpretation,
external tools/models, license notices, privacy/network behavior, migrations, and
support. The official index should expose Homepage, Source, Support, changelog,
license, attribution, and provenance so users can reach that material. Authoring,
manifest, packaging, SDK, and repository submission rules remain in
`docs/extension-examples/` and developer documentation, not duplicated as
end-user marketplace instructions.

## Inputs, outputs, and metadata

The network path reads an official JSON index and selected release archive. The
local path reads one archive. A successful operation writes the activated package
directory, `extension_package_receipt.json` provenance, managed environment,
ready/settings caches, and menu/status cache invalidation. The receipt records
source tier, repository/local source, install time, archive hash, selected asset,
signature metadata, and host-bootstrap fingerprint.

Update stages a replacement and backup, preserves host-managed settings, and
restores the prior package on failure. Uninstall removes package files after
confirmation; it does not delete settings or every environment automatically.
Opening Browse, filtering, sorting, expanding Details, or checking for updates
does not modify images, image metadata, or history. Package execution outputs are
owned by [Using Extensions](using-extensions.md) and the individual package docs.

## Performance and responsiveness

Index fetch, archive download, install/update/remove, environment cleanup, and
startup checks run off the UI thread. Progress phases and a cancellation token
keep the manager responsive. One operation runs at a time; additional card
install/update/remove actions can queue. Search/filter/sort and cached-card
rendering remain local.

Network and environment-work duration depends on the index, archive, dependency
sources, and local system. Duration, CPU use, and disk use were not measured.
Keep the window open until success/cancellation and avoid manually editing
staging, backup, package, or environment directories. Offline policy avoids
repository requests but cannot install an uncached package asset; the snapshot
cache contains metadata, not necessarily the archive/dependencies.

## Worked example

- Starting state, actions, settings, checkpoints, result, and limitations: Open
  Browse with a deterministic official-index fixture containing a compatible
  Signed release and an older repository-installed version. **Updates Available**
  selects the card; Details supplies the new version, target, Ed25519 key, SHA-256,
  changelog, license, and support links. **Update to _version_** hashes/verifies,
  stages, validates, provisions, primes, precompiles, and atomically replaces the
  package. The card is marked installed, caches/menus are invalidated, and
  Installed must report **Available** before launch. A hash/signature/readiness
  failure instead preserves/restores the prior package and shows a diagnostic.
  This records the tested transaction contract only; no live official repository,
  third-party package, GUI, dependency download, timing, or package execution was
  used.

## Warnings and limitations

- The current marketplace supports one official repository. Do not infer support
  for arbitrary third-party feeds from the local archive route.
- Unsigned is not equivalent to invalid, but it is unauthenticated and blocked by
  default. Unknown/failed signatures are always blocked.
- **Official Extensions** filtering is not a security verdict. Inspect Trust and
  package-owned documentation before each install/update.
- Offline cached cards do not guarantee that the archive or dependency artifacts
  are locally available.
- Update/removal should not occur while the same package has running sessions.
- Clean unused environments is destructive and recoverable only by reinstalling
  or repairing the package that needs the removed dependency set.
- AfterNight does not automatically install updates. Startup checks notify only.
- Package links can leave AfterNight and may collect network metadata according to
  the destination's policy. Unsigned card URLs are deliberately not clickable.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| **Extension repository unavailable** | Official index fetch/validation failed and no usable current result replaced it; one release that declares a package format, protocol, or SDK version other than 1 fails the whole index | Check network/policy, keep cached/installed packages, then Refresh later |
| **Extension repository network features are disabled by policy.** | Either repository-offline environment policy is truthy | Restart without the policy only when approved; Installed/local uninstall remain available |
| Cached cards show a failed-background-refresh diagnostic | Saved snapshot loaded, later live refresh failed | Do not assume metadata is current; retry Refresh when online |
| **Package asset is missing the required SHA-256 hash.** | Repository asset lacks authoritative hash metadata | Do not install; report the index/release defect |
| **Package signature state is unknown, so installation is blocked.** | Asset did not resolve to a supported signature state | Obtain corrected repository metadata/package |
| **Package signature verification failed, so installation is blocked.** | Signature was Failed or cryptographic verification did not match | Do not bypass by allowing unsigned; report package/repository compromise or metadata defect |
| **Unsigned Extension Blocked** | Unsigned repository/local package while opt-in is Off | Prefer Signed; otherwise independently verify, enable opt-in, and confirm each install |
| Install/Update button says **Unavailable** | Selected release is incompatible with current app/backend/target | Read Compatibility and install a compatible build/release |
| Install finishes download but reports failure | Hash/signature, extraction, package validation, environment, cache, or bytecode readiness failed; a manifest of another version reports **Only package/protocol/sdk version 1 manifests are supported** | Read exact banner/progress phase; fix source/package/disk/dependencies and retry; prior package is preserved on update failure |
| Update is not offered for a local/user package | UpdateAvailable is derived for an older Repository-installed source | Use a reviewed local replacement or reinstall from repository after understanding source precedence |
| Uninstall is disabled | Not installed, Bundled, or explicit Local package source on that card | Use Installed/source-specific management; bundled content is app-owned |
| Cleanup removes zero environments | Every environment is still referenced or inventory is already clean | No action needed; uninstall obsolete packages first |
| No startup notification appears | Setting Off, policy/network/fetch failure, no updates, or no matching installed package | Check General setting/logs; open Browse and Refresh manually |

## Related workflow steps

- [Using Extensions](using-extensions.md) — confirm environment readiness,
  configure schema fields, understand host modes/permissions/buffers, launch, and
  diagnose the installed package.
- [Settings Dialog](../interface/settings-dialog.md) — General startup update
  setting and global settings persistence.
- [System Requirements](../reference/system-requirements.md) — supported
  platforms, deployed runtimes, network/disk requirements, and policy context.
- [Session Console](../interface/log-viewer.md) — repository refresh, startup
  update, install, environment, and host diagnostics.
