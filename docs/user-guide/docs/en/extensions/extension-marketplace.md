---
kind: interface
summary: >-
  The Browse tab of the Extensions window: the extensions of AfterNight's official repository, whether each suits
  this AfterNight and is signed, and the buttons that install, update and remove them or install a package file.
menu: Extensions > Manage Extensions...
---
# Extension Marketplace

The **Browse** tab of the Extensions window lists the extensions of AfterNight's official repository. It says what
each one is, whether it suits this AfterNight and whether its package is signed, and it installs, updates and
removes them. It can also install a package file from your disk. It is no store, sells nothing and lists no other
repository.

Open it with **Extensions > Manage Extensions...**, with **Extension Manager** under the Dashboard's **···** button,
or with **Open Extensions** in the message AfterNight shows after it starts when an installed extension has an update.
Once a package is installed, the **Installed** tab says whether it can run: see [Using Extensions](using-extensions.md).

```figure
owed: true
image: ../assets/images/extensions/extension-marketplace.webp
alt: >-
  The Extensions window on its Browse tab. Under the tabs, a banner counts the packages the repository offers and a
  line gives the disk space the extension environments take. A row holds a search box, a Filter list at All
  Extensions, a Sort list at Name (A-Z), a Refresh button and a More Actions button. Package cards follow, each with
  its name, badges for its state, its version and its signature, and the buttons Install or Installed, Uninstall and
  Details. The first card is open on its details, in five parts from Overview to Trust. Two check boxes, Automatically
  remove unused extension environments and Allow unsigned extensions, sit above Reset Tab and Close.
caption: >-
  The Browse tab with the official repository's list, the first card open on its details.
pins:
  - {n: 1, x: 90.0, y: 10.0}
  - {n: 2, x: 80.0, y: 14.5}
  - {n: 3, x: 50.0, y: 19.5}
  - {n: 4, x: 71.0, y: 19.5}
  - {n: 5, x: 83.5, y: 19.5}
  - {n: 6, x: 3.5, y: 27.0}
  - {n: 7, x: 62.0, y: 27.0}
  - {n: 8, x: 3.5, y: 45.0}
  - {n: 9, x: 45.0, y: 87.0}
  - {n: 10, x: 14.0, y: 95.5}
```

## What you see

1. **The summary.** How many packages the list shows, and how many have an update or do not suit this AfterNight.
2. **The environments' disk space.** What the extensions' environments take, and how much of it is unused.
3. **Search extensions.** Shows the packages whose card holds the words you type.
4. **Filter and Sort.** Narrow the list to one kind of package, and order it.
5. **Refresh and More Actions.** **Refresh** reads the repository again. **More Actions** installs a package file and
   removes unused environments.
6. **A package's card.** Its name, and badges for its state, its version and its signature.
7. **The card's buttons.** Install or update it, uninstall it, and open its details.
8. **The details.** What the package is, who made it, where it comes from, what its release holds, and whether it
   can be trusted.
9. **The two check boxes.** Whether AfterNight removes unused environments by itself, and whether it installs
   unsigned packages.
10. **Reset Tab and Close.** **Reset Tab** returns the tab's choices to their defaults, and **Close** closes the
    window.

## What you can do

### Find an extension

Type part of a name, an author or a tag in **Search extensions**, or choose a **Filter** and a **Sort**. They act on
the list already loaded and use no network. The tab shows the list it saved last at once, while it reads the
repository again in the background.

### Read a card before you install

Choose **Details**, and read each part: what the package does, who wrote and publishes it, its licence, the work it
derives from, its links and changelog, the platforms it runs on, and its **Trust**. Each is a decision of its own.
**Signed** means that the package file comes from the publisher whose key this AfterNight trusts. It says nothing of
whether the code is safe or suits your data. The package's own pages hold its documentation and its support.

### Install an extension

1. Choose **Install** on its card. A package that is not signed is refused unless you allow unsigned packages, and
   then asks you first.
2. The button reads **Installing...**, and the summary and a bar show each step. **Cancel** stops the install, which
   then leaves nothing behind.
3. When it is done, the summary reads "Installed", the name, the version and "Startup caches primed.", and the card's
   state reads **Repository installed**. Its processes appear in the **Process** menu.
4. Open the **Installed** tab and check that the package shows **Available**. A download that succeeded is not yet a
   package that runs.

### Update an extension

Choose **Updates Available** in **Filter**, or **Updates Available First** in **Sort**. Read the card's details
again, then choose **Update to** and the version. The update runs as an install does, and keeps the extension's
settings. When the new version cannot be put in place, the version you had is put back.

### Remove an extension

Close the extension's windows, choose **Uninstall** on its card and then **Yes**. Its environment stays, until
**More Actions > Clean unused environments** removes the environments no package uses.

### Install a package file

1. Tick **Allow unsigned extensions**, and choose **OK** in "Allow Unsigned Extensions?". A warning banner stays above
   the check box while it is ticked.
2. Choose **More Actions > Install local package...**, then **OK** in "Install Unsigned Local Extension?".
3. Choose a `.tar.zst` or `.tar` file. The summary reads "Installed local package", the name and the version.
4. Open the **Installed** tab and check that the package shows **Available**.
5. Untick **Allow unsigned extensions**.

### Keep AfterNight offline

Set the environment variable `AFTERNIGHT_DISABLE_EXTENSION_REPOSITORY_NETWORK` or
`AFTERNIGHT_EXTENSION_REPOSITORY_OFFLINE` to `1`, `true`, `yes` or `on`, in any case, before you start AfterNight.
The tab then shows the list it saved, with **Install**, **Update** and **Refresh** disabled, and **Uninstall** still
works. The check for updates at start reaches nothing. The saved list holds no package, so nothing can be installed
from it.

!!! warning "Only one repository, and no review of the code"
    The tab reads AfterNight's official repository and no other. Installing a package file is no way to add a
    repository. A package can do anything your user account can, signed or not: install only from authors you trust.

## Controls

### The Browse tab

```controls
window: The Browse tab
controls:
  - name: Summary
    kind: readout
    default: Loading extension repository...
    effect: >-
      Counts the cards shown, as "12 package(s) available from the official repository: 1 update(s), 2 incompatible."
      It turns amber when a package has an update or is incompatible, and gives the progress and result of each
      operation.
    range: >-
      With the saved list it adds where the list comes from and " Refreshing the repository in the background.", or,
      when that reading failed, " Last background refresh did not complete:" and the reason. A list that cannot be
      read gives "Extension repository unavailable:" and the reason.
  - name: Environments' disk space
    kind: readout
    default: ""
    effect: >-
      "Extension environments: 1.2 GiB total across 3 environment(s) — 900.0 MiB active, 300.0 MiB reclaimable in 1
      environment(s)."
    note: Shown once the tab has looked at the environments, even when the repository cannot be read.
  - name: Progress
    kind: readout
    default: ""
    effect: The step of the running operation, such as "Downloading package asset", with a bar.
    enabled: Shown while an operation runs.
  - name: Queued operations
    kind: readout
    default: ""
    effect: >-
      "1 queued extension operation(s)", then a row for each install, update or uninstall waiting its turn, each with a
      **Cancel** button that removes that one from the queue.
    enabled: >-
      Shown while operations wait. One operation runs at a time: the buttons of the other cards queue theirs. A card
      with a queued operation reads **pending...**.
  - name: Search extensions
    kind: field
    default: ""
    effect: >-
      Shows the cards whose name, identifier, summary, description, author, publisher, licence, attribution, original
      author, original project, original source, upstream commit or tags hold the text, in any case.
    enabled: Usable during an operation and while the list is read, as **Filter** and **Sort** are.
    persistence: Empty each time the window opens, as **Filter** and **Sort** are at their first choice.
  - name: Filter
    kind: combo
    default: All Extensions
    options: [All Extensions, Installed Only, Updates Available, Incompatible, Official Extensions]
    effect: >-
      Shows every card, the installed ones, those with an update, those that do not suit this AfterNight, or those of
      the official publisher.
    note: >-
      **Official Extensions** shows the packages signed with AfterNight's official key or published under the
      publisher name `afternight`. It is a convenience, not a judgement of their safety: read **Trust**.
  - name: Sort
    kind: combo
    default: Name (A-Z)
    options: [Name (A-Z), Name (Z-A), Updates Available First]
    effect: Orders the cards by name, either way without regard to case, or puts those with an update first.
    note: It chooses no other release of a package.
  - name: Refresh
    kind: button
    default: ""
    effect: >-
      Reads the official repository's list again, within 10 seconds, and rebuilds the cards against what is
      installed. A list read in full is saved for the next time.
    enabled: Disabled while the list is read, while an operation runs, and when the network is turned off.
    tradeoff: >-
      It uses the network. When the reading fails, the tab keeps the saved list and says that the reading did not
      complete.
  - name: More Actions
    kind: toolbutton
    default: ""
    effect: Opens a menu of **Install local package...** and **Clean unused environments**.
    enabled: Disabled while an operation runs. It reads **Installing...** while a package file installs.
  - name: Install local package...
    kind: action
    default: ""
    effect: >-
      Asks "Install Unsigned Local Extension?", then for a file, `.tar.zst` or `.tar`, and installs it among the
      user-installed packages, in place of a package of the same identifier there.
    enabled: >-
      Refused while **Allow unsigned extensions** is off, with "Local extension packages are treated as unsigned.
      Enable unsigned extensions before installing a local package.", titled "Unsigned Extension Blocked".
    tradeoff: >-
      Nothing checks who made the file: it has no signature and no hash to compare. It runs every other check of an
      install.
    persistence: The file window opens in the folder you chose last, kept as `extensions/lastLocalPackageDirectory`.
  - name: Clean unused environments
    kind: action
    default: ""
    effect: >-
      Removes the environments of uninstalled extensions, and those a ready one replaced, at once. It ends with
      "Removed 2 unused extension environment(s) and reclaimed 1.4 GiB." or "No unused extension environments were
      found."
    range: An environment an installed package uses, one awaiting repair and one being prepared stay.
    tradeoff: >-
      What it removes comes back only when a package that needs it is repaired or reinstalled. It can be cancelled.
  - name: Cancel
    kind: button
    default: ""
    effect: >-
      Stops the running operation, and reads "Cancelling extension operation..." until it has. With operations
      queued, it then asks "Cancel Queued Extension Operations?", with **No** as the default answer.
    enabled: Shown only while an operation runs.
  - name: Automatically remove unused extension environments
    kind: check
    default: "On"
    effect: >-
      At start, removes the environments no installed package uses, once 24 hours have passed since they were last
      written, and those a ready one replaced. One that awaits repair or is being prepared stays.
    persistence: Kept at once as `extensions/automaticEnvironmentCleanup`, and usable during other operations.
  - name: Allow unsigned extensions
    kind: check
    default: "Off"
    effect: >-
      Lets you install an unsigned package, each time after a question, and a package file. Ticking it asks "Allow
      Unsigned Extensions?" first, and **Cancel** leaves it off.
    tradeoff: >-
      AfterNight can then no longer tell who made what you install. While it is ticked, a banner reads "Unsigned
      extensions can run code that AfterNight cannot authenticate. Only enable this for packages from authors and
      files you trust."
    persistence: Kept at once as `extensions/allowUnsignedPackages`, and usable during other operations.
```

### A package's card

```controls
window: A package's card
controls:
  - name: State
    kind: readout
    default: ""
    effect: >-
      **Not installed**, **Update available**, **Incompatible**, or, once installed, where from: **Repository
      installed**, **User-installed**, **Local package** or **Bundled**. Its tooltip says more.
  - name: Status
    kind: readout
    default: ""
    effect: >-
      For an installed package whose environment is not ready, **Install needed**, **Repair needed**, **Failed** or
      **Bootstrapping**, as the **Installed** tab says. Shown only when it differs from the state.
  - name: Version
    kind: readout
    default: ""
    effect: >-
      "Latest" and the repository's version, "Installed" and yours when they differ, or "New version" and the version
      of an update. Its tooltip gives both versions.
  - name: Signature
    kind: readout
    default: ""
    effect: >-
      **Signed** in green, **Unsigned** in amber, **Signature failed** in red, **Unknown** otherwise. Its tooltip names
      the key of a signed package.
  - name: Install
    kind: button
    default: ""
    effect: >-
      Installs the release the card offers. It reads **Update to** and the version for an update, **Installed** for a
      package you have, and **Unavailable** for one that does not suit this AfterNight, both disabled.
    enabled: >-
      Disabled while the list is read, and with the network turned off, with "Repository install and update actions
      are disabled by policy." as its tooltip.
    tradeoff: >-
      It downloads the package and prepares its environment, which can take minutes, disk space and the network.
      Keep the window open until the summary says it is done. Close the extension's windows before an update: a host
      that runs is not started again on the new files.
  - name: Uninstall
    kind: button
    default: ""
    effect: >-
      Asks "Uninstall" and the name, with "This removes the installed extension package from disk. Running extension
      sessions for this package should be closed first.", **Cancel** being the default answer. **Yes** removes the
      package's folder, and the summary reads "Removed" and the name.
    enabled: >-
      For a package installed from the repository or among the user-installed ones. Disabled for a bundled package,
      with "... is bundled with AfterNight and cannot be uninstalled here." Works with the network turned off.
    tradeoff: Its settings and environment stay. A host that is running keeps running.
  - name: Details
    kind: toolbutton
    default: "Off"
    effect: Shows the card's details, and then reads **Hide details**.
  - name: Summary line
    kind: readout
    default: ""
    effect: >-
      The package's summary, or "No summary provided.". Under it, for an update, "Installed 1.1.0; 1.2.0 is
      available."
```

### The card's details

```controls
window: The card's details
controls:
  - name: Overview
    kind: readout
    default: ""
    effect: >-
      **Description**, **State**, **Status**, **Version**, **Status Detail**, **Author**, **Publisher** and **License**.
    note: A value the repository does not give reads "Not declared".
  - name: Provenance
    kind: readout
    default: ""
    effect: >-
      **Attribution**, **Original Author**, **Original Project**, **Original Source**, **Upstream Commit** and **Tags**:
      the work the package derives from.
  - name: Links
    kind: readout
    default: ""
    effect: >-
      **Homepage**, **Source** and **Support**, which open in your browser.
    tradeoff: >-
      A link leaves AfterNight for a site with rules of its own. The addresses of an unsigned package are shown as
      text, never as links, and the same goes for its other texts.
  - name: Release
    kind: readout
    default: ""
    effect: >-
      **SDK**, the package, protocol and SDK versions it declares, **Launch**, as **Single image** or **Workflow**,
      **Runtime Targets**, **Published**, **Changelog** and **Asset**, the file a click on **Install** downloads.
  - name: Trust
    kind: readout
    default: ""
    effect: >-
      **Signature**, **Signature Key**, **Package Hash**, the file's SHA-256, and **Compatibility**, as "Compatible with
      this AfterNight runtime".
    note: A banner under it gives the reason a package is incompatible or its signature failed.
```

### The window's buttons

```controls
window: The window's buttons
controls:
  - name: Reset Tab
    kind: button
    default: ""
    effect: >-
      Asks "Are you sure you want to reset the "Browse" tab to defaults?", titled "Reset Extensions". **Yes** empties
      the search, returns **Filter** and **Sort** to their first choice, ticks **Automatically remove unused extension
      environments**, unticks **Allow unsigned extensions** and reads the repository again.
    tradeoff: It uses the network, and a package file can no longer be installed until you allow unsigned packages again.
  - name: Close
    kind: button
    default: ""
    effect: Closes the Extensions window. AfterNight then reads the installed packages again and updates its menus.
    tradeoff: An extension that is running keeps running. Let an operation finish, or cancel it, before you close.
    note: Both check boxes are kept as you change them, so nothing is left to save.
```

### Extension Updates Available

```controls
window: Extension Updates Available
controls:
  - name: The updates
    kind: readout
    default: ""
    effect: >-
      "An installed extension has an update available:", or how many have one, then a line per package, as
      "Example: 1.1.0 -> 1.2.0", up to four, and "And 2 more..." for the rest.
    note: >-
      It appears a few seconds after AfterNight starts, while **Check for extension updates on startup** in Settings
      is ticked, only when the repository was read and an update was found. AfterNight installs nothing by itself.
  - name: Open Extensions
    kind: button
    default: ""
    effect: Opens the Extensions window on this tab. You still choose each update.
  - name: Ignore
    kind: button
    default: ""
    effect: Closes the message and changes nothing. The same update is shown again at a later start.
  - name: Disable extension update checks on startup
    kind: check
    default: "Off"
    effect: Turns the check at start off when the message closes, as unticking it in Settings does.
    note: It leaves **Refresh** as it is.
```

## When it goes wrong

"Extension repository unavailable:" and a reason
:   **Cause.** The list could not be read: no network, a refusal by the server, a list that AfterNight does not
    accept, or the network turned off with no list saved. A release of a package format, protocol or SDK version other
    than 1 makes the whole list unreadable.
:   **Fix.** Check the network, keep using what is installed, and choose **Refresh** later.

"Extension repository network features are disabled:" and the policy
:   **Cause.** One of the two environment variables that keep AfterNight offline is set.
:   **Fix.** Start AfterNight without it, if you may. Installed packages and **Uninstall** still work.

"Unsigned Extension Blocked"
:   **Cause.** The package, or every package file, is unsigned, and **Allow unsigned extensions** is off.
:   **Fix.** Prefer a signed package. Otherwise make sure of its author and file, tick **Allow unsigned extensions**,
    and confirm the install.

"Extension Cannot Be Installed"
:   **Cause.** The package's signature failed or its state is unknown. Allowing unsigned packages does not lift this.
:   **Fix.** Do not install it. Tell the repository's or the package's maintainer.

The install downloads, then fails
:   **Cause.** A check after the download refused the package: its hash, its signature, its contents, its manifest,
    its environment or the disk space it needs.
:   **Fix.** Read the summary, deal with what it names and install again. An update that fails puts back the version
    you had.

**Update to** never appears for a package you have
:   **Cause.** Only a package installed from the repository is offered its updates.
:   **Fix.** Uninstall the copy you installed otherwise, then install the package from the repository.

## Related

- [Using Extensions](using-extensions.md), to check that an installed package runs, change its settings and use it.
- [Extension Marketplace Reference](extension-marketplace-reference.md), for how an install runs, what the trust
  states mean, what an install writes, and every message.
- [Settings](../interface/settings-dialog.md), for **Check for extension updates on startup**.
- [Dashboard](../interface/dashboard.md), for its **Extension Manager**.
- [Session Console](../interface/log-viewer.md), for the lines the repository, the installs and the check at start
  write.
- [System Requirements](../reference/system-requirements.md), for what the network and the disk must provide.
