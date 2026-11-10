---
kind: interface
summary: >-
  The Installed tab of the Extensions window: whether each installed extension can run, its environment, its
  settings and its last launch, and how the processes an extension adds open and run.
rewritten: true
---
# Using Extensions

An extension adds processes to AfterNight. Its Python code runs in a program of its own, an
[extension host](../reference/glossary.md#extension-host), and never inside AfterNight, so an extension that fails
does not take the application down with it. Each process an extension adds sits in the **Process** menu, after a line
at the end of the category its package names.

The **Installed** tab of the Extensions window lists the extension packages on this computer: whether each one can
run, the Python environment it needs, and its settings. You install, update and remove packages on the **Browse**
tab, which [Extension Marketplace](extension-marketplace.md) describes.

```figure
owed: true
image: ../assets/images/extensions/using-extensions.webp
alt: >-
  The Extensions window on its Installed tab. Under the tabs Browse and Installed, a banner counts the packages found,
  and a Refresh Status button sits below it. Two package cards follow. Each card shows the package's name with a
  green Available badge and a version badge, then the buttons Uninstall, Reveal Package and Details on the right, with
  Settings on the card of a package that has settings, and under the name a line with the source, the host mode and
  the identifier. The first card is open on its
  details: the package's folders, its runtime targets, its last check, its last launch and its last stop. Reset Tab
  and Close run along the bottom.
caption: >-
  The Installed tab with two packages, one of each host mode, the first open on its details.
pins:
  - {n: 1, x: 39.0, y: 4.5}
  - {n: 2, x: 88.0, y: 11.0}
  - {n: 3, x: 20.0, y: 17.5}
  - {n: 4, x: 3.5, y: 28.5}
  - {n: 5, x: 57.0, y: 28.5}
  - {n: 6, x: 3.5, y: 33.0}
  - {n: 7, x: 3.5, y: 42.0}
  - {n: 8, x: 14.0, y: 95.5}
```

## What you see

1. **The tabs.** **Browse** for the official repository, **Installed** for the packages on this computer.
2. **The summary.** How many packages AfterNight found, and how many are available, still being prepared,
   incompatible or in need of attention.
3. **Refresh Status.** Looks for the packages again and rebuilds the cards.
4. **A package's card.** Its name, whether it can run, and its version.
5. **The card's buttons.** Prepare or repair its environment, uninstall it, change its settings, show its folder,
   and open its details.
6. **Source, host mode and identifier.** Where the package was found, which kind of host runs it, and its
   identifier.
7. **The details.** Its folders, its last check, its last launch and its last crash or stop, with what to do next.
8. **Reset Tab and Close.** **Reset Tab** looks for the packages again, and **Close** closes the window.

## What you can do

### Open the Installed tab

Choose **Manage...** in an extension's own submenu of the **Extensions** menu, or choose **Extensions > Manage
Extensions...** and then the **Installed** tab. It shows the status it found last at once, and replaces it when it
has looked again.

### Check that an extension can run

1. Find the package's card. **Available** means that its environment is ready and that its processes can open.
2. For any other state, choose **Details**. **Validation Result**, **Install / Repair** and the banners under them say
   what stands in the way.
3. Choose **Refresh Status** after a change made outside AfterNight, such as a package folder copied by hand. The
   cards show the new state.

### Run an extension's process

1. Make the image you want to process the [active image](../reference/glossary.md#active-image), then choose the
   process in the **Process** menu.
2. When the package's environment is not ready yet, a window titled **Preparing** and the process's name shows
   AfterNight installing, refreshing or repairing it. The process window opens once the environment is ready.
3. Work in the extension's window. A package of the **Full hosted** kind opens a window built like AfterNight's own
   [process windows](../interface/process-windows.md), with their preview and **Apply**. A package of the **Lite
   hosted** kind draws its own window.
4. Apply the result. It reaches the image, as one step of its [history](../reference/glossary.md#history), only when
   the extension hands it over: a run that is cancelled or fails leaves the image as it was.
5. Close the window. Its session ends, its host stops, and the extension can be started on the same image again.

### Change an extension's settings

Choose **Settings** on the card. A window named after the extension opens with the fields its package declares.
Change them and choose **Save**. They hold for all its processes and later sessions.

### Prepare or repair an environment

AfterNight prepares an environment by itself: in the background after it starts, and when you choose one of the
package's processes. To do it now, choose **Install Environment** or **Repair Environment** on the card. The summary
says when it is done, and the card then shows **Available**. When it fails, the summary gives the reason.

### Remove a package

1. Close the extension's windows: uninstalling does not stop a host that is running.
2. Choose **Uninstall** on the card, then **Yes**. The summary reads "Uninstalled" and the package's name, and its
   card and processes go.
3. Its settings and its environment stay. **More Actions > Clean unused environments** on the **Browse** tab removes
   the environment once no package uses it.

### Find out why a launch failed

Choose **Refresh Status** and open the card's **Details**. **Last Launch** and **Last Crash / Termination** give the
reason, and the banner under them says whether to relaunch, repair the environment, reinstall the package or ask its
author. Add the [Session Console](../interface/log-viewer.md)'s lines to a report, once you have read them: they
name your folders and can hold the package's data.

!!! warning "An extension is a program you run"
    A host runs with your user account's rights to your files and the network. Being a separate program protects
    AfterNight from an extension that crashes. It protects nothing else, and AfterNight asks no permission on an
    extension's behalf. Install only packages whose author you trust, even when their signature checks out.

## Controls

### The Installed tab

```controls
window: The Installed tab
controls:
  - name: Summary
    kind: readout
    default: Loading installed extension status...
    effect: >-
      Counts the packages: "2 extension package(s) discovered: 2 available, 0 bootstrapping, 0 incompatible, 0 needing
      attention." It turns amber when a package is incompatible or needs attention.
    range: >-
      Needing attention counts **Install Needed**, **Repair Needed**, **Validation Failed** and **Unavailable**. With no
      package it reads "No bundled or user-installed extension packages were discovered.", with "Open this page again
      after adding or provisioning an extension package to refresh the status list." under it.
    note: >-
      While the tab looks again over the cards it shows, it reads "Refreshing installed extension status in the
      background...". It also gives the result of an uninstall or of an environment's preparation.
  - name: Refresh Status
    kind: button
    default: ""
    effect: >-
      Looks for the packages again and rebuilds every card from the packages, their environments, their stored
      settings and their last launches.
    enabled: Disabled while the tab looks or a card's operation runs.
    tradeoff: It installs, repairs, updates and uninstalls nothing.
  - name: Availability
    kind: readout
    default: ""
    effect: >-
      The badge after the package's name. **Available**, in green, when the environment is ready. **Bootstrapping**
      while AfterNight prepares it. **Install Needed** and **Repair Needed**, in amber, when it is missing or out of
      date. **Validation Failed** and **Incompatible**, in red, when its check failed or the package does not suit this
      AfterNight. **Unavailable** when no environment state was reported.
    range: >-
      A process of a package that shows **Install Needed**, **Repair Needed**, **Validation Failed** or
      **Bootstrapping** stays available in the menu, and choosing it prepares the environment first. The processes of
      an **Incompatible** or **Unavailable** package are greyed, with the reason as their tooltip.
    note: Its tooltip says what the state means. Using Extensions Reference lists each state's causes.
  - name: Version
    kind: readout
    default: ""
    effect: The version the package declares, as `v1.2.0`. A package that declares none has no badge.
  - name: Install Environment
    kind: button
    default: ""
    effect: >-
      Creates the package's environment, checks it, and prepares what the package needs to start, then reads the tab
      again. It reads **Installing...** meanwhile, and the summary reads "Installed environment for" and the package's
      name when it is done.
    enabled: >-
      Shown when the environment is missing, for a package installed from the Extensions window. A bundled package,
      or one copied in by hand, has neither this button nor **Repair Environment**: its processes prepare it when you
      choose them.
    tradeoff: It takes disk space and processor time, and the network when the package's requirements come from an index.
  - name: Repair Environment
    kind: button
    default: ""
    effect: >-
      Refreshes in place an environment prepared for another build of AfterNight or another revision of the package,
      without installing anything. Otherwise it builds the environment again and checks it. It reads **Repairing...**
      meanwhile, and the summary reads "Repaired environment for" and the package's name when it is done.
    enabled: Shown in place of **Install Environment** when the environment is out of date or failed its check.
    tradeoff: >-
      A shared environment serves every package that uses it, so its repair changes them all. A failure leaves
      "Extension environment repair failed." or the reason in the summary.
  - name: Uninstall
    kind: button
    default: ""
    effect: >-
      Asks "Uninstall" and the package's name, with "This removes the installed extension package from disk. Running
      extension sessions for this package should be closed first.". **Yes** removes the package's folder in the
      background, and the button reads **Uninstalling...**. The tab and the menus are then read again.
    enabled: >-
      On every card whose package folder AfterNight knows. Like every button of a card, it is disabled while an
      operation runs. **Cancel** is the default answer.
    tradeoff: >-
      A host that is running keeps running. The package's settings and environment stay, and a failure leaves
      "Extension uninstall failed." or the reason in the summary.
  - name: Settings
    kind: button
    default: ""
    effect: Opens the extension's settings window.
    enabled: >-
      Shown once the environment is ready and the package declares settings. AfterNight reads a package's settings
      only from a ready environment, and keeps what it read for the next start.
  - name: Reveal Package
    kind: button
    default: ""
    effect: Shows the package's folder in your file manager.
    enabled: Greyed when the folder no longer exists.
    tradeoff: The folder's path holds your user name.
  - name: Details
    kind: toolbutton
    default: "Off"
    effect: Shows the rows below the card, and then reads **Hide details**.
    enabled: Disabled while the tab looks or a card's operation runs.
    persistence: Closed again each time the cards are rebuilt.
  - name: Source, Host Mode and ID
    kind: readout
    default: ""
    effect: >-
      The line under the name, as "Source: User-installed | Host Mode: Full hosted | ID: example.basic_process". The
      sources are **Bundled**, **Repository**, **Local package** and **User-installed**. The host modes are **Full
      hosted**, **Lite hosted** and **Lite hosted + RPC**.
  - name: Package Root, Environment Path and Manifest File
    kind: readout
    default: ""
    effect: The package's folder, its environment's folder and its `extension.json`. You can select and copy them.
    note: A row with nothing to show is left out, here and below. Each row's tooltip says what it holds.
  - name: Runtime Targets
    kind: readout
    default: ""
    effect: The platforms the package declares, such as `windows-msvc-x86_64`.
  - name: Validation Result
    kind: readout
    default: ""
    effect: >-
      The last message of the package's or the environment's check, or "Manifest and environment validation passed."
  - name: Install / Repair
    kind: readout
    default: ""
    effect: >-
      What the environment needs next, as "Installation is required before this extension can launch." or "No install
      or repair action is currently required."
  - name: Last Launch
    kind: readout
    default: ""
    effect: >-
      "The latest launch succeeded.", or "Failed:" and the reason, or "No launch result has been recorded yet."
  - name: Last Crash / Termination
    kind: readout
    default: ""
    effect: >-
      How the last session ended badly: **Crashed**, **Forced Stop**, **Disconnected**, **Unresponsive**, **Failed** or
      **Exited**, with the reason, the session, the exit code, the time since the last heartbeat and the request in
      progress where known.
    note: >-
      Closing a full window is recorded as "Disconnected: Extension host native process window closed". With nothing
      recorded it reads "No crash or forced-termination history is recorded yet."
  - name: Diagnostic and recovery banners
    kind: readout
    default: ""
    effect: >-
      Under the rows, a banner repeats the check's message or a failure of the package's settings. A second one says
      what to do after a failed launch or a bad end, such as "The extension host stopped responding and was
      terminated. Reduce workload or adjust settings, then relaunch."
```

### The window's buttons

```controls
window: The window's buttons
controls:
  - name: Reset Tab
    kind: button
    default: ""
    effect: >-
      Asks "Are you sure you want to reset the "Installed" tab to defaults?", titled "Reset Extensions", and on **Yes**
      looks for the packages again, as **Refresh Status** does.
    note: The tab has no setting to reset. On **Browse** the same button resets that tab's choices.
  - name: Close
    kind: button
    default: ""
    effect: Closes the Extensions window. AfterNight then reads the installed packages again and updates its menus.
    tradeoff: Hosts that are running keep running.
    note: Both tabs keep their choices as you make them, so nothing is left to save.
```

### Settings of an extension

```controls
window: Settings of an extension
controls:
  - name: Fields of the package
    kind: field
    default: The package's defaults
    effect: >-
      What the package declares: check boxes, whole and decimal numbers, sliders, lists, text, and file and folder
      paths. A field can show, hide, enable or disable itself as other fields change.
    persistence: >-
      **Save** keeps each field the package marks to keep, except a secret one, in AfterNight's settings database as
      `extension/<extension id>/<field id>`. One value per extension serves all its processes.
    tradeoff: A wrong path or value can keep the package's processes from working.
  - name: Status and output
    kind: readout
    default: ""
    effect: Shows a status, or the output an action of the package streams, where the package declares one.
    note: The values of secret and sensitive fields are written as `[redacted]` in an action's messages.
  - name: Actions of the package
    kind: button
    default: ""
    effect: >-
      Runs what the package offers, such as finding or checking a tool, in the background. An action can report its
      progress and fill in fields, but never a secret or sensitive one.
    enabled: >-
      One action at a time. An action can stop after its time limit, 60 seconds unless the package says otherwise,
      and some run by themselves when the window opens.
    persistence: What an action fills in is kept as it arrives, so **Cancel** does not undo it.
  - name: Save
    kind: button
    default: ""
    effect: Keeps the fields and closes the window.
    note: When they cannot be kept, a message titled "Error Saving Settings" gives the reason, and the window stays open.
  - name: Cancel
    kind: button
    default: ""
    effect: Closes the window without keeping the fields you changed.
```

### Preparing

```controls
window: Preparing a process
controls:
  - name: Progress
    kind: readout
    default: Preparing the extension's Python environment
    effect: >-
      The step AfterNight is on, with a bar when the step can count its progress. It reads "Waiting for the extension
      environment being prepared" while another preparation of the same environment, such as the one AfterNight
      starts with, finishes first.
    note: Choosing the process again while it prepares brings this window forward.
  - name: Cancel
    kind: button
    default: ""
    effect: Stops the preparation, and the process window does not open. Closing the window does the same.
    note: >-
      When the environment cannot be made ready, a message titled with the process's name gives the reason and
      "Open Extensions > Manage Extensions... > Installed to install or repair it." The next launch prepares it again.
```

### The window of a Full hosted extension

```controls
window: The window of a Full hosted extension
controls:
  - name: Preview
    kind: action
    default: ""
    effect: >-
      Previews as AfterNight's process windows do. While you drag a slider or a curve, it shows previews at the rate of
      **Updates per second while dragging** in Settings, and letting go runs one more over the whole image.
    persistence: The window keeps the rate it opened with. A change applies to the windows opened after it.
    note: >-
      A newer change, an **Apply** or the window's closing stops a preview, which then shows nothing. Some packages
      preview a reduced image while you drag, as Using Extensions Reference says.
  - name: Apply
    kind: button
    default: ""
    effect: >-
      Runs the extension over the whole image, never over a preview's. The result is copied into AfterNight and shown
      once the copy is done, as one step of the image's history.
    note: >-
      While another process writes the image, the run waits, with "Execution waits for the image being written."
      titled "Extension Waiting". A step the package does not name is called `Extension Apply`.
  - name: Close
    kind: button
    default: ""
    effect: Ends the extension's session and stops its host.
    enabled: While a run goes on, it reads **Cancel** and the other controls are disabled. A cancelled run changes nothing.
    note: >-
      A package that closes its window after **Apply** still delivers that result first. One that hides its window
      to show a dialog of its own ends its session when that dialog closes.
```

## When it goes wrong

"No bundled or user-installed extension packages were discovered."
:   **Cause.** No readable package is installed.
:   **Fix.** Install one from the **Browse** tab, then choose **Refresh Status**.

A package's files are installed, but it has no card and no process
:   **Cause.** Its `extension.json` cannot be read, it declares a package format, protocol or SDK version other than
    1, or it is marked for developers only.
:   **Fix.** Install a release of the package made for this version of AfterNight, then choose **Refresh Status**.

The card shows **Incompatible**
:   **Cause.** The package needs a newer AfterNight, another Python, another platform or a host this AfterNight does
    not have, such as **Lite hosted + RPC**.
:   **Fix.** Read **Validation Result** in its details, and install a release of the package that suits this
    AfterNight.

A message says "The extension's environment could not be made ready."
:   **Cause.** Preparing the environment failed: a dependency could not be installed, the disk is full, or the
    package's sources could not be reached.
:   **Fix.** Read the reason above it, free disk space or connect to the network, and choose the process again or
    **Repair Environment**. Reinstall the package if that fails too.

"This extension already has an active host session for the selected image."
:   **Cause.** A session of that extension on that image still runs: its window or its own dialog is open, or its run
    has not finished.
:   **Fix.** Close the extension's window or dialog, or let its run finish, then choose the process again.

The extension's window is gone, and a message titled with the process's name gives a reason
:   **Cause.** The host crashed, failed or stopped answering, and AfterNight stopped it. **Last Crash / Termination**
    keeps the reason.
:   **Fix.** Read it there and in the Session Console, repair the environment when it names a missing dependency, and
    relaunch on a smaller image or preview.

## Related

- [Extension Marketplace](extension-marketplace.md), to find, install, update and remove packages.
- [Using Extensions Reference](using-extensions-reference.md), for the states, host modes, environments, what an
  extension receives, and every message.
- [Toolbar & Menus](../interface/toolbar-and-menus.md), for the **Extensions** menu and where an extension's processes
  sit in the **Process** menu.
- [Process Windows](../interface/process-windows.md), for the preview, **Apply** and the history that a Full hosted
  extension's window shares.
- [Masks](../interface/masks.md), for the [mask](../reference/glossary.md#mask) an extension receives as it is.
- [Settings](../interface/settings-dialog.md), for **Updates per second while dragging** and **Check for extension
  updates on startup**.
