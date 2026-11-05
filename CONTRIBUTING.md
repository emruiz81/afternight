# Contributing to AfterNight

Thank you for considering a contribution. This page is how a change gets in; [AGENTS.md](AGENTS.md) is the canonical
guide to the repository - the build order, the coding standards, the theme rules, the test policy and the definition
of done - and where the two disagree, AGENTS.md is right. Everyone taking part follows the
[code of conduct](CODE_OF_CONDUCT.md).

## Before you start

- **Look for an issue.** Work is tracked as GitHub issues. For anything larger than a small fix, open one first, or
  say on the existing one that you are working on it, so the approach can be agreed before the code is written.
- **Build it.** [BUILD.md](BUILD.md) takes you from a clone to a running build and its tests.
- **Read the guide for the area.** A design's reasons are written in the guide that describes its area:
  [docs/dev/guides/PRODUCT_ARCHITECTURE.md](docs/dev/guides/PRODUCT_ARCHITECTURE.md) for the product and the engine
  guides beside it. A change to how an area works updates its guide in the same change.

## Sign your work

Every commit carries a `Signed-off-by` line, which certifies that you wrote the change or otherwise have the right to
submit it under the project's licence, as the [Developer Certificate of Origin 1.1](https://developercertificate.org/)
states. Read it before your first contribution. Git adds the line for you:

```bash
git commit -s
```

The name and address in the line are those of your commit's author. A pull request whose commits are not signed off
is not merged.

## Tests first

A new feature starts with a test that fails, then the change that makes it pass. A bug fix arrives with a test that
fails without it. A change that cannot be tested automatically says why in its pull request and what was checked
instead. The suites, the fixtures and the rules for test data are in
[docs/dev/guides/TESTING.md](docs/dev/guides/TESTING.md); a test never writes into the application's own data
directory.

## The rules every change keeps

AGENTS.md states them in full. The ones a first change most often meets:

- **Formatting.** Every changed C or C++ file under `src/` passes the repository's clang-format 22 check:
  `build\scripts\msvc\check_clang_format.bat origin/main` or `build/scripts/check_clang_format.sh origin/main`, and the
  `format_changed` scripts beside them fix what it finds.
- **Line endings.** Every text file the project owns uses CRLF, whatever system it is edited on. Files under
  `build/scripts/linux/` and the shell scripts run on Linux use LF. A file never mixes the two.
- **Licence headers.** Every new `.h`, `.hpp`, `.cpp`, `.c`, `.inl` and `.py` file opens with two lines:

  ```cpp
  // SPDX-License-Identifier: GPL-3.0-or-later
  // SPDX-FileCopyrightText: <year> <your name>
  ```

  with `#` in place of `//` in Python. A file of the extension protocol boundary - `src/AfterNightExtensionProtocol/`,
  `src/AfterNightExtensionHostLite/` and the protocol half of `python/modules/afternight/` - states `Apache-2.0`
  instead. `python build/scripts/tools/check_license_headers.py` checks every file, and `--apply` inserts a missing
  header.
- **Derived code.** Code copied, translated or ported from another project carries that project's credit, its
  copyright line verbatim and its licence in every file that holds it, with a provenance note and a credit in the
  About dialog. Check the upstream licence is compatible before you start: see "Credit for derived code" in AGENTS.md.
- **The interface.** No `setStyleSheet()` and no hard-coded colour, spacing, radius or font: read
  [docs/dev/THEME_SYSTEM.md](docs/dev/THEME_SYSTEM.md) before changing a widget.
- **Names.** PascalCase types and files, camelCase functions, `m_` members, `k` constants, and no new snake_case API.
- **No planning references in code.** A comment or a test message under `src/` states a reason, never an issue
  number, a branch or a plan's section.

## Validating a change

Validation runs locally. [docs/dev/LOCAL_VALIDATION.md](docs/dev/LOCAL_VALIDATION.md) lists the repository's checks
and says which a change needs: build and run the suites of what you changed, run the checks the change could affect
and the Python test beside every script you changed under `build/scripts/tools/`. Say in the pull request what you
ran, with the counts - the suites and how many cases passed - and what you could not run.

A pull request to `main` runs the continuous integration workflow: the format and licence-header checks, and the Linux
build and tests. The Windows build runs on pushes to `main`, on releases, and on pull requests that change the build
or carry the `ci:windows` label; the label is read when a run starts, so a label added to an open pull request takes
effect at its next push. A pull request that changes only documentation builds nothing, and `CI Complete` reports
on every pull request.

## Pull requests

- One change per pull request, kept small; no unrelated reformatting.
- The description says what changed and why, how it was tested, what was validated with its counts, and links the
  issue it closes.
- The [pull request template](.github/PULL_REQUEST_TEMPLATE.md) lists what a reviewer looks for.

## Reporting a problem

Open an issue with the bug report or feature request form. A bug report needs the version, the operating system, the
steps that reproduce it, the log, and the FITS header of the image involved when there is one; the form says where
to find each. A security vulnerability is never reported in a public issue: follow [SECURITY.md](SECURITY.md).
