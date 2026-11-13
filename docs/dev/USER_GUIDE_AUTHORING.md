# User Guide Authoring

> **Audience:** contributors who write, review, build or publish the end-user guide in `docs/user-guide/`.
>
> **Source of truth:** for the site, the configuration, templates, data and scripts this document names; for what
> a page says, the product's current source and tests.

The user guide is a MkDocs site in a theme of its own, written in Markdown. It is built from a locked toolchain,
works when opened from disk, ships inside the installers and is published to GitHub Pages with each release. This document states the
rules a change to the guide follows: where its parts live, how it is built and shipped, what a page's statements
are read from, what a complete page contains and how its media is handled.
[USER_GUIDE_STYLE.md](USER_GUIDE_STYLE.md) says how a page is written.

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
| `docs/user-guide/requirements-capture.in`, `requirements-capture.txt` | Pillow, pinned and locked for the script that prepares a captured picture ([Media](#media)). Installed by hand by whoever captures, never in continuous integration. |
| `docs/user-guide/SCREENSHOTS.md` | How a picture of a window is captured and prepared ([Media](#media)). |
| `docs/user-guide/docs/en/` | The English pages; `mkdocs.yml` names this directory `docs_dir`. |
| `docs/user-guide/data/` | The guide's data: `stages.yml`, the stages of the pipeline that pages name ([Page metadata](#page-metadata)); `limits.yml`, the budgets of a page, and `vocabulary.yml`, the words a page does not use ([What a complete page contains](#what-a-complete-page-contains)); and `ui/`, the inventory of the controls every process window opens with, which a test of the application writes ([The windows' controls](#the-windows-controls)). |
| `docs/user-guide/hooks/` | The theme's MkDocs hooks - `guide_hooks.py` (the events, and the only file `mkdocs.yml` names), `guide_fences.py` (the `controls` and `figure` blocks), `guide_html.py` (the passes over each page's HTML) and `guide_search.py` (the search index) - and `guide_model.py`, the schema of the pages' front matter and the readers of the guide's data and navigation, which the hooks and `check_user_guide.py` share; their tests beside them. |
| `docs/user-guide/hooks/fixtures/components/` | A small made-up guide that shows every page kind and component of the theme. The hooks' tests build it strictly, and it is the site to look at while changing the theme ([Components and blocks](#components-and-blocks)). |
| `docs/user-guide/theme/` | The theme: `main.html`, `base.html`, `404.html`, a layout per page kind in `layouts/`, the shared parts in `partials/`, and under `assets/guide/` its CSS (`tokens.css` a copy of the website's), JavaScript, fonts and the vendored lunr. |
| `docs/user-guide/docs/en/assets/images/` | Committed screenshots and example images. |
| `docs/user-guide/docs/en/assets/javascripts/` | The vendored MathJax runtime with its fonts and configuration, and the Mermaid runtime with its initializer. |
| `docs/user-guide/docs/en/assets/MEDIA_PROVENANCE.md` | The provenance of every committed image and retained diagram, and the offline runtime policy. |
| `docs/user-guide/templates/` | The four page templates ([Page kinds and templates](#page-kinds-and-templates)). |
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
| `destructive`, `preview`, `sequences`, `masks`, `external`, `glance` | Process | What the page's "At a glance" shows: whether Apply replaces the target (`true` or `false`), the preview (`real-time`, `static` or `none`), whether sequences are processed, what a mask does (`post-blend`, `modulates` or `none`), what the process needs from outside (`none` or a short phrase), and four texts - `enabled`, `inputs`, `outputs` and `state`. A process page states them all, and `derived` (rule R12). |
| `controls_ignore` | Process | The window's controls the page leaves out on purpose, each as `{label, reason}`, with the `object` of the one control meant when the window has several of that label ([The windows' controls](#the-windows-controls)). |
| `hero`, `notice`, `paths`, `popular` | Home | What the home page is drawn from ([Pages drawn from data](#pages-drawn-from-data)). |
| `reach` | Catalog | The ways to reach a process, a list of sentences under the catalog's summary. |

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
`docs/user-guide/theme/`, the hooks under `hooks/` and the data under `data/`.

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
  page's stage, the stages a tutorial or workflow spans, and leads the catalog and the home page. Any time is one
  of the eight, so that a page of a tool no stage owns shows where it stands too.
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
  `field`, `action` or `readout`. A spin box or slider needs `widget.min` and `widget.max` (and may state `step` and
  `decimals`), with a numeric default inside them; one whose range follows the image - a crop cannot be wider than
  the picture - states the end it has, or neither, and says what bounds it in `range`. A combo or radio needs
  `options` that hold its default. Each control is drawn as the widget it is, at its default, with its facets beside
  it, and gets the anchor `ctl-<name>`, which the page's table of contents lists. The facets take `code`,
  `**bold**` and key caps written as in prose (`++ctrl+shift+k++`), and no other Markdown; a message of the
  application that holds asterisks is quoted as a code span. A parameter table of ten columns cannot be read on a page; a row per control can,
  and it holds the numbers rule R14 compares with the window.
- **A `readout` is what the window shows and takes no input:** a count beside a control, a status line, a plot. It
  sits in the block where the window shows it, with what it opens with as its `default`, so that a reader finds
  the number beside the control it belongs to. It is no control: the inventory does not have it and rule R14 does
  not look for it. Without it a window's clipping counts and histograms had no place but the prose, where nobody
  looking for "Clipped" finds them. A readout is named by the caption the window gives it, or by its first words
  when it has none. Two lines the window shows together are one readout named for both when the page says the same
  of each, and two when it does not; two readouts of one name state an `object` each.
- **A control is called by its label,** the name the window gives it. `object` names it in the inventory of the
  window's controls when the label cannot: the window has several controls of that label, or gives the control no
  caption at all ([The windows' controls](#the-windows-controls)). It draws nothing. A name is listed once in a
  block, unless `object` tells the window's several controls of one label apart.
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
- **"At a glance"** is drawn from the page's front matter: its menu paths, the image it takes and leaves, what
  becomes of the target, its preview, sequences, masks and tools, then `glance`'s `enabled`, `inputs`, `outputs`
  and `state`. The head's chips show the same facts in short: "Replaces the target" when `destructive` is true.
- **The catalog** has no body: its page is its front matter and its H1. It lists every process page in the lane of
  its `stage`, the lanes in pipeline order with Any time last; inside a lane, by nav group - the Process menu's
  category - in nav order. A row is the nav label, the page's `summary`, its `menu` paths and `data_in` and
  `data_out`. A stage per category would teach the wrong order, so the category is the tag and the stage is the
  lane. Its `reach` sentences follow its summary. A link to a lane is `processes/index.md#stage-<id>`.
- **The home page** has no body either, and is drawn from its front matter:

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
  `extra.guide_runtimes`. Do not add a CDN script, a remote font or a network fallback: rule R1 of
  `check_user_guide.py` refuses a reference that leaves the site, and the render tool a request that does.
- **A page loads a runtime only when it uses it.** MathJax and Mermaid are 1.3 MB to download and 4.5 MB to compile
  between them, Mermaid three quarters of it; few pages have a diagram and many have no equation, and loaded with
  every page the two were most of what a first visit downloaded. `extra.guide_runtimes` of `mkdocs.yml` names each
  runtime by the class of the elements it draws (`arithmatex`, `an-mermaid`) with its scripts in the order they load,
  the hooks note which of those classes a page's content has, and `base.html` writes the scripts of those runtimes
  after the theme's own. A page drawn from data - the home page, the catalog - shows no body and loads none. A
  runtime added later is one more entry there, never an `extra_javascript` line: `extra_javascript` stays empty,
  which a test of the hooks holds, and a runtime that names a script the docs do not have fails the strict build.
- **The logo is small.** Every page loads it and the header draws it 30 px wide, so it is the 128 px icon; the strict
  build refuses a logo wider than that.
- Nothing the site does needs a server. A page opened from disk can neither `fetch()` a file nor start a worker nor
  load a module script, so data reaches a page as a plain script - the search index is one - and the theme's scripts
  are plain scripts. `localStorage` is read and written inside `try`: a browser may keep none for a local file, and
  the theme then follows the system's color scheme.
- `use_directory_urls: false` makes every link name its `.html` file, since a folder opened from disk has no index.
  The page-relative paths of the `figure` blocks depend on it too.
- `404.html` is the one page that does not work from disk: MkDocs writes its references from the site's root, for the
  web server that shows it at any address. R1 resolves them from the root, and the render tool serves the page over
  the loopback address when it is named.
- The pages write their equations as LaTeX, which is easier to write and to review than MathML by hand, and
  MathJax typesets them. Its fonts and font data are vendored with it, under `assets/javascripts/mathjax/fonts/`, and
  `assets/javascripts/mathjax/mathjax-config.js`, listed before MathJax among its runtime's scripts, points it at
  them. MathJax otherwise fetches
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
  From a process window it opens that process's page ([Help from a process window](#help-from-a-process-window)).
- The guide is English only (`docs_dir: docs/en`). Adding a language moves `docs_dir` to `docs/` with one directory
  per language and pins the `mkdocs-static-i18n` plugin in `requirements.in`; an untranslated page falls back to
  English only once that migration is in place.

### Help from a process window

F1 in a process window opens the guide at that process's page, from the bundled site first and from the published
guide otherwise, as the home page opens.

- **The table.** `src/AfterNightProcesses/app/ProcessRegistryBuiltins.cpp` holds the help table, `kHelpPages`: a row
  per registered action, from its object name to the page as the built guide names it
  (`processes/stretching/histogram-transformation.html`). The channel extractions and the format and sequence
  conversions open no window and have no row: they share `kNonInteractiveHelpPage`. A descriptor carries its page as
  `helpPath`; an extension's process has none, and Help opens the home page from its window.
- **Why a table in the application.** The published guide and the bundled one name a page by the same path, because
  `use_directory_urls: false` holds for both, so one relative path serves both. The application cannot ask the guide
  which page describes a window, since the guide may not be installed, so it states the path itself, and the check
  holds the statement to the guide.
- **The window.** The launcher states the descriptor's page on the window a launch shows
  (`TopLevelProcessBaseControl::setHelpPath`). An entry point makes and shows its own window and hands nothing back,
  so the window is the process window shown after the launch that was not shown before it.
  `DialogOrchestrationCoordinator::openHelp` walks up from the widget with the keyboard focus to the nearest process
  window that states a page (`an::ui::helpPathFor`). A path that is not a page inside the guide - one that climbs out
  of it, an absolute one, an address - is not followed, and the home page opens (`an::ui::userGuidePage`).
- **One F1.** A process window is a window of its own, and a shortcut of the main window's context is not heard
  there, so **Help > User Guide** has the application's shortcut context. Qt runs neither command when two shortcuts
  on one key are both active, so no window binds F1 to a command of its own: Image Integration's list of keys is on
  Shift+F1, and `Test_ProcessesShared` opens every process window and refuses an action or a shortcut on F1.
- **Moving or renaming a page** changes its row in the same change. Rule R15 of `check_user_guide.py` reads the
  table from the source: every path is a page the guide has, every row is of a registered action, every registered
  action has a row or is a non-interactive operation, and the page a row names is one whose `menu` reaches the
  action. `Test_AfterNightApp` holds the same of the descriptors the application registers, and
  `Test_ProcessesShared` that every window a launch shows carries its descriptor's page.

## Sources of truth

A page states what the product does, and each statement is read from the product, in this order:

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

**A page explains; it does not prove.** What shows a sentence to be true - source files and tests, review dates
and revisions, the data a run used, hashes - belongs to the repository and to the pull request, not to the page. A
record of that kind is pinned to the revision it was read at and is stale with the next commit, and a reader
processing an image has no use for it. So a page carries no evidence section, no evidence column in a table and no
comment for its authors, and a rule of the product is stated as the rule, without the file it was read in. The
pull request that writes or rewrites a page lists, for each page, the sources read and the windows observed: that
list is the record the page does not carry.

What a page owes to others is the exception, and it stays on the page: the credits of
[Naming other programs](#naming-other-programs).

### The windows' controls

What a process window holds is not read from its UI builder by eye. `docs/user-guide/data/ui/` has one file per
process window, named by the id the process is registered under (`tone_brightness.actionGamma_Correction.json`),
and each file lists the controls the window opens with, in the order a person meets them:

```json
{"label": "Gamma", "kind": "spinbox", "object": "gammaValue", "group": "", "visible": true, "default": "1.00",
 "min": 0.01, "max": 9.9, "step": 0.03, "decimals": 2, "slider": true}
```

- **A test of the application writes the files, and fails when a window no longer matches its file.**
  `ProcessUiInventoryTest` in `Test_ProcessesShared` opens every process window the registry registers, under the
  C locale, walks its widgets in layout order with tabs in tab order, and compares what it finds with the record. A
  window that gains, loses or changes a control fails the suite, naming the window and the first difference, until
  the record is written again - so the page and the picture of that window are looked at in the same change. To
  write the records, run the suite with `AFTERNIGHT_WRITE_UI_INVENTORY` naming the folder:

  ```cmd
  set AFTERNIGHT_WRITE_UI_INVENTORY=<repository>\docs\user-guide\data\ui
  bin\msvc\release\Test_ProcessesShared.exe --gtest_filter=ProcessUiInventoryTest.*
  ```

  The files are generated and never edited by hand. They record no geometry and no font, so Windows and Linux
  write the same files.
- **Why a test and not a reader of the sources.** Ten UI builders declare their labels without `translate()`, Image
  Integration sets labels in its `.cpp` files, and no reader of sources sees a default, a range or a combo's choices
  the way the window ends up showing them. The window is the fact; the test reads the window.
- **A record** states the control's `label`, its `kind` (the kinds of a `controls` block), its `object` name, the
  `group` it sits under (the titles of its tab, card, section and group box, outermost first, joined by ` > `),
  whether it is `visible` when the window opens, and its `default`: the text it shows. A spin box adds `min`, `max`,
  `step`, `decimals` and its `suffix`; a combo or a set of radio buttons adds `options`. `"slider": true` says a
  slider moves the spin box: the two are one control. A check box, a checkable button and a checkable group open
  `On` or `Off`.
- **A label is what a person would call the control by:** the text it shows itself; else the caption the window
  puts beside it - its buddy label, its form row's label, the nearest label to its left in its grid row or before it
  in its row, the check box that switches it on, or the short label right above it; else the title of its group,
  when it is the group's one control without a caption; else the first line of its tooltip; else its object name.
  A button without a text is an icon, and its tooltip names it before a caption does. A window whose control has
  none of these fails the test: give the control a tooltip.
- **`varies`** names what the window does not fix, and the record then leaves that value out. The test opens every
  window twice, on two images of different names, sizes and samples, and what the two openings disagree on follows
  the image: the size a geometry tool starts from, the open images a combo lists. The number of threads a window
  offers follows the machine and is recorded the same way.
- **`shared`** marks what every window has, which a process page leaves out: `rt-preview` is the histogram toolbar
  of a real-time preview, which the Process windows page describes once; `process-window` is a window's own Apply,
  Reset and Close. The image view inside a window - the preview and its toolbar - is the image viewer and is not
  recorded: it is the same in every window, and how much of its toolbar shows depends on the window's width.
- **What the inventory does not have:** a dialog one of the window's buttons opens, a window that is no registered
  process, such as Plate Solve, and a part the window draws itself - a histogram with its handles, a curve with
  its points. A page describes those in `controls` blocks named for them ("The histograms", "Sequence progress"),
  which rule R14 does not hold. What a window only shows is a `readout` in the window's own block
  ([Components and blocks](#components-and-blocks)).

**Rule R14 holds a process page to its windows** ([Checks](#checks)). The page's windows are the ones
its `menu` reaches that no other page names more exactly, so the Non-Interactive Processes page, which names whole
submenus, is not held to the windows that have pages of their own.

- Every control of the inventory is in a `controls` block, under its label and in the window's order; or the
  page's `controls_ignore` names it with a reason; or it is `shared`.
- Every control a block lists is in the window, and its `kind`, its `default`, the numbers of its `widget` and its
  `options` are the window's. A `readout` is not looked for. What `varies` names is the writer's to say, and so are the choices of a list the
  window opens empty, which it fills from what is installed or open.
- A block is of the page's window when it states no `window` or the window's title, and of one tab or group of it
  when `window` is that tab's or group's title. A page of several windows names the window of each block.
- Where the window has a label more than once - three boxes in a row under one caption - each entry states its
  `object`. A control the window gives no caption appears in the inventory under its object name: the writer names
  it, and `object` finds it.

**A new page's blocks start from the inventory.** The names, kinds, defaults, numbers and choices of a `controls`
block are copied from the window's file, in the window's order; the writer adds what each control does.

## Verifying in the application

Source and focused tests come first; the running application is for what they cannot show.

1. Read the controls of a process window - their labels, kinds, defaults, ranges and choices - from its inventory
   ([The windows' controls](#the-windows-controls)), and static dependencies, validation, persistence and algorithm
   behavior from source and focused tests, before launching AfterNight.
2. Write a short list of what source and tests leave open: visibility on a platform or build, dynamic enablement,
   state filled in at run time, layout, clipping and responsiveness, or an interaction the page must show.
3. Launch the application only when that list is not empty, or to take a picture. Work through the list in one
   session where feasible, with an isolated profile and output location (`AFTERNIGHT_TEST_STORAGE_ROOT`), and reach
   each state by the most deterministic route: a menu action, a shortcut, a known starting state or a focused test
   harness. Do not explore dialogs to enumerate controls or copy labels that source already gives.
4. After two failed automated attempts to reach the same state, stop retrying. Use a deterministic alternative
   route or a short manual inspection, and state nothing the inspection did not show.

Opening a dialog does not show persistence, algorithm execution, output quality or the readiness of an external
tool. A statement about an external tool's contract, a supported version, a standard or a licence is read from that
tool's or standard's own documentation, which the page then credits. The pull request says which windows were
observed, as it says which sources were read.

## Page kinds and templates

A page's `kind` ([Page metadata](#page-metadata)) picks its layout, its template and the rules it is held to:

| Kind | Template | Pages |
| --- | --- | --- |
| `process` | `docs/user-guide/templates/PROCESS_PAGE_TEMPLATE.md` | One process of the Process menu, under `processes/<group>/`. |
| `interface` | `docs/user-guide/templates/INTERFACE_PAGE_TEMPLATE.md` | A window, a panel or a part of the application that is not a process, under `interface/` and `extensions/`. |
| `tutorial`, `workflow` | `docs/user-guide/templates/TUTORIAL_PAGE_TEMPLATE.md` | A task done from start to finish: a tutorial under `getting-started/`, a workflow, which takes a kind of data through the pipeline, under `workflows/`. |
| `reference` | `docs/user-guide/templates/REFERENCE_PAGE_TEMPLATE.md` | What a reader looks up: the pages under `reference/`, Supported Formats, the media provenance, and the reference page a process page splits off. |
| `home`, `catalog` | None | Drawn from their front matter ([Pages drawn from data](#pages-drawn-from-data)); they have no body. |

A template is the page to start from: its front matter with every key, its sections in order, a placeholder where
the text goes, and a comment at each section saying what it holds and how much. Copy it, replace the placeholders
and delete the comments. The sections are fixed, and rule R7 of `check_user_guide.py` holds a page to them, for two
reasons. A reader who has used one process page finds the steps, the controls and the troubleshooting of the next
in the same place. And the theme draws its components by the words of those headings
([Components and blocks](#components-and-blocks)), so a renamed section is drawn as plain text.

| Kind | Sections, in order |
| --- | --- |
| Process | The opening and the window's figure, then `Before you start`, `Steps`, `Controls`, `How it works` (with `Model`, `Boundaries` and `Worked example` as the page needs them), `Starting points` (optional), `When it goes wrong`, `Related` and `Credits` (when there is something to credit). |
| Interface | The opening and a figure with numbered pins, then `What you see`, `What you can do`, `Controls` (optional), `When it goes wrong`, `Related` and `Credits` (optional). |
| Tutorial, workflow | The opening, then `What you need`, `Your camera` or `Your data` (optional), `Steps` (one `###` heading per step), `The result`, `Next` and `Credits` (optional). |
| Reference | The opening, then sections of its own. The glossary alone is fixed: `## <letter>` sections in alphabetical order, each holding `### <term>` entries. |

A subsection has a place too: a `###` heading names a step of a tutorial, a task of an interface page, a window or
tab under `Controls`, or one of the three parts of `How it works`. There is no fourth level. A page that needs more
structure than its template has is two pages: the page, and a reference page beside it.

**Every page is held to its template from the day it is added:** a new page is written on its template whole, in
the change that adds it, and no page can excuse itself from a rule.

## What a complete page contains

A page carries what its reader needs and nothing about how it was written ([Sources of truth](#sources-of-truth)).
[USER_GUIDE_STYLE.md](USER_GUIDE_STYLE.md) says how each part is worded.

### Every page

- **Front matter** that is complete for its kind, and a summary of one sentence.
- **An opening** without a heading that says what the page is about, before anything else.
- **Its template's sections,** in order, each holding what the template says.
- **No proof, no code names and no dating:** the vocabulary of `docs/user-guide/data/vocabulary.yml`.
- **Tables a reader can read:** five columns at most. What has more facets than that is a definition list, or the
  facets of a control.
- **Links that lead somewhere:** related pages by title, glossary terms on first use.

### A process page

1. **The opening** says what the process does and what Apply produces.
2. **The figure** shows the window; its pins carry the numbers of the steps.
3. **Before you start:** what the image must be, what to do first, and what Apply cannot give back.
4. **Steps:** an ordered list, each step a bold lead and a sentence or two, the last one ending in something the
   reader can see.
5. **Controls:** every control of the window in a `controls` block, in the window's order, with its kind, default,
   range and choices as the window has them and its effect as a sentence. The other facets - `range`, `enabled`,
   `tradeoff`, `scope`, `persistence`, `note` - hold what a reader looks up: when the control is enabled, what it
   costs, whether it acts on the preview or on Apply, how long its value is kept. A control the page leaves out is
   listed under `controls_ignore` with the reason. Rule R14 holds the block to the window's inventory
   ([The windows' controls](#the-windows-controls)).
6. **How it works:** a few numbered statements of what the process does, in order; then, as the page needs them,
   the model with its equations, the boundaries (edges, invalid samples, cancellation) and a worked example with
   its settings and its result. The settings are ones a reader can type, the result is worked out with the page's
   own equations for a value the reader can find - the background's median - and the opening picture and the
   before-and-after pair show those same settings.
7. **Starting points,** where there are some: a small table of situation, where to start and what to adjust next.
   A recommendation is given as one, and no result is stated that was not computed or observed.
8. **When it goes wrong:** the warnings a reader must not miss, then the symptoms with their cause and their fix.
9. **Related:** the pages a reader goes to next, each with its reason.
10. **Credits,** when the process is derived or the explanation rests on a paper or a standard
    ([Naming other programs](#naming-other-programs)).

A page that is rewritten still states every fact it stated before: in its prose, in a control's facets, or on its
reference page. A rewrite drops only what proves.

### An interface page

The opening, and a figure of the part with a numbered pin on each thing worth naming. `What you see` is the
numbered list of those things, each with what it is for. `What you can do` gives the tasks as instructions. Its
controls, where the list does not say it all, are in `controls` blocks. Then what goes wrong, and where to go next.

### A tutorial or a workflow

The opening says what the reader starts with and ends with. `What you need` is a checklist. `Your camera` or
`Your data` holds the one choice that changes the steps, as two cases. Each step is a `###` heading that is an
instruction, with what to do, a checkpoint - `!!! see "You should see"` - wherever the page can name one, and at
most one `??? wrong "If it goes wrong: <symptom>"`. `The result` says what the reader has now, and `Next` where to
go from here.

### Budgets

`docs/user-guide/data/limits.yml` holds how much a page may hold, and rule R8 holds a page to it:

| Kind | Words | Steps | Troubleshooting entries | Also |
| --- | --- | --- | --- | --- |
| Process | 1,800 (large 4,000) | 9 (large 12) | 8 (large 12) | The opening 120 words; 6 bullets before you start; 5 statements and 5 boundaries; 4 rows of starting points; 4 warnings; 6 related links; 8 credits. |
| Interface | 1,200 (large 3,000) | - | 6 | 6 related links. |
| Tutorial, workflow | 2,500 | 12 | One `wrong` box per step | - |
| Reference | - | - | - | A glossary entry 80 words. |

Every page: a summary of 40 words and tables of five columns. The file is the source of these numbers; the table is
here to be read.

- **Words** are counted in the page's Markdown body. Its front matter, its fenced blocks (controls, figures, code,
  diagrams), its math, and its credits do not count: what a reader looks up is not what makes a page long to read.
- **The budgets are upper bounds only.** A lower limit invites padding on a page that has little to say.
- **Budgets are in one file, not in front matter,** so that a page cannot grant itself an exception and the numbers
  change in one place.
- **Large pages** are named in `limits.yml`: the windows with workflow-scale controls (Image Integration, Image
  Registration, Image Calibration, Image Stacking), the solver and the catalog-backed color calibrations, the page
  that covers many operations (Non-Interactive Processes), the pages with an external tool or an expression
  language (Starnet++, Pixel Math), and the Settings dialog. A window with many controls and one job is not large:
  its controls sit in their block, which is not counted.
- **A page over its budget splits;** it does not grow headings. A process page splits off `<page>-reference.md`,
  a reference page beside it, for the facts its budget leaves out, and links it under `Related`.

## Writing style

[USER_GUIDE_STYLE.md](USER_GUIDE_STYLE.md) is the style guide: who reads which kind of page, voice and sentences,
what a page does not say, how the interface, numbers, pictures, equations, callouts and troubleshooting are
written, links, credits and length.

## Media

- **Format and size.** A picture is a WebP file at quality 90, or a PNG when that is smaller; a JPEG is accepted.
  Its longest side is at most 1,600 px and it weighs at most 400 KB (`images` in `limits.yml`; rule R11). A
  lossless PNG of a window with a sky image in it weighs from 400 KB to over 1 MB, and the same picture as WebP at
  quality 90 between 130 and 280 KB.
- **Names.** A process page's window is `assets/images/processes/<group>/<page>.webp`; another page's is
  `assets/images/<section>/<page>.webp`; a second picture of a page is `<page>-<what>.webp`.
- **The window.** The application runs in its dark theme at 100 % scale, in the `en_US` locale, with an isolated
  profile (`AFTERNIGHT_TEST_STORAGE_ROOT`). A process window is captured at its default size, with the real-time
  preview on, its title visible and the mouse outside it, cropped to the window's frame.
- **The window at work.** A process page's opening picture shows the window in the state of the page's worked
  example, not as it opens: a stretch at its defaults shows a black preview and a histogram that is one spike,
  which tells a reader nothing of what the window is for. The caption names the settings, and the capture script
  sets them (`-Set`).
- **The tools.** `build/scripts/tools/capture_guide_screenshots.ps1` takes the picture that way from a deployed
  build, and `build/scripts/tools/optimize_guide_image.py` makes the capture fit the limits and prints its
  provenance row; [docs/user-guide/SCREENSHOTS.md](../user-guide/SCREENSHOTS.md) is the recipe. The capture script
  is committed because the harness that took the README's pictures lived in a scratch folder and was lost. Pillow,
  which the second script needs, is pinned in `docs/user-guide/requirements-capture.txt`.
- **The data** is the maintainer's own, or data the project may publish: a linear color master for the color, noise,
  stretch and finishing pages; a stretched copy of it for the finishing pages; the frame sets the workflows name
  for calibration, registration and stacking; a synthetic frame for a tool that needs no sky.
- **Pins** are placed in percent of the picture's width and height while looking at it, and carry the numbers of
  the steps they point at.
- **Before and after.** For a process judged by its result (`before_after_groups` in `limits.yml`), the same region
  before and after Apply with the worked example's settings, under the same display mode, which the caption
  names. A process that changes the tone of the whole picture - a stretch, a curve - shows the whole frame; one
  whose effect is in the detail - noise, sharpening, stars - shows a crop at 100 %. The capture script takes both
  from the main window in one run (`-Apply`), so the two are the same place at the same zoom.
- **A process that works on a stretched image** is captured on the linear master after the stretch its worked
  example names, which the capture script applies first (`-Prepare`): the guide's data stays one file, and a
  reader can reproduce the picture from the page.
- **Provenance.** `docs/user-guide/docs/en/assets/MEDIA_PROVENANCE.md` has a row for every committed picture, by
  its path under the docs folder: its creator or source, its licence or permission, its date, its SHA-256, how to
  update it and the data it shows. A captured window's row is the one the optimizing script prints - the picture,
  what it shows, the data in it, the day and the application commit it was captured from, and its SHA-256 - under
  the heading of its pages' group in "Captured windows", so that two groups of pages never edit the same lines.
  The file records the vendored runtimes and fonts too, with their licences, and a retained inline diagram by the
  SHA-256 of its source with LF line endings: the complete Mermaid fence and its trailing newline. A picture is
  committed with its row, in the same change.
- **A page shows only a picture that exists,** in a `figure` block, with a caption and an alternative text written
  for the reader. A picture not yet taken states `owed: true` on its block, which keeps its caption, alternative
  text and pins for it: the page draws a frame that says the picture is to come and holds its description,
  `check_user_guide.py` counts it (`--owed` lists every one), and `--no-owed` refuses it, for a build that must
  show every picture. The block drops `owed` in the change that commits the picture. A process page of
  `before_after_groups` that shows no pair on purpose - DeepSNR's, a program whose licence limits what its
  outputs may be used for - is named in `before_after_exempt` of `limits.yml` with its reason, instead of owing
  a pair for ever.
- **Recapture with the window.** A change that alters a window recaptures its picture in the same pull request.
- **What may be committed.** Captures and processed examples made by the maintainer from data the project may
  publish. No third-party astrophotography images, catalog screenshots, captures of other applications or
  downloaded media unless redistribution in the repository and the installers is explicitly allowed; unknown
  permission keeps an image out. A capture never shows credentials, tokens, licence secrets, private repositories
  or endpoints, device serials or unrelated personal data.
- **Diagrams.** Prefer a Mermaid diagram for a pipeline, a data flow, an algorithm or a decision. The render tool
  fails a page whose diagram does not render ([Checks](#checks)).
- Media is committed as ordinary files. Git LFS is not used; adopting it is a separate change to `.gitattributes`,
  CI and contributor setup together.

## Naming other programs

A page names another astrophotography program only to credit derived work or to state compatibility with a file
format or a behavior the product reproduces; it never compares AfterNight with one. The sentence that names the
program says why it is named - "derived from", "ported from", "reads the files it writes".

**Credits stay on the page.** The page of a derived process - one that states `derived: true`
([Page metadata](#page-metadata)) - credits the upstream work, its authors and its licence, with a link, under
`## Credits`, as "Credit for derived code" in `AGENTS.md` requires; rule R6 of `check_user_guide.py` refuses such a
page without one. Any page credits there the papers, standards and upstream documentation its explanation rests
on. A credit links outside the repository: it names no AfterNight file or test, says nothing of which source is
authoritative, and carries no date on which a link was checked. A page with nothing to credit has no such section.

## Scope of a change

- Do not change application or engine behavior to make a page true. A discrepancy between source and documentation
  is recorded as a finding, and a product fix is its own change.
- Do not invent controls, defaults, algorithms, citations, screenshots, example results, validation output or reviewer
  approval.
- Use primary sources for external algorithms and standards, and keep verified implementation behavior apart from
  general recommendations.
- The pages a change sets out to write are its deliverables. Navigation, front matter, the glossary, the media
  provenance and reciprocal links are synchronized as far as the change requires, without pulling another page
  into a rewrite; a deficit outside the change is recorded as an issue.

## Definition of done

A page is done when:

1. **Front matter.** It is complete and valid for the page's kind, and the page sits in the right stage of the
   catalog with the right neighbours.
2. **The picture** comes first, the caption is true to it, and its pins match the steps.
3. **Structure.** The sections are exactly its template's; the page is within its budgets, says none of the
   forbidden vocabulary and has no table wider than five columns.
4. **Controls.** Every control of the window is in a `controls` block in the window's order, with its label, kind,
   default, range and choices as the window's inventory has them (rule R14). Defaults and ranges are not repeated
   in the prose.
5. **Facts kept.** Every fact the page stated before is still stated: in its prose, in a control's facets or on its
   reference page. A process judged by its result shows a before-and-after pair.
6. **Steps** end in something the reader can see, and a tutorial has a checkpoint wherever the page can name one.
7. **Equations** are typeset and numbered, with their symbols defined.
8. **Troubleshooting** entries are symptom, cause and fix.
9. **Links.** Related links resolve, glossary terms are linked on first use, and a derived page carries its
   credits.
10. **Truth.** Every statement was read in the product's source or seen in the window
    ([Sources of truth](#sources-of-truth)), and the pull request lists, for the page, the sources read and the
    windows observed. The page itself carries no record of how it was written.
11. **Checks.** `check_user_guide.py` reports no error and no warning on the page; the strict build passes; and
    the page was rendered from disk with the network blocked, in Chromium and Firefox, at 1440, 1024 and 390 px,
    in both themes ([Checks](#checks)).

A change that touches the guide without rewriting a page - a correction, a new control's row, a recaptured picture
- is done when what it changed is accurate, navigation, front matter, the glossary, related links and the media
provenance still agree, `git diff --check` passes, the diff holds nothing unrelated, and the checks below pass.

## Checks

Run from the repository root:

| Command | Checks |
| --- | --- |
| `build\scripts\msvc\build_user_guide_docs.bat`, `build/scripts/linux/build_user_guide_docs.sh` | The strict build from the lock. |
| `python build/scripts/tools/check_user_guide.py --site docs/user-guide/site`, with its test `python build/scripts/tools/check_user_guide_test.py` and the hooks' `python -m unittest discover -s docs/user-guide/hooks -p "*_test.py"`, all run with the toolchain's Python | Each page's front matter, nav entry, H1, menu paths, images and credits; its sections, budgets, vocabulary, figures, glossary links and controls; the application's help table; and what the built site references. `docs.yml` runs all three after the strict build. |
| `python build/scripts/tools/check_user_guide.py --page <page>` | One page, with its count of words against its budget: the command to run while writing it. |
| `bin\msvc\release\Test_ProcessesShared.exe --gtest_filter=ProcessUiInventoryTest.*`, with `AFTERNIGHT_TEST_STORAGE_ROOT` set | That every process window still opens with the controls `data/ui/` records ([The windows' controls](#the-windows-controls)). It runs with the application's suites, on both platforms. |
| `python build/scripts/tools/optimize_guide_image_test.py` | The tool that prepares a picture. It needs Pillow and skips without it. |
| `python build/scripts/tools/check_user_guide.py --tokens <the website's assets/css/tokens.css>` | That the theme's tokens are still the website's (R16), where the website's sources are at hand; not in CI. |
| `node build/scripts/render_user_guide.cjs --offline` | What a reader sees in the copy the installers bundle; the script's header gives its setup, which keeps its Node packages and browsers outside the repository. |

`check_user_guide.py` reads the guide through `guide_model.py`, which needs PyYAML, so it runs with the documentation
toolchain's Python: `docs\user-guide\.venv\Scripts\python` on Windows and `docs/user-guide/.venv/bin/python` on
Linux, the environment the build helper makes. It prints each finding as `path:line: Rnn message`, then a count per
rule and what it read, and exits 1 on any error; a warning prints as `path:line: Rnn warning: message` and fails
nothing. `--page <page>` holds only the pages named, and ends with each one's count of words against its budget,
which rule R8 otherwise states only once a page is over; the parts that need the whole guide - nav entries, menu
coverage, the help table and unused images - then do not run.

- R2 holds each page's front matter to its schema and the stage ids to `data/stages.yml`.
- R3 holds every page to the nav and to exactly one H1, and every nav entry to a page.
- R4 holds the `menu` paths to the application's menus, and every registered action, and every other action of the
  Process menu, to some page's `menu` ([Page metadata](#page-metadata)).
- R5 holds every image a page shows to a file spelled in its own case, and warns about an image under
  `assets/images` that no page, `README.md`, logo or favicon shows.
- R6 holds every page that states `derived: true` to a `## Credits` section that says something
  ([Naming other programs](#naming-other-programs)).
- R15 holds the application's help table to the guide: every page it names exists, every registered action opens
  one, and the page is one whose `menu` reaches the action ([Help from a process window](#help-from-a-process-window)).

R7 to R14 hold every page to its template, its budgets, the style guide and its windows:

- R7 holds the page's sections to its template's, in order, with no others, and the parts the theme draws to their
  shape: a process page's steps an ordered list with bold leads, its controls in a `controls` block, a tutorial's
  steps `###` headings, a troubleshooting entry a `**Cause.**` and a `**Fix.**`. The home page and the catalog
  have nothing below their H1.
- R8 holds the page to the budgets of `data/limits.yml`
  ([What a complete page contains](#what-a-complete-page-contains)).
- R9 refuses the words of `data/vocabulary.yml` in what the page says: its prose, the sentences of its front matter,
  the facets of its `controls` blocks and the captions of its figures. Fenced code, math and the addresses of links
  are not read. A code span is read only for the names of source - a file, a test, a scoped name - because a
  function in a code span may be what the reader types, and a source file's name is a citation however it is set.
  A label of the application that holds one of the words goes on the file's allow-list, with its page and the
  reason; an allowance its page does not use is a warning. An HTML comment is refused too: it is a note for the
  page's authors.
- R10 holds every picture to a `figure` block with a caption and an alternative text, a process or interface page
  to one opening figure before its first section, each pin to a step, and each item of an interface page's
  `What you see` to a pin. It warns when a process page of a group under `before_after_groups` shows no
  before-and-after pair in its worked example, unless `before_after_exempt` names the page with its reason, and
  when such an entry excuses nothing.
- R11 holds each picture a page shows to PNG, WebP or JPEG, to the size and weight of `limits.yml`, and to a row in
  `MEDIA_PROVENANCE.md`.
- R12 holds a process page to every key its "At a glance" is drawn from, and to saying whether it is
  `derived`.
- R13 warns when the first use on a page of a term the glossary defines - a `### <term>` of one of its letter
  sections - is not a link to the glossary. A term matches whole, with or without a plural ending; one written all
  in capitals matches only in capitals.
- R14 holds the `controls` blocks of a process page to the inventory of its windows
  ([The windows' controls](#the-windows-controls)): every control of a window listed, excused or shared; every
  control listed in the window under its label, with the window's kind, default, numbers and choices. Controls in
  another order than the window's are a warning, and so is an entry of `controls_ignore` that excuses nothing.

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
