---
kind: interface
summary: >-
  The Installed tab of the Extensions window: whether each installed extension can run, its environment, its
  settings and its last launch, and where the processes an extension adds are found and how they run.
---
# Using Extensions

An extension adds processes to AfterNight. Its Python code runs in a program of its own, an
[extension host](../reference/glossary.md#extension-host), and never inside AfterNight, so an extension that fails
does not take the application down with it. Its processes, each tagged **extension**, are at the end of their
category in the **Process** menu, in the extension's menu under **Extensions** and in the
[command palette](../reference/glossary.md#command-palette). [Extensions in the Window](extension-tasks.md) says how
they show.

The **Installed** tab of the Extensions window lists the extension packages on this computer: whether each one can
run, the Python environment it needs, and its settings. You install, update and remove packages on the **Browse**
tab, which [Extension Marketplace](extension-marketplace.md) describes.

```figure
image: ../assets/images/extensions/using-extensions.png
alt: >-
  The Extensions window, a window of its own, on its Installed tab. Under its heading, Extensions, the tabs Browse and
  Installed; under them a banner reads 5 extension package(s) discovered: 5 available, 0 bootstrapping, 0
  incompatible, 0 needing attention. A Refresh Status button sits below it, then a note that the tab looks for the
  packages again when it is refreshed. The package cards follow. Each card shows the package's name with a green
  Available badge and a version badge, the buttons Uninstall, Settings on a package that has settings, Reveal Package
  and Details on the right, and under the name a line with the source, the host mode and the identifier. The first
  card, Continuum Subtraction, is open on its details: its package root and manifest file, its environment path and
  runtime targets, the result of its last check, what install or repair it needs, its last launch and its last crash
  or termination. The second, Gaia Glow Subtraction, is closed, and a third begins under it. Reset Tab at the left and
  Close at the right run along the bottom.
caption: >-
  The Installed tab with the packages installed on this computer, every one available, the first open on its details.
pins:
  - {n: 1, x: 38.0, y: 12.6}
  - {n: 2, x: 65.0, y: 16.9}
  - {n: 3, x: 16.0, y: 23.0}
  - {n: 4, x: 3.0, y: 38.0}
  - {n: 5, x: 60.0, y: 38.0}
  - {n: 6, x: 50.0, y: 42.4}
  - {n: 7, x: 3.0, y: 50.0}
  - {n: 8, x: 14.0, y: 94.8}
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

Choose **Manage...** in an extension's own menu under **Extensions**. **Extensions > Manage Extensions...**, its row
in the command palette and **More actions > Extension Manager** on [Home](../interface/home.md) open the window on
**Browse**: then choose **Installed**. The tab shows the status it found last at once, and replaces it when it has
looked again.

### Check that an extension can run

1. Find the package's card. **Available** means that its environment is ready and its processes can open.
2. For any other state, choose **Details**. **Validation Result**, **Install / Repair** and the banners under them say
   what stands in the way.
3. Choose **Refresh Status** after a change made outside AfterNight, such as a package folder copied by hand.

### Run an extension's process

1. Make the image you want to process the [active image](../reference/glossary.md#active-image), then choose the
   process in the **Process** menu, in its extension's menu under **Extensions**, or in the command palette.
2. When the package's environment is not ready yet, AfterNight prepares it first, and says so in the
   [task](../reference/glossary.md#task)'s column or in a window titled **Preparing** and the process's name.
3. Work in the task, or in the extension's own window.
4. Apply the result. It reaches the image, as one step of its [history](../reference/glossary.md#history), only when
   the extension hands it over: a run that is cancelled or fails leaves the image as it was.
5. Close the task. Its session ends, its host stops, and the extension can be started on the same image again.

### Change an extension's settings

Choose **Settings** on the card. A window named after the extension opens with the fields its package declares.
Change them and choose **Save**. They hold for all its processes and later sessions.

### Prepare or repair an environment

AfterNight prepares an environment by itself: in the background after it starts, and when you choose one of the
package's processes. To do it now, choose **Install Environment** or **Repair Environment** on the card. The summary
says when it is done, or why it failed.

### Remove a package

1. Close the extension's tasks: uninstalling does not stop a host that is running.
2. Choose **Uninstall** on the card, then **Yes**. The summary reads "Uninstalled" and the package's name, and its
   card and processes go.
3. Its settings and its environment stay. **More Actions > Clean unused environments** on the **Browse** tab removes
   the environment once no package uses it.

### Find out why a launch failed

Choose **Refresh Status** and open the card's **Details**. **Last Launch** and **Last Crash / Termination** give the
reason, and the banner under them says whether to relaunch, repair the environment, reinstall the package or ask its
author. Read the [Session Console](../interface/log-viewer.md)'s lines before you add them to a report: they name
your folders and can hold the package's data.

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
      **Bootstrapping** stays available in the menus and the command palette, and choosing it prepares the environment
      first. The processes of an **Incompatible** or **Unavailable** package are greyed, with the reason as their
      tooltip, and dimmed in the palette with the reason beside them.
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
      On every card whose package folder AfterNight knows, except a **Bundled** one: a bundled package ships with
      AfterNight and goes only with it. Like every button of a card, it is disabled while an operation runs.
      **Cancel** is the default answer.
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
      Closing the window a Full hosted extension shows of its own is recorded as "Disconnected: Extension host native
      process window closed". With nothing recorded it reads "No crash or forced-termination history is recorded yet."
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
    note: >-
      The window is titled with the extension's name and Settings, under the heading Extensions and the line "The
      settings this extension states for itself." An extension can open it from its task or its own window too.
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
  - name: Cancel
    kind: button
    default: ""
    effect: Closes the window without keeping the fields you changed.
  - name: Save
    kind: button
    default: ""
    effect: Keeps the fields and closes the window. It stands at the right of the footer, after **Cancel**.
    note: When they cannot be kept, a message titled "Error Saving Settings" gives the reason, and the window stays open.
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
    enabled: >-
      In a window titled **Preparing** and the process's name, for a process that does not open as a page. A process
      AfterNight draws as a page opens its task at once instead, and the starting row of its column says the step.
    note: Choosing the process again while it prepares brings this window, or the task, forward.
  - name: Cancel
    kind: button
    default: ""
    effect: >-
      Stops the preparation, and the process does not open. Closing the window does the same, and so does closing the
      task of a process that opens as a page.
    note: >-
      When the environment cannot be made ready, a message titled with the process's name gives the reason and
      "Open Extensions > Manage Extensions... > Installed to install or repair it." The next launch prepares it again.
```

### The window of a Full hosted extension

```controls
window: The window of a Full hosted extension
controls:
  - name: Window
    kind: readout
    default: ""
    effect: >-
      A window of the extension host's own, drawn in AfterNight's look, for an extension that shows a window rather
      than controls AfterNight draws as a page. It is a task in the rail that brings it forward.
    note: An extension whose controls AfterNight draws opens as a task instead, with the preview and **Apply** of every task.
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
:   **Cause.** A session of that extension on that image still runs: its task, its window or its own dialog is open,
    or its run has not finished.
:   **Fix.** Close it from its item in the [rail](../reference/glossary.md#rail), or let its run finish, then choose
    the process again.

A message titled with the process's name gives a reason, and the extension's task or window stopped
:   **Cause.** The host crashed, failed or stopped answering, and AfterNight stopped it. **Last Crash / Termination**
    keeps the reason.
:   **Fix.** Read it there and in the Session Console, repair the environment when it names a missing dependency, and
    choose **Restart** in the task, or start the process again, on a smaller image or preview.

## Related

- [Extensions in the Window](extension-tasks.md), for an extension's task, a window of its own, the processes it adds
  and what it may do to the image's view.
- [Extension Marketplace](extension-marketplace.md), to find, install, update and remove packages.
- [Using Extensions Reference](using-extensions-reference.md), for the states, host modes, environments, what an
  extension receives, and every message.
- [Top Bar and Menus](../interface/top-bar-and-menus.md), for the **Extensions** menu and where an extension's
  processes sit in the **Process** menu.
- [Masks](../interface/masks.md), for the [mask](../reference/glossary.md#mask) an extension receives as it is.
- [Settings](../interface/settings-dialog.md), for **Updates per second while dragging** and **Check for extension
  updates on startup**.
