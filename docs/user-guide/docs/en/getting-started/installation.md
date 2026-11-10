---
kind: tutorial
summary: >-
  Install AfterNight on Windows with its installer or on Linux as an AppImage, start it, and open the copy of this
  guide that comes with it.
needs: [the package for your system, from the Releases page]
rewritten: true
---
# Installation

You start with a 64-bit x86 computer running Windows or Linux. You end with AfterNight started on its Dashboard and
this guide open from the copy installed with it. Windows takes an installer. Linux takes an AppImage, one file that
runs without being installed. Follow the first step, the steps for your system, then the last step.

## What you need

- [ ] A computer that meets the [System Requirements](../reference/system-requirements.md), on Windows or Linux, 64-bit
  x86
- [ ] A network connection to download the package
- [ ] On Windows, administrator rights only if AfterNight must be installed for every user of the computer
- [ ] A folder for your images and results outside the folder AfterNight is installed in

The installers check only that the computer is 64-bit x86. They check no memory, disk or graphics card: the System
Requirements page says what a large project needs.

## Steps

### Download the package for your system

Open the [Releases page](https://github.com/emruiz81/afternight/releases) of AfterNight, and under the latest release
open **Assets**. Download one file:

- `AfterNightUserSetup_<version>.exe` installs AfterNight on Windows for your account only. Choose it unless your
  computer's administrator needs otherwise.
- `AfterNightSetup_<version>.exe` installs it on Windows for every user of the computer, and needs administrator
  rights. It installs the same program, no faster and no better.
- `AfterNight-<version>-x86_64.AppImage` is AfterNight for Linux.

The files whose names end in `debug-symbols.zip` serve to read crash reports. You do not need them.

!!! see "You should see"
    One file in your downloads folder, its name holding the version you chose.

### Run the Windows installer

Close AfterNight if an earlier version of it is running. Then run the file you downloaded. Run the whole setup
program: a copy of `AfterNight.exe` taken out of an installation does not start, because it needs the libraries and
folders beside it.

The installer for your account asks for no administrator rights. The installer for every user asks for them first.

When it finds an earlier version, the installer removes it before it goes on. It reads "A previous version (…) is
installed. It will now be uninstalled." Choose **OK**: its uninstaller runs, and setup continues when it succeeds.
When it finds the same version, it asks "The same version (…) is already installed. Would you like to uninstall it
first?" **Yes** removes it and installs again. **No** ends with "Installation cancelled by user." and changes
nothing. Setup compares the two version numbers as text, character by character. It installs over a version that
sorts after its own without removing it first. Your settings are kept in every case, because they are not in the
installation folder.

!!! see "You should see"
    The setup wizard. On a first installation it opens on the page that chooses the destination folder.

??? wrong "If it goes wrong: "Uninstaller not found in registry." or "Uninstallation failed with exit code: …""
    Setup found an earlier version recorded but could not remove it, and it stops without installing. Remove the
    earlier AfterNight from the list of installed apps in the Windows settings, then run setup again. Do not delete
    files of the old installation by hand.

### Choose the folder and the shortcuts

Keep the folder setup proposes unless you have a reason to change it. The installer for your account proposes
`AfterNight` under `%LOCALAPPDATA%\Programs`. The installer for every user proposes `AfterNight` under
`C:\Program Files`.

Keep no images, projects or results in that folder. Uninstalling AfterNight deletes the folder and everything in it.

Choose **Next**. On the next page, check **Create a desktop shortcut** if you want one. The Start menu entry
**AfterNight** is always made. Choose **Next**, then **Install**.

!!! warning "The installer for every user opens its folder to every account"
    It gives every account on the computer full control of the installation folder. Any of them can then change
    the installed program files. Use it only on a computer whose accounts you trust.

!!! see "You should see"
    A progress bar while the files are copied, then the last page of the wizard, with **Launch AfterNight** checked.

### Finish and start AfterNight on Windows

Leave **Launch AfterNight** checked and choose **Finish**. Later, start AfterNight from **AfterNight** in the Start
menu, or from the desktop shortcut. When the package holds the guide, as every package of the Releases page does,
the Start menu also holds **AfterNight User Guide**. It opens this guide in your web browser without starting
AfterNight.

!!! see "You should see"
    The AfterNight window, open on its Dashboard. [First Launch](first-launch.md) describes what is on it.

### Make the AppImage executable on Linux

Move the AppImage to a folder of your own, such as `~/Applications`, outside your image folders. Then allow it to run
as a program: in a terminal in that folder, type

```bash
chmod +x AfterNight-<version>-x86_64.AppImage
```

or turn on the permission to run it as a program in your file manager's properties of the file.

!!! see "You should see"
    In the terminal, `ls -l` lists the file with an `x` among its permissions, such as `-rwxr-xr-x`.

??? wrong "If it goes wrong: "Permission denied""
    The file still lacks the permission to run, or the disk it is on does not allow programs to run from it. Run
    `chmod +x` again, or move the AppImage to a folder in your home directory.

### Start the AppImage

Double-click the AppImage, or start it from the terminal:

```bash
./AfterNight-<version>-x86_64.AppImage
```

It runs from the one file. It installs nothing in your system and adds no entry to your applications menu. The paths
to its own libraries are set for its run only.

!!! see "You should see"
    The AfterNight window, open on its Dashboard. [First Launch](first-launch.md) describes what is on it.

??? wrong "If it goes wrong: nothing starts, or a library is reported missing"
    Most systems need FUSE to run an AppImage: [System Requirements](../reference/system-requirements.md) says which
    package provides it and how to run the AppImage without it. A library or plugin reported missing means the file
    is incomplete. Download the AppImage again, whole, rather than adding files to it.

### Open the user guide

In AfterNight, press ++f1++, or choose **Help > User Guide**. The guide opens in your web browser from the copy
installed with AfterNight: the `docs` folder of the installation on Windows, or the inside of the AppImage on Linux.
It needs no network connection. Only when no copy can be opened does AfterNight open the guide online, at
`https://docs.afternightastro.org/`. To keep it from going online, set the environment variable
`AFTERNIGHT_DISABLE_ONLINE_USER_GUIDE` to `1` before AfterNight starts.

!!! see "You should see"
    This guide in your browser, opened from a file on your disk rather than from a web address.

??? wrong "If it goes wrong: the guide opens online, or not at all"
    A package built on your own computer holds the guide only when the guide was built before the package. A
    package from the Releases page always holds it. When the online guide is turned off and no copy can be opened,
    AfterNight reports "The offline AfterNight User Guide could not be opened, and online fallback is disabled by
    policy." When neither can be opened, it reports "The AfterNight User Guide could not be opened locally or
    online." Install the package from the Releases page.

## The result

AfterNight is installed and starts, and this guide opens without a network connection.

- **On Windows,** the installation folder holds the application and everything it loads: its libraries, the Visual
  C++ runtime among them, the Python that extensions run in, the two programs that run extensions, the command-line
  tool `AfterNightCLI.exe`, the crash handler and the guide in `docs`. Nothing else needs to be installed. It holds
  no test programs and no debug symbols. Windows lists AfterNight among its installed apps, and uninstalling it there
  removes the folder.
- **On Linux,** the AppImage holds the same parts. Deleting the file removes AfterNight.

Every AfterNight program on Windows can read and write paths longer than 260 characters, where Windows has long paths
turned on.

Your settings, recent files and other data of AfterNight are not part of the installation. AfterNight creates them in
your user profile the first time it starts, and uninstalling or deleting the package leaves them there. Installing
changes none of your images.

AfterNight needs no network connection to open, process and save your images. A catalog download, an extension or an
online plate solve needs one, and so does the check for extension updates AfterNight makes when it starts.

To build AfterNight from source instead, follow [BUILD.md](https://github.com/emruiz81/afternight/blob/main/BUILD.md)
in its repository. A build runs from its own `bin` folder, and on Linux through the `run_AfterNight.sh` script beside
the program, which sets the paths to its libraries.

## Next

- [First Launch](first-launch.md) shows what AfterNight shows on its first start, and opens a first image.
- [Quick Start](quick-start.md) takes the frames of one night to a stacked
  [master](../reference/glossary.md#master).
- [System Requirements](../reference/system-requirements.md) says how much memory and disk a large project takes.
- [Supported Formats](supported-formats.md) lists the files AfterNight opens and writes.
