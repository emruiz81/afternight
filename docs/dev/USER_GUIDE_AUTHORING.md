# User Guide Authoring

> **Audience:** contributors who write, review, build or publish the end-user guide in `docs/user-guide/`.
>
> **Source of truth:** for the site, the configuration, templates, records and scripts this document names; for what
> a page says, the product's current source and tests.

The user guide is a MkDocs site in a theme of its own, written in Markdown. It is built from a locked toolchain,
works when opened from disk, ships inside the installers and is published to GitHub Pages with each release. This document states the
rules a change to the guide follows: where its parts live, how it is built and shipped, how a page's claims are
verified, what a complete page contains, how it is written and how its media is handled.

> **Frozen while the guide is rebuilt ([#37](https://github.com/emruiz81/afternight/issues/37)).** The audit
> apparatus - `build/scripts/audit_user_guide.ps1`, `validate_user_guide_media.ps1`,
> `validate_ug00_user_guide_foundation.ps1`, `validate_ug01_user_guide_audits.ps1` and
> `validate_ug65_user_guide_diagrams.cjs`, the completion tracker, the screenshot request manifest, the
> content-verification prerequisites, `docs/user-guide/audits/` and `docs/dev/USER_GUIDE_SCREENSHOT_TRIAGE.md` - is
> frozen: nobody runs or updates it, no CI job runs it, and the rebuild removes it. What this document asks of a
> tracker row, a manifest row, a `MEDIA_SLOT` comment or an audit run is suspended with it. The strict build,
> `check_user_guide.py` and the render tool hold the guide meanwhile ([Checks](#checks)).

## Contents

1. [Layout](#layout)
2. [Building](#building)
3. [Page metadata](#page-metadata)
4. [The guide's own theme](#the-guides-own-theme)
5. [Offline site, packaging and publication](#offline-site-packaging-and-publication)
6. [Sources of truth](#sources-of-truth)
7. [Verifying in the application](#verifying-in-the-application)
8. [Page kinds and templates](#page-kinds-and-templates)
9. [What a complete page contains](#what-a-complete-page-contains)
10. [Writing style](#writing-style)
11. [Media](#media)
12. [Naming other programs](#naming-other-programs)
13. [Scope of a change](#scope-of-a-change)
14. [Definition of done](#definition-of-done)
15. [Checks](#checks)

## Layout

| Path | Holds |
| --- | --- |
| `docs/user-guide/mkdocs.yml` | The site: the theme's keys, the hook file, the one plugin, the Markdown extensions, the local runtimes and the `nav:` tree. |
| `docs/user-guide/requirements.in` | The direct pins of the documentation toolchain. |
| `docs/user-guide/requirements.txt` | The resolved lock, with hashes, generated from `requirements.in`. |
| `docs/user-guide/docs/en/` | The English pages; `mkdocs.yml` names this directory `docs_dir`. |
| `docs/user-guide/data/` | The guide's data: `stages.yml`, the stages of the pipeline that pages name ([Page metadata](#page-metadata)). |
| `docs/user-guide/hooks/` | The theme's MkDocs hooks - `guide_hooks.py` (the events, and the only file `mkdocs.yml` names), `guide_fences.py` (the `controls` and `figure` blocks), `guide_html.py` (the passes over each page's HTML) and `guide_search.py` (the search index) - and `guide_model.py`, the schema of the pages' front matter and the readers of the guide's data and navigation, which the hooks and `check_user_guide.py` share; their tests beside them. |
| `docs/user-guide/hooks/fixtures/components/` | A small made-up guide that shows every page kind and component of the theme. The hooks' tests build it strictly, and it is the site to look at while changing the theme ([Components and blocks](#components-and-blocks)). |
| `docs/user-guide/theme/` | The theme: `main.html`, `base.html`, `404.html`, a layout per page kind in `layouts/`, the shared parts in `partials/`, and under `assets/guide/` its CSS (`tokens.css` a copy of the website's), JavaScript, fonts and the vendored lunr. |
| `docs/user-guide/docs/en/assets/images/` | Committed screenshots and example images. |
| `docs/user-guide/docs/en/assets/javascripts/` | The vendored MathJax runtime with its fonts and configuration, and the Mermaid runtime with its initializer. |
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

- The strict build is the gate every change passes. It turns each warning into a failure: a link to a missing page
  or anchor, a page left out of the nav (`validation` in `mkdocs.yml` raises all three to warnings), front matter
  that does not fit its schema, a `controls` or `figure` block that cannot be drawn. It proves the site's structure,
  not what a page says.
- `AFTERNIGHT_GUIDE_VERSION` names the version the build states in its footer and on its home page. A release sets
  it; without it the guide says it was built from the development line.
- A new page is listed under `nav:` in `mkdocs.yml` and opens with front matter ([Page metadata](#page-metadata));
  `check_user_guide.py` holds both.
- The toolchain is four direct pins in `requirements.in`: MkDocs, pinned exactly because MkDocs 2.x loads neither
  this theme nor its hooks; the lightbox plugin; the PyMdown extensions; and Pygments, which colors code and which
  nothing else requires. It changes there. Regenerate the lock with pip-tools, run the strict build, and commit both
  files together; the lock is never edited by hand:

  ```bash
  python -m piptools compile --generate-hashes --output-file docs/user-guide/requirements.txt docs/user-guide/requirements.in
  ```

## Page metadata

Every page opens with YAML front matter that states what the guide's theme and checks need to know about it. MkDocs
reads it and keeps it out of the rendered page. `docs/user-guide/hooks/guide_model.py` holds its schema, which the
guide's hooks and `check_user_guide.py` share, so the build and the check read one definition.

```yaml
---
kind: process
stage: stretch
menu: Process > Intensity Transformations > Histogram Transformation
summary: >-
  Maps the image through a shadows, midtones and highlights transfer, the controlled step from a linear master to a
  visible picture.
data_in: linear
data_out: nonlinear
---
```

| Key | Pages | Value |
| --- | --- | --- |
| `kind` | Every page | `home`, `catalog`, `process`, `interface`, `tutorial`, `workflow` or `reference`. It picks the page's layout and the rules it is held to. |
| `summary` | Every page but the home page | One sentence of at most 40 words saying what the page is for. |
| `stage` | Process | The stage of `data/stages.yml` where the process belongs. |
| `stages` | Workflow; a tutorial that walks through part of the pipeline | The stages it spans, in pipeline order. |
| `menu` | Process; any other page may state it | The menu path as the application shows it, with ` > ` between the levels, or a list of paths. |
| `data_in`, `data_out` | Process | `linear`, `nonlinear`, `either` or `cfa`: the state the process expects, and the state its result is recorded in. Stated only where the page establishes it. |
| `derived` | Process | `true` when the process's result is computed, in whole or in part, by another project's code, by a bundled upstream library such as G'MIC, or by an external tool or service, or needs licensed data. The page credits that work ([Naming other programs](#naming-other-programs)). |
| `needs` | Tutorial, workflow | What it needs from outside AfterNight; an empty list when nothing. |
| `destructive`, `preview`, `sequences`, `masks`, `external`, `glance`, `controls_ignore` | Process | What the page's "At a glance" shows, and the window's controls the page leaves out on purpose. Stated when the page is rewritten on the new templates. |
| `hero`, `notice`, `paths`, `popular` | Home | What the home page is drawn from ([Pages drawn from data](#pages-drawn-from-data)). |
| `reach` | Catalog | The ways to reach a process, a list of sentences under the catalog's summary. |
| `rewritten` | Every page | `true` once the page is rewritten on the new templates. |

- **One source per fact.** An unknown key is an error. A fact that has a source of its own is not repeated in front
  matter: the title is the page's H1 and its nav label, the category is its nav group, related pages are links in its
  text, and its length budgets belong to the guide's limits. Duplicated facts drift.
- **Stages.** `docs/user-guide/data/stages.yml` lists the stages of the pipeline in order - Calibrate, Register, Stack,
  Gradients & color, Noise & stars, Stretch, Finish - and Any time last, for the tools no one stage owns. Each has a
  short description and a hue for each theme, readable at 4.5:1 or better on the backgrounds it is drawn on. A process
  page's stage is its own, not its menu category's: most noise reduction, StarNet and Star Mask work on linear data
  before the stretch, and their pages say so, so a stage per category would teach the wrong order.
- **Menus.** `check_user_guide.py` reads the application's menus from `AfterNightAppUiBuilder.h` and its registered
  actions from `ProcessRegistryBuiltins.cpp`. Every `menu` path a page states must exist. A path may name a submenu,
  which covers every action under it, as the Non-Interactive Processes page names the submenus of immediate actions.
  Every registered action, and every other action of the Process menu, must sit on some page's `menu`, so a process
  cannot ship without a page.

## The guide's own theme

The guide is drawn by a theme of its own that wears the website's look: the templates and assets under
`docs/user-guide/theme/`, the hooks under `hooks/` and the data under `data/`. It replaced MkDocs Material, which is
no longer installed.

- **MkDocs 1.6.1, a custom theme.** `theme.name: null` with `custom_dir: theme`; with no named theme MkDocs reads no
  `mkdocs_theme.yml`, so the theme's keys are in `mkdocs.yml`. Material for MkDocs is in maintenance mode, and its
  layout fought the design. MkDocs 2.x loads neither a 1.x theme nor hooks, so MkDocs is pinned exactly; the hooks
  stay small, plain Python, so a later move costs little.
- **One hook file, one plugin.** `mkdocs.yml` names `hooks/guide_hooks.py` alone: MkDocs puts a hook's folder on the
  import path while it loads it, so the file imports its siblings and registers the `controls` and `figure` blocks
  itself, and the build needs no `PYTHONPATH`. The only plugin is the lightbox; naming `plugins:` at all is what
  keeps MkDocs' own search plugin out.
- **A layout per kind.** `main.html` extends `layouts/<kind>.html` (workflows share the tutorial layout); the page's
  front matter picks it. Interface and reference pages render the plain anatomy: left navigation, breadcrumbs, the
  page head (the H1, lifted out of the content, with the summary and the chips), the lead, the body, the table of
  contents. A process page adds its position and its "At a glance" card between the lead and the body; a tutorial or
  workflow has two columns, its table of contents under the left navigation, so that its steps and their figure get
  the right rail's width; the home page and the catalog are drawn from data
  ([Pages drawn from data](#pages-drawn-from-data)).
- **The stage stepper is on every page**, all eight stages, linked to their lanes of the catalog: it marks a process
  page's stage, the stages a tutorial or workflow spans, and leads the catalog and the home page.
- **The hooks** (`guide_hooks.py`) read `data/stages.yml` and `AFTERNIGHT_GUIDE_VERSION` (the footer's version, or
  "development build"), lift each page's H1, wrap tables and make code blocks and display equations reachable by the
  keyboard, collect the search index, and hand the layouts `guide` (the data every page shares) and `gp` (the page's).
  Every page whose front matter does not fit the schema is a warning, which the strict build counts; only a data file
  that cannot be read stops the build.
- **The website's skin.** `theme/assets/guide/css/tokens.css` is a verbatim copy of the website's
  `assets/css/tokens.css`, and `guide-tokens.css` holds what only the guide needs. Two files do not justify a shared
  package; rule R16 of `check_user_guide.py` holds the copy to the website's file where both are at hand. The stage
  hues come from `data/stages.yml`, written into each page by `base.html`, so they have one source.
- **Fonts are self-hosted:** Inter, Sora and JetBrains Mono, Fontsource's Latin subsets, the same files as the
  website's, under the SIL Open Font License. No font, script or stylesheet comes from the network.
- **Search without MkDocs' search plugin.** A page opened from disk can neither `fetch()` a file nor start a worker,
  which the plugin's client needs. The hooks write `search/search_index.js`, one entry per section of every page, as
  a script that sets `window.__guideSearchIndex`; `search.js` loads it and the vendored lunr 2.3.9 on the first focus
  of the box or `/`, and indexes it then. lunr's separator is `[\s\-,:!=\[\]()"/]+`, so "image-integration" and "1.5"
  split as a reader expects. Results are grouped by the guide's section; a process shows its menu path; the keys are
  `/`, Up, Down, Enter and Esc.
- **The theme follows the reader.** An inline script in the head sets `data-theme` before the first paint, from the
  choice the toggle stored (read inside `try`, since a browser may keep no storage for a local file) or else from the
  system's colour scheme; without a stored choice the page follows the system as it changes. The toggle dispatches
  `an-themechange` for whatever draws in theme colours.
- **Breakpoints.** Three columns from 1100 px, two from 820 px, one below; the header's section links move behind
  the menu button below 1024 px, which opens the left navigation as a drawer. Anything wider than the column - a
  table, a code block, an equation, a diagram - scrolls in its own box, and the page is never wider than the window.

### Components and blocks

A page is plain Markdown. The theme draws some of it as components, found by where it sits, so that a page stays
readable as text. `guide_html.dress_sections` does this by the words of each `##` heading:

| Where | Markdown | Drawn as |
| --- | --- | --- |
| Process page, `## Steps` | An ordered list; each item a bold lead and a sentence or two | Numbered step cards |
| Any page, `## When it goes wrong` or `## Troubleshooting` | A definition list: the term is the symptom, the definitions `**Cause.** ...` and `**Fix.** ...` | Troubleshooting cards |
| Tutorial or workflow, `## What you need` | A task list (`- [ ] ...`) | A checklist the reader ticks, with its progress; nothing is stored |
| Tutorial or workflow, `## Your camera` or `## Your data` | A list of two items, each a bold lead and its text | Branch cards, side by side |
| Tutorial or workflow, `## Steps` | One `###` heading per step, after an optional `figure` block | Step cards beside the figure, which stays in view from 1100 px; a step the figure does not pin has a plain number |
| Tutorial or workflow, `## The result` | Text | The result box |
| Tutorial or workflow, `## Next` | A list of links | Cards |
| Any page | `!!! see "You should see"` and `??? wrong "If it goes wrong: <symptom>"` | The green checkpoint and the folded amber box, beside `note`, `tip`, `warning` and `danger` |
| Any page | A table of six columns or more | A smaller table whose first column stays in view while the rest scrolls, until the page is rewritten without it |

Process steps are a list and tutorial steps are headings on purpose: a tutorial's steps get anchors, search entries
and the home page's step track, and a process's short steps read best as a list.

Two fenced blocks hold what Markdown cannot say. Both are YAML read with PyYAML's base loader, so `Off`, `Yes` and
`No` stay the words a window shows. `guide_fences.py` registers them with SuperFences, which would swallow an
exception and leave stray text: a block that cannot be drawn becomes a visible error box and a warning that names the
page and the line, so `mkdocs serve` shows it and the strict build refuses it.

````markdown
```controls
window: Histogram Transformation
controls:
  - name: Shadows
    kind: spinbox
    default: "0.000000"
    widget: {min: 0, max: 1, step: 0.01, decimals: 6}
    range: Not forced below Highlights.
    effect: Higher maps more low samples to black and rescales the rest; lower keeps more shadow range.
    tradeoff: Raising it can erase faint signal for good.
  - name: Channel
    kind: combo
    default: RGB
    options: [RGB, Red, Green, Blue]
    effect: Chooses the channel the sliders move.
```
````

- **`controls`** holds one window's controls: a mapping of `window` and `controls`, or a plain list on a page with
  one window. A control states `name`, `kind`, `default` and `effect`, and may state `range`, `enabled`, `tradeoff`,
  `scope`, `persistence` and `note`. `kind` is `spinbox`, `slider`, `combo`, `check`, `radio`, `toolbutton`, `button`,
  `field` or `action`. A spin box or slider needs `widget.min` and `widget.max` (and may state `step` and
  `decimals`), with a numeric default inside them; a combo or radio needs `options` that hold its default. Names are
  unique in a block. Each control is drawn as the widget it is, at its default, with its facets beside it, and gets
  the anchor `ctl-<name>`, which the page's table of contents lists. The facets take `code` and `**bold**` and no
  other Markdown. A parameter table of ten columns cannot be read on a page; a row per control can, and it holds the
  numbers a later check compares with the window.
- **`figure`** is a picture with `image` (a path relative to the page), `alt` and `caption`, all required, and
  optional `pins`: `{n, x, y}`, numbered from 1 to match the page's steps and placed in percent of the picture's
  width and height. With `images:` - two entries of `image`, `alt` and `label` - it is a before-and-after pair,
  side by side. The picture must exist; the block writes its size, so the page does not jump while it loads, and its
  own lightbox link, so the lightbox plugin does not wrap the image and shift the pins. A quoted value keeps a comma
  or a colon in a flow mapping: write a pair's entries as block mappings when their `alt` has one.

````markdown
```figure
image: ../../assets/images/processes/stretching/histogram-transformation.webp
alt: The Histogram Transformation window with the source and predicted histograms and the real-time preview.
caption: The source histogram below, the predicted result above, the preview on the right.
pins:
  - {n: 2, x: 91.0, y: 27.5}
```
````

The fixture guide under `docs/user-guide/hooks/fixtures/components/` uses every component, block and kind:

```bash
mkdocs build -f docs/user-guide/hooks/fixtures/components/mkdocs.yml -d <a folder outside the repository> --strict
```

It reads the guide's theme, hooks and data in place (`extra.guide_root` names the guide's folder for a configuration
that is not beside it). It loads neither MathJax nor Mermaid, which are checked on the guide's own pages.

### Pages drawn from data

- **A process page's position.** "Where it belongs" shows the stage before, the page's own and the stage after, each
  leading to its lane of the catalog, and a bar of the seven stages. `data/stages.yml` states where the image stands
  at each (`data`: `linear` through Noise & stars, `turn` at Stretch, `nonlinear` at Finish), and the page's
  `data_in` and `data_out` are shown beside it. A page of Any time has one card and no marked stage.
- **"At a glance"** is drawn only on a page that states `glance`: its menu paths, the image it takes and leaves, what
  becomes of the target, its preview, sequences, masks and tools, then `glance`'s `enabled`, `inputs`, `outputs`
  and `state`. A page not yet rewritten keeps the "At a glance" section it wrote. The head's chips show the same
  facts in short: "Replaces the target" when `destructive` is true.
- **The catalog** ignores its own body. It lists every process page in the lane of its `stage`, the lanes in pipeline
  order with Any time last; inside a lane, by nav group - the Process menu's category - in nav order. A row is the
  nav label, the page's `summary`, its `menu` paths and `data_in` and `data_out`. A stage per category would teach
  the wrong order, so the category is the tag and the stage is the lane. Its `reach` sentences follow its summary.
  A link to a lane is `processes/index.md#stage-<id>`.
- **The home page** ignores its own body too:

  ```yaml
  hero:
    accent: finish               # words of the H1 drawn in the accent color; optional
    lede: One or two sentences under the title.
    actions:                     # one to three; the first is the main one
      - {label: Run the quick start, page: getting-started/quick-start.md}
    image: assets/images/interface/hero-screenshot.png   # with alt and caption, or none of the three
    alt: What the picture shows.
    caption: Its caption.
  notice: A sentence the reader must not miss.
  paths:                         # one to four cards
    - {tag: First session, title: Install and launch, text: What the card leads to., page: getting-started/installation.md}
  popular:
    - getting-started/quick-start.md
  ```

  A page is named by its path under the docs folder, as the nav names it, and its title and address come from the
  nav; a page or picture the guide does not have fails the strict build. The title is the page's H1. The stages show
  their first four processes; the sections and their page counts come from the nav; and "First stack" lists the
  `###` steps of the first tutorial in the nav that states `stages`, and is left out while that tutorial has none.
- **The search** holds the summary of a page drawn from data, not a body the page does not show.
- **Diagrams follow the theme.** `mermaid-init.js` gives Mermaid the theme's tokens as its colors and draws each
  diagram again, from the source it kept, when the toggle dispatches `an-themechange`.
- **Code is dark in both themes**, like the application's console, so the highlighter has one set of colors.

## Offline site, packaging and publication

The installers bundle the built site, and it works opened from disk without a network:

- Every script, style, font, image and data file the site uses is inside it. The theme's own are under
  `theme/assets/guide/`, which MkDocs copies to the site's `assets/guide/`, apart from the docs' `assets/images` and
  `assets/javascripts`; MathJax and Mermaid are vendored under `assets/javascripts/` and listed in
  `extra_javascript`. Do not add a CDN script, a remote font or a network fallback: rule R1 of `check_user_guide.py`
  refuses a reference that leaves the site, and the render tool a request that does.
- Nothing the site does needs a server. A page opened from disk can neither `fetch()` a file nor start a worker nor
  load a module script, so data reaches a page as a plain script - the search index is one - and the theme's scripts
  are plain scripts. `localStorage` is read and written inside `try`: a browser may keep none for a local file, and
  the theme then follows the system's color scheme.
- `use_directory_urls: false` makes every link name its `.html` file, since a folder opened from disk has no index.
  The page-relative paths of the `figure` blocks depend on it too.
- `404.html` is the one page that does not work from disk: MkDocs writes its references from the site's root, for the
  web server that shows it at any address. R1 resolves them from the root, and the render tool serves the page over
  the loopback address when it is named.
- MathJax's fonts and font data are vendored with it, under `assets/javascripts/mathjax/fonts/`, and
  `assets/javascripts/mathjax/mathjax-config.js`, listed before MathJax, points it at them. MathJax otherwise fetches
  them from jsDelivr, and an equation opened offline renders with wrong glyphs. The configuration also turns semantic
  enrichment off, because its speech worker is made from a `blob:` URL that a page opened from disk may not load;
  assistive MathML gives screen readers each equation instead. The fonts are updated with MathJax, to the font
  package version its release names, as their `MEDIA_PROVENANCE.md` row says.
- A `mermaid` fence renders through the project's local initializer: `mkdocs.yml` gives the fence the `an-mermaid`
  class, and `mermaid-init.js` draws it with the vendored runtime.
- `.github/workflows/docs.yml` builds the site strictly from the lock, runs the hooks' tests and the check's, holds the
  pages and the built site with `check_user_guide.py --site`, and uploads the site as the
  `afternight-documentation-html` artifact. Called by a release it builds with `AFTERNIGHT_GUIDE_VERSION` set to the
  release's version. It runs on pull requests that touch the guide, this document, `README.md`,
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
  pre-release leaves Pages alone). A dispatch of the documentation workflow with `deploy_pages: true` publishes
  the guide the latest release attached (`AfterNight-Documentation-v<version>.zip`), as that release built it; the
  workflow never checks out or runs another ref's code, which code scanning holds against a default-branch run. A
  push to `main` builds the guide and does not publish it.
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
- Use admonitions for asides: `!!! note` for supplementary information, `!!! tip` for practical advice,
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

Run from the repository root:

| Command | Checks |
| --- | --- |
| `build\scripts\msvc\build_user_guide_docs.bat`, `build/scripts/linux/build_user_guide_docs.sh` | The strict build from the lock. |
| `python build/scripts/tools/check_user_guide.py --site docs/user-guide/site`, with its test `python build/scripts/tools/check_user_guide_test.py` and the hooks' `python -m unittest discover -s docs/user-guide/hooks -p "*_test.py"`, all run with the toolchain's Python | Each page's front matter, nav entry, H1, menu paths and images, and what the built site references; `docs.yml` runs all three after the strict build. |
| `python build/scripts/tools/check_user_guide.py --tokens <the website's assets/css/tokens.css>` | That the theme's tokens are still the website's (R16), where the website's sources are at hand; not in CI. |
| `node build/scripts/render_user_guide.cjs --offline` | What a reader sees in the copy the installers bundle; the script's header gives its setup, which keeps its Node packages and browsers outside the repository. |

`check_user_guide.py` reads the guide through `guide_model.py`, which needs PyYAML, so it runs with the documentation
toolchain's Python: `docs\user-guide\.venv\Scripts\python` on Windows and `docs/user-guide/.venv/bin/python` on
Linux, the environment the build helper makes. It prints each finding as `path:line: Rnn message`, then a count per
rule and what it read, and exits 1 on any error. `--page <page>` holds only the pages named; the parts that need the
whole guide - nav entries, menu coverage and unused images - then do not run.

- R2 holds each page's front matter to its schema and the stage ids to `data/stages.yml`.
- R3 holds every page to the nav and to exactly one H1, and every nav entry to a page.
- R4 holds the `menu` paths to the application's menus, and every registered action, and every other action of the
  Process menu, to some page's `menu` ([Page metadata](#page-metadata)).
- R5 holds every image a page shows to a file spelled in its own case, and warns about an image under
  `assets/images` that no page, `README.md`, logo or favicon shows.

Rule R1 holds the built site: every relative reference in its pages and stylesheets names a file inside the
site, spelled in the file's own case, and nothing the site loads comes from the network; a link a reader follows may
lead anywhere. A folder does not count as a file, because a browser opened from disk lists a folder instead of
opening its `index.html`, and a root-relative reference is refused, because it breaks from disk. The exception is
`404.html`: a web server shows it at whatever address was missing, so MkDocs writes its references root-relative, and
they are resolved from the site's root.

`render_user_guide.cjs` opens every page of the built site from disk, in Chrome and Firefox, at 1440, 1024 and 390 px,
in both color schemes. It fails a load on a request that leaves the site, a console or page error, a local file that
does not load, a page wider than the window, an image or a font face that did not load, an equation MathJax did not
typeset, or a Mermaid diagram that did not render. `--offline` blocks the network: a connected machine hides what the
offline copy lacks, as it hid MathJax's fonts. Firefox is rendered as well as Chromium because Linux users often open
the installed copy in it, and its `file://` rules are stricter. `--a11y` adds axe-core's serious and critical
violations, `--search` times queries from the first focus of the search box, `--shots <dir>` writes a full-page PNG
per load and prints its width, so that overflow shows up as a wider image, and the JSON report records every load.
`404.html` is left out unless `--pages` names it, and is then served from the site's root by a web server on the
loopback address. `--help` lists the options.

Frozen while the guide is rebuilt, with the rest of the audit apparatus (see the note at the top); the PowerShell
scripts take Windows PowerShell:

| Command | Checks |
| --- | --- |
| `powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\validate_user_guide_media.ps1 -RunFixtures` | The media contract: manifest rows, slots and links, tracker media states and capture-phase rows, provenance hashes and the per-phase counts; the fixtures inject eight known failures and require each to be refused for its reason. |
| `powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\validate_ug00_user_guide_foundation.ps1` | The tracker's page rows and phase ledger, the templates' rubric and their pointer to the tracker, that no template or page carries authoring evidence, the prerequisite inventory, and that these files and this document carry no trailing whitespace. |
| `powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\validate_ug01_user_guide_audits.ps1` | The media validator with its fixtures, the audit against its structural baseline, and the audit's parity and source-drift fixtures. |
| `powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\audit_user_guide.ps1 -OutputPath <report.md>` | Writes the audit report: registry and menu parity, required headings, source-review records and drift, boilerplate and parameter tables. |
| `node build\scripts\validate_ug65_user_guide_diagrams.cjs` | The rendering of retained diagrams in the built site; the script's header gives its setup, which keeps its Node packages outside the repository. |
