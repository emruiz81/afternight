---
kind: reference
summary: >-
  What Extension Marketplace leaves out: where the list comes from, which release a card offers, what each signature
  state allows, how an install runs and what it writes, what a package file may hold, and every message.
---
# Extension Marketplace Reference

This page holds what [Extension Marketplace](extension-marketplace.md) leaves out to stay readable beside the **Browse**
tab. Each section can be read on its own. What happens once a package is installed is on
[Using Extensions](using-extensions.md) and its reference.

## Where the list comes from

One repository
:   The tab reads one list, AfterNight's official repository, at
    `https://api.github.com/repos/emruiz81/afternight-extensions/contents/index.json?ref=live`, and the list must say
    that it is the official one. No setting and no environment variable names another. The tab has no ratings, no
    payments and no catalog of other publishers, and installing a package file adds no repository.

The saved list
:   A list read in full is saved as `repository_snapshot.json`, in the `extension_manager` folder of AfterNight's cache
    folder, and again after each install. The environment variable `AFTERNIGHT_EXTENSION_MANAGER_CACHE_DIR` moves it.
    The next time the tab opens, it shows the saved list at once and reads the repository again in the background,
    with the card buttons and **Refresh** disabled meanwhile. Until that reading ends, the cards show what was
    installed when the list was saved.

What the saved list holds
:   The descriptions of the packages, not the packages. Nothing can be installed from it without the network.

Time limits
:   Reading the list stops after 10 seconds, at **Refresh** as at the check at start. Downloading a package stops after
    120 seconds for the whole file, with "Package asset download timed out."

With the network turned off
:   `AFTERNIGHT_DISABLE_EXTENSION_REPOSITORY_NETWORK` or `AFTERNIGHT_EXTENSION_REPOSITORY_OFFLINE`, set to `1`, `true`,
    `yes` or `on` in any case, turns off every request to the repository. With a saved list, the cards show, nothing
    reads the repository again, and **Install**, **Update** and **Refresh** are disabled. Without one, the summary
    reads "Extension repository unavailable: Repository network features are disabled by policy." **Uninstall**, the
    **Installed** tab and the installed processes work either way.

The check at start
:   A few seconds after AfterNight starts, while **Check for extension updates on startup** is ticked, it reads the
    list in the background and compares it with the installed packages. It writes its steps in the
    [log file](../reference/glossary.md#log-file), shows a message only when it read the list and found an update, and
    never changes a package. With the network turned off it only writes "Extension repository network features are
    disabled by policy." in the log.

## Which release a card offers

- **The latest release, for this platform.** A card offers the release whose version is the package's latest in the
  list, and the first file of it made for this platform, `windows-msvc-x86_64` or `linux-clang-x86_64`. It falls back
  on no older release.
- **What the card checks.** A release that needs a newer AfterNight, or offers no file for this platform, makes the
  card **Incompatible**. The Python, the host and the launch mode that a package needs are checked on its own
  `extension.json` while it installs.
- **Versions 1 only.** Every release must declare package format, protocol and SDK version 1, the versions both
  hosts and the SDK speak. One release that declares another makes the whole list unreadable, as "repository releases
  must declare package/protocol/sdk version 1 for this host", rather than leaving that release out.
- **Updates.** A card offers **Update to** only when your copy came from the repository, its version is older than the
  latest, and the latest suits this AfterNight. A copy you installed from a file, copied in by hand or that came with
  AfterNight reads **Installed**, and the list never replaces it on its own.

## Signatures and hashes

| Signature | Install | What to decide |
| --- | --- | --- |
| **Signed** | Allowed when the release gives its whole signature - the Ed25519 method, the key and the signature - the key is one this AfterNight trusts, and the signature matches the downloaded file's hash and the release's description. | Read the publisher, the licence, the provenance, the links and what the code is for all the same. |
| **Unsigned** | Refused while **Allow unsigned extensions** is off. Otherwise it asks "Install Unsigned Extension?" each time. | Make sure of the author and of the exact file yourself: AfterNight cannot. |
| **Unknown** | Refused: "Package signature state is unknown, so installation is blocked." | Wait for a corrected release. |
| **Signature failed** | Refused, even with unsigned packages allowed. | Do not install it, and tell the repository's or the package's maintainer. |

- **One trusted key.** This AfterNight trusts one key, `afternight-official-ed25519-v1`, built into it. Nothing adds
  another. A release signed with another key, or with a malformed signature, makes the whole list unreadable.
- **Refused before the install starts.** "Package signature metadata is incomplete, so installation is blocked." and
  "Package signature key is not trusted by this build, so installation is blocked." appear in the summary. A failed or
  unknown signature also opens a message titled "Extension Cannot Be Installed".
- **A hash for every file.** The list gives each package file's SHA-256. A file without one is refused with "Package
  asset is missing the required SHA-256 hash." The downloaded file is hashed before anything else is done with it, and
  one that differs is refused with "Package archive SHA-256 does not match the expected hash".
- **What they prove.** A matching hash says that the file is the one the list names. A matching signature says that
  the holder of the trusted key published it. Neither says what the code does, nor that it is safe, correct or
  private. **Official Extensions** in **Filter** is a convenience of the same kind. The descriptions in the list are
  the publishers', and can be wrong in a list that is well formed.

## How an install runs

1. **The file.** The package is downloaded to a temporary folder, or a package file is copied there.
2. **Its hash and signature,** as above. A package file has neither and skips this step.
3. **Unpacking,** into a new staging folder, within the limits below.
4. **Its `extension.json`.** The file must hold one, whose identifier and version are the ones the list gives. It
   must suit this AfterNight's version, Python, platform, launch mode and host, and the files it declares must be
   there. A package from the repository must also give its summary, author, licence and publisher, and keep to the
   rules of its licence and host on what it may import.
5. **Its record,** `extension_package_receipt.json` in the package's folder.
6. **Its environment.** AfterNight prepares the environment, reads and keeps the package's settings and what it needs
   to start, and compiles the package's Python modules. A dependency that cannot be compiled fails nothing; the
   package's own modules must compile, or the install fails with "Extension package bytecode precompilation failed".
7. **In place.** A copy of the same identifier is renamed aside, the new package is moved in and checked again, and
   the old copy is removed.

A failure or a cancel before step 7 removes the staging folder and an environment the install created for the
package alone. A failure while the package is put in place, or at the check after, puts the old copy back. Extension
settings are never part of the install, so an update keeps them. When the package is in place, the card reads
installed at once, the saved list is updated, the tab reads the repository again, the **Installed** tab reads the
packages again, and the menus follow.

## What a package file may hold

A package is a `.tar` or `.tar.zst` file: any other is refused with "Unsupported package archive format; supported
formats are .tar and .tar.zst".

| Limit | Value |
| --- | --- |
| Folder depth | 32 |
| Entries | 10,000 |
| One entry, unpacked | 768 MiB |
| All entries, unpacked | 2 GiB |

An entry with an empty name, a backslash or a colon in its path, an absolute path or `..`, an entry twice, a link, a
device or any other entry that is neither a file nor a folder, and an entry that would leave or overwrite the staging
folder are refused, and so is a staging folder that is not empty.

## What an install writes

- **The package's folder,** in `extensions\_repository` of AfterNight's data folder for a package from the list, in
  `extensions` for a package file.
- **The install record,** `extension_package_receipt.json`: the identifier and version, the source, the list's
  address or the package file's path, the time of the install, the file's hash, the file chosen from the release, the
  signature's state, method, key and detail, and a fingerprint of this build's hosts and Python. The signature itself
  is not kept.
- **The environment and the caches** the package starts from, which the **Installed** tab reads.
- **The settings of the tab,** `extensions/allowUnsignedPackages`, `extensions/lastLocalPackageDirectory` and
  `extensions/automaticEnvironmentCleanup`, and the check at start, `general/checkExtensionUpdatesOnStartup`, in
  AfterNight's settings database. The search, **Filter** and **Sort** are not kept.

**Uninstall** removes the package's folder and nothing else: its settings and its environment stay. Browsing,
searching, reading a card's details and checking for updates change no image, no image's cards and no image's
[history](../reference/glossary.md#history).

## What the publisher answers for

The guide describes the Extensions window, its states, the rules of trust and what AfterNight does with a package.
What a package does - its method, the images it accepts, the meaning of its settings, its examples, how to read its
results, the tools and models it calls, its licences, what it does with your data and the network, how it moves its
own files from one version to the next, and its support - is its publisher's to document. The links of a card lead
there. An extension may keep files of its own that an update or an uninstall does not touch: back up what matters
before either. Writing and publishing a package is explained in the `docs/extension-examples` folder of AfterNight's
source, not in this guide.

## What it costs

- **Waiting.** Reading the list, downloading, installing, removing, cleaning and the check at start run in the
  background, with each step in the summary and a **Cancel**. One operation runs at a time, and the others wait in
  the queue. Searching, filtering and sorting are immediate.
- **Time.** An install takes as long as the download and the package's dependencies, which depend on the package, its
  sources and the computer.
- **Disk.** The file, its staging copy and the backup of the copy it replaces, the environment, the compiled bytecode
  and the caches, all at once during an install. A full disk fails the install, which then removes what it wrote.
- **Hands off.** Do not edit the staging, backup, package or environment folders while AfterNight works on them.

## Troubleshooting

"Package asset is missing the required SHA-256 hash."
:   **Cause.** The list gives no hash for the package's file.
:   **Fix.** Do not install it. Tell the repository's maintainer.

"Package signature verification failed, so installation is blocked." or "Package signature does not match the signed repository asset metadata"
:   **Cause.** The signature does not match the file or the release, or the list marks it failed.
:   **Fix.** Do not allow unsigned packages to get around it. Tell the maintainers: the file or the list may have been
    tampered with, or be wrong.

The card's button reads **Unavailable**
:   **Cause.** The latest release does not suit this AfterNight or this platform.
:   **Fix.** Read **Compatibility** in its details, and install a version of AfterNight or a release that suits it.

"Only package/protocol/sdk version 1 manifests are supported"
:   **Cause.** The package declares another package format, protocol or SDK version.
:   **Fix.** Install a release of the package made for this AfterNight.

The summary names a step that was not done after the install, such as "Extension environment readiness was not verified after installation."
:   **Cause.** The package was put in place, but AfterNight could not confirm that it is ready to start.
:   **Fix.** Open the **Installed** tab, and repair the environment or reinstall the package.

**Uninstall** is disabled
:   **Cause.** The package is not installed, or it came with AfterNight. Its tooltip says which.
:   **Fix.** None is needed for a package that came with AfterNight: it is part of AfterNight.

**Clean unused environments** removes nothing
:   **Cause.** Every environment is still used by an installed package, or none is left to remove.
:   **Fix.** None is needed. Uninstall the packages you no longer use first.

The summary says that the last background refresh did not complete
:   **Cause.** The tab shows the list it saved, and could not read the repository again.
:   **Fix.** Do not take the list as current. Choose **Refresh** once the network is back.

No update message at start, though an update exists
:   **Cause.** The check is off, the network is turned off or failed, or the package was not installed from the
    repository.
:   **Fix.** Look at **Check for extension updates on startup** and the Session Console, or open the **Browse** tab
    and choose **Refresh**.

"Package asset download timed out." or "Package asset download failed:" and a reason
:   **Cause.** The file did not arrive within 120 seconds, or the server refused it.
:   **Fix.** Check the network, and install again.
