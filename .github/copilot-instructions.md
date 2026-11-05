# AfterNight - GitHub Copilot Instructions

## Source of truth

The canonical workflow + standards for this repository live in [AGENTS.md](../AGENTS.md).
[CLAUDE.md](../CLAUDE.md) is the quick operational summary (commands, module layering, conventions), and
[docs/dev/guides/PRODUCT_ARCHITECTURE.md](../docs/dev/guides/PRODUCT_ARCHITECTURE.md) maps each area of
the product to its code, its suite and the reasons for its design.

- Follow [AGENTS.md](../AGENTS.md) for: build order, platform scripts, testing, style, theming rules, line endings,
  test-data isolation, derived-code credit, and "definition of done".
- Use the product guide, or the engine guides beside it, to locate the code and the design reasons for an area
  before changing it: a design's reasons are written in the guide for its area, and there are no decision records.
  [docs/dev/README.md](../docs/dev/README.md) indexes the developer documents.
- If anything in this file conflicts with [AGENTS.md](../AGENTS.md), treat [AGENTS.md](../AGENTS.md) as authoritative.

## Engine and registry rules

- `src/AfterNightEngine` is the engine every product target links, in `namespace an`.
- This repository is public and its `main` is the development line; work is tracked as GitHub issues. A pull request
  to `main` runs `.github/workflows/ci.yml`; validation is local, as `docs/dev/LOCAL_VALIDATION.md` describes.
- Never write a document that compares AfterNight with another product, measures it against one or plans to exceed
  one: such notes are kept privately, outside this repository. Attribution of derived code stays here.
- `build/scripts/tools/consumer_targets.txt` records every target. Build the active set with `build_active_targets`
  and the engine's own short loop with `build_afternightengine` (both under `build/scripts/<platform>/`).
- A new project needs a record in `consumer_targets.txt`; an isolated test executable compiles only its list in
  `build/scripts/tools/isolated_sources.txt`.
- Test inputs come from the fixture layers under `src/AfterNightTests/fixtures/` (`common/`, `harness/`, `engine/`,
  held by `Test_FixtureNeutral` and `Test_FixtureEngine`); the expectations under `fixtures/common/expectations/`
  are recorded reference expectations that cannot be captured again and are never regenerated ("The fixture
  layers" in [docs/dev/guides/TESTING.md](../docs/dev/guides/TESTING.md)).
- A new static, namespace-scope object or singleton in product code needs an entry in
  `build/scripts/tools/consumer_state.txt`, checked by `check_consumer_state.py`, as described in
  [AGENTS.md](../AGENTS.md).
- Code added to the engine: public headers under `include/AfterNightEngine/<Area>/`, every public declaration documented
  (`///`, no `@brief`), a self-compile unit per public header, expected failures returned as `Status`/`Result<T>`,
  no globals (`EngineContext`), every non-trivial algorithm takes an `ExecutionContext`, parallelism only through
  `Runtime/TaskScheduler.h` (no `tbb::global_control`).

## Copilot-specific expectations

- Keep diffs small and focused; avoid drive-by refactors/reformatting.
- Prefer repository automation (scripts under `build/scripts/<platform>/`, existing VS Code tasks) over inventing new build/run commands.
- When you implement a feature or fix a bug, add/adjust unit tests (TDD is mandatory; see [AGENTS.md](../AGENTS.md)).
- Do not introduce new dependencies unless explicitly requested; if unavoidable, follow the dependency workflow in [AGENTS.md](../AGENTS.md).
- Code derived from another project carries its upstream credit and licence in every file holding the logic, plus
  its provenance note, `ported` record and About dialog credit (see "Credit for derived code" in [AGENTS.md](../AGENTS.md)).

## Coding conventions (Copilot reminder)

- Naming: PascalCase types/enumerators, camelCase functions/locals, `m_` members, `k` constants, PascalCase files.
  Add no snake_case API.
- No planning-artifact references (task/milestone IDs, plan sections, contract IDs, issue numbers) in code,
  comments or test messages under `src/` or Premake files. State the reason instead.
- Write `(std::max)(...)` / `(std::min)(...)` everywhere under `src/`.
- Use Verdigris (`W_OBJECT`, `W_SIGNAL`, `W_SLOT`), not `Q_OBJECT`; never block the UI thread or the extension host's event loop.
- Log through `<AfterNightPlatform/Logging.h>` (`LOG_*`); never print to stdout/stderr.
- Every AfterNight-owned text file uses CRLF; Linux-exclusive files (`build/scripts/linux/**`, Linux shell scripts) use LF.
  Never leave mixed line endings.
- Every new `.h`, `.hpp`, `.cpp`, `.c`, `.inl` and `.py` opens with `SPDX-License-Identifier: GPL-3.0-or-later` and
  `SPDX-FileCopyrightText: <year> <author>` comment lines (`Apache-2.0` on the protocol boundary);
  `check_license_headers.py` holds it (see "Licence headers" in [AGENTS.md](../AGENTS.md)).
- Tests never write into the production storage tree (`%LOCALAPPDATA%\AfterNight\` or Linux equivalents); own a
  temporary root and set `AFTERNIGHT_TEST_STORAGE_ROOT`.

## UI guardrails (Copilot reminder)

UI styling is governed by the theme system.

- Never use QSS / `setStyleSheet()`.
- No hardcoded colors, spacing, radii or fonts: resolve through `an::ui::tokens()` (inside `paintEvent()`).
- Read `docs/dev/THEME_SYSTEM.md` before modifying/adding UI widgets.

## Validation

- Run clang-format validation for every C/C++ change under `src/`:
  `build\scripts\msvc\check_clang_format.bat origin/main` (fix with `format_changed.bat`), or
  `build/scripts/check_clang_format.sh origin/main` on Linux. Report the formatter version used.
- Build and test the affected targets; for `AfterNightEngine` changes also run the engine's checks listed in
  [AGENTS.md](../AGENTS.md) (`check_include_layers.py`, `check_forbidden_constructs.py`, …).

## Communication / output

- In your summary, list the files you changed and what you validated (format check, build, tests, scripts run).
- If blocked by missing prerequisites (Qt/deps not installed, wrong shell on Windows, etc.), state the missing prerequisite and point to the relevant script in `build/scripts/`.
