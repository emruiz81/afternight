# User Guide Authoring

> **Audience:** contributors who write, review, build or publish the end-user guide in `docs/user-guide/`.
>
> **Source of truth:** for the site, the configuration, templates, records and scripts this document names; for what
> a page says, the product's current source and tests.

The user guide is a MkDocs Material site written in Markdown. It is built from a locked toolchain, works when opened
from disk, ships inside the installers and is published to GitHub Pages with each release. This document states the
rules a change to the guide follows: where its parts live, how it is built and shipped, how a page's claims are
verified, what a complete page contains, how it is written and how its media is handled.

## Contents

1. [Layout](#layout)
2. [Building](#building)
3. [Offline site, packaging and publication](#offline-site-packaging-and-publication)
4. [Sources of truth](#sources-of-truth)
5. [Verifying in the application](#verifying-in-the-application)
6. [Page kinds and templates](#page-kinds-and-templates)
7. [What a complete page contains](#what-a-complete-page-contains)
8. [Writing style](#writing-style)
9. [Media](#media)
10. [Naming other programs](#naming-other-programs)
11. [Scope of a change](#scope-of-a-change)
12. [Definition of done](#definition-of-done)
13. [Checks](#checks)

## Layout

| Path | Holds |
| --- | --- |
| `docs/user-guide/mkdocs.yml` | The site: theme, plugins, Markdown extensions, local scripts and the `nav:` tree. |
| `docs/user-guide/requirements.in` | The direct pins of the documentation toolchain. |
| `docs/user-guide/requirements.txt` | The resolved lock, with hashes, generated from `requirements.in`. |
| `docs/user-guide/docs/en/` | The English pages; `mkdocs.yml` names this directory `docs_dir`. |
| `docs/user-guide/docs/en/assets/images/` | Committed screenshots and example images. |
| `docs/user-guide/docs/en/assets/javascripts/` | The vendored MathJax and Mermaid runtimes and the polyfills the site loads locally. |
| `docs/user-guide/docs/en/stylesheets/extra.css` | The site's style overrides. |
| `docs/user-guide/docs/en/assets/MEDIA_PROVENANCE.md` | The provenance of every committed image and retained diagram, and the offline runtime policy. |
| `docs/user-guide/templates/` | The four page templates. |
| `docs/user-guide/DOCUMENTATION_COMPLETION_TRACKER.md` | One row per navigated page with its states and evidence, the state model and the phase ledger. |
| `docs/user-guide/SCREENSHOT_REQUEST_MANIFEST.md` | The canonical media manifest, the only capture brief. |
| `docs/user-guide/CONTENT_VERIFICATION_PREREQUISITES.md` | The evidence modes, and the data, tools, catalogs and platforms a verification needs. |
| `docs/user-guide/audits/` | The audit's configuration and baseline, the media contract and retained validation evidence; its `README.md` explains the audit. |
| `docs/user-guide/site/` | The built site. Git ignores it, as it ignores the helpers' environment in `docs/user-guide/.venv/`. |

## Building

The platform helper creates a virtual environment in `docs/user-guide/.venv`, installs the lock into it with
`--require-hashes` and runs the strict build:

```cmd
build\scripts\msvc\build_user_guide_docs.bat
```

```bash
build/scripts/linux/build_user_guide_docs.sh
```

The direct commands work in any environment that has the lock installed:

```bash
pip install --require-hashes -r docs/user-guide/requirements.txt
mkdocs build -f docs/user-guide/mkdocs.yml --strict
mkdocs serve -f docs/user-guide/mkdocs.yml
```

- The strict build is the gate every change passes. It turns each warning, such as a link to a missing page or
  anchor, into a failure. It proves the site's structure, not what a page says.
- A new page is listed under `nav:` in `mkdocs.yml`. The audit reads the navigation to decide which pages it checks.
- The toolchain changes in `requirements.in`. Regenerate the lock with pip-tools, run the strict build, and commit
  both files together; the lock is never edited by hand:

  ```bash
  python -m piptools compile --generate-hashes --output-file docs/user-guide/requirements.txt docs/user-guide/requirements.in
  ```

## Offline site, packaging and publication

The installers bundle the built site, and it works opened from disk without a network:

- Every script, style and font the site uses is local. `theme.font: false` keeps web fonts out, MathJax and Mermaid
  are vendored under `assets/javascripts/` and listed in `extra_javascript`, the `offline` plugin builds the site for
  file access, and `use_directory_urls: false` makes every link name its `.html` file. Do not add a CDN script, a
  remote font or a network fallback, and do not enable `navigation.instant`, which needs a server.
- A `mermaid` fence renders through the project's local initializer: `mkdocs.yml` gives the fence the `an-mermaid`
  class, so Material's network-loaded renderer is never used.
- `.github/workflows/docs.yml` builds the site strictly from the lock and uploads it as the
  `afternight-documentation-html` artifact. It runs on pull requests that touch the guide, this document, `README.md`,
  `AGENTS.md`, the documentation, release and platform workflows, the installers, or the application's menus and help
  entry point.
- `.github/workflows/release.yml` calls the documentation workflow once per release, which then uploads the site to
  the release as a ZIP, and the platform jobs (`.github/workflows/build-windows.yml`,
  `.github/workflows/build-linux.yml`) download the artifact before packaging. Release packaging requires it: Inno
  Setup takes `/DREQUIRE_DOCS` and `installer/linux_packager.sh` takes `REQUIRE_DOCS_SITE=1`. A local or manual
  installer build bundles `docs/user-guide/site/` only when it exists. The Windows installer places the site in
  `{app}\docs` with a Start menu entry; the AppImage places it in `usr/share/doc/AfterNight/`.
- GitHub Pages carries the latest release's guide, so that the online fallback below describes the build people have
  installed. The release workflow publishes the guide it built once the release is published (`deploy-guide`; a
  pre-release leaves Pages alone). A dispatch of the documentation workflow with `deploy_pages: true` builds and
  publishes the latest release's tag, or the ref its `ref` input names. A push to `main` builds the guide and does not
  publish it.
- **Help > User Guide** (`DialogOrchestrationCoordinator::openHelp`) opens the bundled site first and falls back to
  the published guide, whose address is `kAfterNightUserGuideOnlineUrl` in
  `src/AfterNightControls/app/DialogOrchestrationCoordinator.h`. `AFTERNIGHT_DISABLE_ONLINE_USER_GUIDE=1` turns the
  fallback off for offline smoke tests; the application then says that the offline guide could not be opened.
- The guide is English only (`docs_dir: docs/en`). Adding a language moves `docs_dir` to `docs/` with one directory
  per language and pins the `mkdocs-static-i18n` plugin in `requirements.in`; an untranslated page falls back to
  English only once that migration is in place.

## Sources of truth

A page states what the product does, and each claim is verified against the product in this order:

1. **Reachability and labels:** `src/AfterNightProcesses/app/ProcessRegistryBuiltins.cpp`,
   `src/AfterNightControls/app/AfterNightAppUiBuilder.h`, `src/AfterNightControls/app/MenuActionManager.cpp`, the
   action-state code and the registration of dialogs and pages - for Settings,
   `DialogOrchestrationCoordinator::openSettingsAt`.
2. **Control behavior:** the process or control implementation, its UI builder, its parameter structs, settings keys
   and defaults, persistence code and UI tests.
3. **Algorithm behavior:** the engine's headers and implementations under `src/AfterNightEngine/`, the adapters of
   external runtimes, characterization and numerical tests, the guides in `docs/dev/guides/`, and the
   provenance notes.
4. **Workflow behavior:** the Image Integration model, workflow, stages and reporting, the sequence
   implementations, the file manager and export code, and the integration tests.
5. **Recommendations:** repeatable tests and maintainer-reviewed domain guidance. A recommendation is labelled as
   guidance, never presented as implementation fact.

Existing guide prose and old plans help find what to read; they are not sources. The guide documents reachable
behavior only: a control, page or option that exists in source but that the product does not register or reach is
not described as available, and the discrepancy is recorded as a finding.

A review is recorded in the page's tracker row: the source files and tests read, the review date, the full `HEAD`
SHA reviewed, and whether a reviewed file had uncommitted changes. When one did, the row lists the affected paths and
the diff that matters is kept as evidence. `build/scripts/audit_user_guide.ps1` reads these records to report which
reviewed sources have changed since.

## Verifying in the application

Source and focused tests come first; the running application is an escalation for what they cannot prove.

1. Derive the control and action inventory, labels, defaults, ranges, static dependencies, validation, persistence
   and algorithm behavior from source and focused tests before launching AfterNight.
2. Write a short unresolved-GUI checklist that holds only the claims source and tests cannot prove: visibility on a
   platform or build, dynamic enablement, runtime-populated state, layout, clipping and responsiveness, or an
   interaction the page must show.
3. Launch the application only when that checklist is not empty and a claim needs GUI evidence (mode `G`), or when an
   execution or a capture (modes `E` and `C`) uses the GUI. Batch the checklist into one session where feasible, use
   an isolated profile and output location, and reach each state by the most deterministic route: a menu action, a
   shortcut, a known starting state or a focused test harness. Do not explore dialogs to enumerate controls or copy
   labels that source already gives.
4. After two failed automated attempts to reach the same state, stop retrying. Use a deterministic alternative route
   or a short recorded manual inspection. A claim with enough source and test evidence is labelled mode `S`, without
   claiming a GUI check that did not happen; when mode `G` is mandatory and still cannot be produced, the blocker is
   recorded.

Opening a dialog does not prove persistence, algorithm execution, output quality or the readiness of an external
tool. Screenshots are taken through the media workflow below, not during a content review.

`docs/user-guide/CONTENT_VERIFICATION_PREREQUISITES.md` defines the evidence modes (`S`, `A`, `E`, `B`, `G`, `C`,
`P`, `H-T`, `H-U`), what each can and cannot prove, and the data, tools, catalogs and platforms each area of the guide
needs. A claim about an external tool's contract, a supported version, a standard or a licence also needs mode `A`.

## Page kinds and templates

| Kind | Template | Pages |
| --- | --- | --- |
| Process | `docs/user-guide/templates/PROCESS_PAGE_TEMPLATE.md` | `processes/` |
| Component | `docs/user-guide/templates/COMPONENT_PAGE_TEMPLATE.md` | Every other page, unless configured otherwise |
| Workflow | `docs/user-guide/templates/WORKFLOW_PAGE_TEMPLATE.md` | `workflows/` |
| Reference | `docs/user-guide/templates/REFERENCE_PAGE_TEMPLATE.md` | `reference/` |

The audit decides a page's kind by its directory; `pageKindOverrides` in
`docs/user-guide/audits/user-guide-audit-config.json` names the exceptions. The templates are frozen: a required
heading may be added, never removed. The audit reports a navigated page that lacks a heading its template requires,
and a process page whose parameter table lacks the canonical columns;
`build/scripts/validate_ug00_user_guide_foundation.ps1` holds the templates to their rubric.

A page renders no authoring evidence. Its owner, review date and revision, dirty reviewed sources, sources and tests,
coverage checks, content, review and media states and last strict build are recorded in its tracker row alone, and
each template opens with a comment that sends the author there; `build/scripts/validate_ug00_user_guide_foundation.ps1`
refuses an evidence section or row in a template or a page, and `build/scripts/validate_user_guide_media.ps1` checks
the tracker's media states. What a reader needs - provenance, compatibility and version statements, references, the
limits of a worked example, troubleshooting evidence - stays in the page's reader-facing sections, and a citation of
the implementation stays only where it explains behavior.

## What a complete page contains

### Every page

A review-ready page states:

- its purpose, audience, prerequisites and expected result;
- the exact path to the feature and the conditions that enable it;
- a task-oriented procedure with an observable checkpoint after each meaningful step;
- the inputs and outputs, the image and sequence states it supports, its effect on metadata, and what it persists;
- warnings about destructive or irreversible actions, linear and nonlinear data, masks, clipping, color changes,
  memory, disk and external dependencies;
- troubleshooting tied to the product's actual validation and error paths;
- links to the pages for the previous and the next workflow step;
- reviewed terminology, with no generic filler copied from another page.

### Every process page

A process page also has:

1. **At a glance:** the menu path, input and output types, the linear or nonlinear recommendation, preview, sequence
   and mask support, and external dependencies.
2. **Before you start:** prerequisites and data preparation.
3. **A complete parameter and action reference:** one row per visible control and meaningful action, with:

   | Field | Meaning |
   | --- | --- |
   | UI label | The exact current label. |
   | Type and unit | Checkbox, selector, integer, normalized float, pixels, sigma, degrees, percent, path, image selector, and so on. |
   | Default | The runtime default, and any persisted default that overrides it. |
   | Valid values | Minimum and maximum, enumerated values, step, or file and type constraints. |
   | Effect | What increasing, decreasing or selecting it changes. |
   | Dependencies | When it is enabled, hidden, ignored or exclusive of another control. |
   | Trade-off | The quality, artifact, memory, disk and runtime consequences. |
   | Scope | Preview or apply, the selected channel, the mask, a still image or a sequence. |

4. **How the algorithm works:** the conceptual pipeline, the equations that matter, the estimators, kernels and
   transforms, channel and color handling, boundary and invalid-pixel behavior, numerical precision, and the external
   implementation where one is used.
5. **Parameter interactions:** the combinations that matter, conservative starting points, and what settings that are
   too weak or too strong look like.
6. **Outputs and metadata:** how many images, their names and pixel type, metadata and history, auxiliary maps and
   files, and whether the source changes.
7. **Performance:** the main costs and safe ways to prototype.
8. **A worked example:** the input condition, the settings, the expected observation, and what to inspect at 100%
   zoom or in statistics and history. It says whether it was executed with project-owned data or is
   source-and-test-derived, and gives no unverified quantitative or visual result.
9. **Troubleshooting:** the process's errors and how to diagnose its artifacts.
10. **References:** the primary papers, upstream algorithm documentation, standards and project provenance that
    support the explanation.

An equation helps the reader predict behavior: every symbol is defined, and the equation is paired with a
plain-language reading. Incidental C++ detail stays out unless it affects results, compatibility or performance.

### States

The tracker's "Independent state model" defines a page's states. A page that meets the rubric above is a
review-ready `Draft`. It becomes `Content complete` only after a human technical review and a human user review, each
of the page itself or of a recorded review cohort, and a current strict build. Media is a separate dimension: a page
can be `Content complete` while required media is still missing, and it is fully complete only when that media is
`Validated`.

## Writing style

- Write for intermediate astrophotographers who know basic imaging but may be new to AfterNight: professional and
  approachable.
- Address the reader as "you", never "we" or "I".
- Describe behavior in the present tense and give instructions in the imperative.
- Write a UI label in bold, exactly as the current build shows it: **Strength**.
- Write a menu path in bold with `>` between levels: **Process > Preprocess > Image Integration Lite**.
- Write a keyboard shortcut as a code span with `+` between keys, as the binding defines it: `Ctrl+Shift+S`.
- Define a technical term where it is first used and add it to `docs/user-guide/docs/en/reference/glossary.md`.
- Write math as LaTeX, `\( ... \)` inline and `\[ ... \]` for display; the vendored MathJax renders it.
- Use Material admonitions for asides: `!!! note` for supplementary information, `!!! tip` for practical advice,
  `!!! warning` for a common mistake or risk, `!!! danger` for behavior that can silently give a wrong result or lose
  work.
- Link related processes and workflows with relative links to their `.md` files, which the strict build checks.
- Keep sentences short and plain, without idioms, slang or cultural references, so that pages translate.

## Media

`docs/user-guide/SCREENSHOT_REQUEST_MANIFEST.md` is the only capture brief. Each retained asset has one row: a stable
Media ID, the owning capture phase, the canonical repository path, the consumer pages, the editorial role, the
disposition (`Keep` or `Deferred`), the reader question, the distinctive state to show, the evidence gate, the
privacy boundary, the capture status and the replacement trigger.

- A consumer page of a retained asset that does not exist yet carries only `<!-- MEDIA_SLOT: UG-MEDIA-### -->` with
  the asset's Media ID, and never links the missing file. Capture instructions belong in the manifest row, not in page
  comments.
- A page whose media was triaged as not needed has no slot. A deferred asset has no slot on a release page unless the
  page explains the deferral, and is never presented as verified.
- Pages that share a visual use the same Media ID and canonical file; there is no alias or duplicate file.
- When the asset exists, replace the slot on every consumer page with a Markdown image and a caption written for the
  reader, record its provenance, and only then advance the manifest's capture status (`Planned`, `Ready`, `Blocked`,
  `Captured`, `Placed`, `Validated`) and the tracker's media state. The tracker's ledger row for the owning capture
  phase follows the manifest: `Not started` while every kept asset the phase owns is `Planned` or `Blocked`,
  `In progress` once one is `Ready` or further, and `Agent complete` when all are `Validated`.
- `docs/user-guide/docs/en/assets/MEDIA_PROVENANCE.md` records every committed screenshot, result image, diagram, GIF,
  WebM or other asset with its Media ID (or `Not applicable`), path, creator or source, licence or permission, date,
  SHA-256, update procedure and sample-data boundary. A retained inline diagram is recorded by the SHA-256 of its
  source with LF line endings: the complete Mermaid fence and its trailing newline.
- Prefer captures and processed examples made by the maintainer from data the project may publish. A capture that
  depicts a result also needs reproducible settings, approved data and a bounded claim. Do not commit third-party
  astrophotography images, catalog screenshots, captures of other applications or downloaded media unless
  redistribution in the repository and the installers is explicitly allowed. Unknown permission or missing evidence
  keeps the row `Blocked`; it never justifies a linked file or a capture brief in a page.
- A capture shows only what its row's privacy boundary allows: never credentials, tokens, licence secrets, private
  repositories or endpoints, device serials or unrelated personal data.
- `docs/user-guide/audits/user-guide-media-contract.json` states the expected counts: the original requests and the
  canonical rows, the `Keep` and `Deferred` rows, each capture phase's kept, deferred and still-missing assets, the
  merged and not-needed decisions, and the negative fixtures. A change to a row's disposition, phase or capture status
  updates the contract in the same change; `build/scripts/validate_user_guide_media.ps1` fails while the manifest,
  the pages, the tracker, the provenance and the contract disagree.
- Whether a page needs an image at all follows the rubric in `docs/dev/USER_GUIDE_SCREENSHOT_TRIAGE.md`: by default no
  screenshot, at most one on a typical page, and a second only for mutually exclusive states, a meaningful
  before-and-after comparison or a setup-and-completion sequence. A readable crop of the distinctive region is
  preferred to a full-window inventory of controls.
- Prefer a Mermaid diagram for a pipeline, a data flow, an algorithm or a decision.
  `build/scripts/validate_ug65_user_guide_diagrams.cjs` renders the built pages that carry a retained diagram in a
  Chromium-based browser at phone, tablet and desktop widths in both color schemes, and fails on small text,
  overflow, clipping, low contrast, page errors or a Mermaid request that leaves the site. A page that gains or loses
  a retained diagram is added to or removed from its page list.
- Media is committed as ordinary files. Git LFS is not used; adopting it is a separate change to `.gitattributes`, CI
  and contributor setup together.

## Naming other programs

A page names another astrophotography program only to credit derived work or to state compatibility with a file
format or a behavior the product reproduces; it never compares AfterNight with one. The page of a derived process
credits the upstream work, its authors and its licence in its references and provenance section, as "Credit for
derived code" in `AGENTS.md` requires, and the sentence that names the program says why it is named - "derived
from", "ported from", "reads the files it writes".

## Scope of a change

- Do not change application or engine behavior to make a page true. A discrepancy between source and documentation
  is recorded as a finding, and a product fix is its own change.
- Do not invent controls, defaults, algorithms, citations, screenshots, example results, validation output or reviewer
  approval.
- Use primary sources for external algorithms and standards, and keep verified implementation behavior apart from
  general recommendations.
- The pages a change sets out to write are its deliverables. Navigation, the process index, the glossary, the
  tracker, the manifest, provenance and reciprocal links are synchronized as far as the change requires, without
  pulling another page into a rewrite; a deficit outside the change stays recorded in the tracker.
- The audit changes deliberately. `build/scripts/audit_user_guide.ps1`, its configuration and its baseline change
  only for an intentional change of structure, navigation, tracker metadata, headings, parity or coverage, after the
  report has been generated and the difference reviewed ("Baseline policy" in `docs/user-guide/audits/README.md`).
  Its output stays deterministic, and `build/scripts/validate_ug01_user_guide_audits.ps1` runs with the change.

## Definition of done

A change to the guide is done when:

1. Every page it writes or substantially rewrites meets the rubric for its kind as a review-ready draft, and every
   synchronization edit is accurate and introduces no contradiction.
2. Every visible control and action in scope is accounted for, with its default, range, unit, dependencies, preview
   and apply behavior, persistence and outputs.
3. Each algorithm explanation is verified against the engine's source and tests.
4. Menu paths, enabling conditions, image state, sequence support, mask behavior, external prerequisites, errors and
   performance statements are accurate.
5. Any GUI evidence comes from a recorded unresolved-GUI checklist and a targeted inspection.
6. Navigation, the process index, the glossary, related links, the tracker, the manifest rows, their consumer lists
   and the `MEDIA_SLOT` placements agree.
7. No generic placeholder prose remains in new or rewritten content.
8. The strict build passes.
9. `git diff --check` passes and the diff holds nothing unrelated.
10. The page carries no authoring evidence; its tracker row does.
11. The tracker records the sources and tests reviewed, the reviewed SHA and dirty-source state, the content and media
    states, the build command and date, and the reviews still owed.
12. The checks below pass.

## Checks

Run from the repository root; the PowerShell scripts take Windows PowerShell:

| Command | Checks |
| --- | --- |
| `build\scripts\msvc\build_user_guide_docs.bat`, `build/scripts/linux/build_user_guide_docs.sh` | The strict build from the lock. |
| `powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\validate_user_guide_media.ps1 -RunFixtures` | The media contract: manifest rows, slots and links, tracker media states and capture-phase rows, provenance hashes and the per-phase counts; the fixtures inject eight known failures and require each to be refused for its reason. |
| `powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\validate_ug00_user_guide_foundation.ps1` | The tracker's page rows and phase ledger, the templates' rubric and their pointer to the tracker, that no template or page carries authoring evidence, the prerequisite inventory, and that these files and this document carry no trailing whitespace. |
| `powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\validate_ug01_user_guide_audits.ps1` | The media validator with its fixtures, the audit against its structural baseline, and the audit's parity and source-drift fixtures. |
| `powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\audit_user_guide.ps1 -OutputPath <report.md>` | Writes the audit report: registry and menu parity, required headings, source-review records and drift, boilerplate and parameter tables. |
| `node build\scripts\validate_ug65_user_guide_diagrams.cjs` | The rendering of retained diagrams in the built site; the script's header gives its setup, which keeps its Node packages outside the repository. |
