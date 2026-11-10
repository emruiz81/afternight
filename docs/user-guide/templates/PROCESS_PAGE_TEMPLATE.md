---
# A process page: one process of the Process menu, read with its window open.
# Copy this file to docs/en/processes/<group>/<page>.md, list it under `nav:` in mkdocs.yml, replace every <...>, and
# delete every comment. docs/dev/USER_GUIDE_STYLE.md says how to write it; `check_user_guide.py --page <page>` holds
# it. The keys are explained in docs/dev/USER_GUIDE_AUTHORING.md, "Page metadata".
kind: process
stage: <stage id>                 # calibrate, register, stack, color, noise, stretch, finish or any
menu: <Process > Submenu > Action>
# One sentence, at most 40 words (summary_words): what the process does and what it is for.
summary: >-
  <What the process does and what it is for.>
data_in: <state>                  # linear, nonlinear, either or cfa: what the process expects
data_out: <state>                 # what its result is recorded as
destructive: <true or false>      # true when Apply replaces the target
preview: <mode>                   # real-time, static or none
sequences: <true or false>
masks: <mode>                     # post-blend, modulates or none
external: <none, or a short phrase naming the tool, model, catalog or service it needs>
glance:
  enabled: <When the menu action is enabled.>
  inputs: <What it takes.>
  outputs: <What it leaves.>
  state: <The image it is meant for.>
derived: <true or false>          # true when another project's code, library, tool or data computes its result
# controls_ignore:                # controls of the window that the page leaves out on purpose
#   - {label: <label>, reason: <why>}     # with `object: <object name>` when the window has the label twice
rewritten: true
---
# <Title, as the window shows it>

<!-- The opening: at most 120 words (opening_words), no heading. What the process does and what Apply produces. -->

<What the process does. What Apply produces.>

<!-- The window, at its default size with the real-time preview on. A pin's number is a step's number. -->

```figure
image: ../../assets/images/processes/<group>/<page>.webp
alt: <The window, described for a reader who cannot see it.>
caption: <One sentence that says what the picture shows.>
pins:
  - {n: 1, x: 50, y: 50}
```

<!-- The theme draws "Where it belongs" and "At a glance" here, from the front matter. The whole page: at most 1,800
     words (words), not counting the blocks, the math and the credits. -->

## Before you start

<!-- At most 6 bullets (before_you_start): what the image must be, what to do first, what is lost on Apply. -->

- <What the image must be.>
- <What to do first.>

## Steps

<!-- An ordered list, at most 9 steps (steps). Each step is a bold lead and a sentence or two, and the last one ends
     in something the reader can see. -->

1. **<Open the window.>** <Where it is and what it needs.>
2. **<Do the main thing.>** <What to move, and what to watch.>
3. **<Apply.>** <What the reader sees when it is done.>

## Controls

<!-- One optional sentence, then one `controls` block per window or tab, every control in the window's order. The
     defaults and ranges live here and are not repeated in the prose. A page with several windows may give each
     block a `### <window or tab>` heading. Start from `draft_guide_controls.py <page>`: the names, kinds, defaults,
     numbers and choices are the window's inventory (data/ui/), which the check holds the block to. -->

```controls
window: <Window title>
controls:
  - name: <Label, as the window shows it>
    kind: spinbox
    default: "0.50"
    widget: {min: 0, max: 1, step: 0.01, decimals: 2}
    effect: <What a higher and a lower value do.>
    range: <A constraint the numbers do not show.>
    enabled: <When it is enabled.>
    tradeoff: <What it costs.>
    scope: <Preview or Apply, channels, mask.>
    persistence: <How long the value is kept.>
  - name: <Label>
    kind: combo
    default: <Choice>
    options: [<Choice>, <Another choice>]
    effect: <What it chooses.>
    note: <Anything else the reader needs.>
```

## How it works

<!-- At most 5 numbered statements (how_it_works), then, as the page needs them and in this order: `### Model`,
     `### Boundaries` and `### Worked example`. -->

1. <What the process does first.>
2. <What it does next.>

### Model

<!-- The equations, numbered with \tag{}, every symbol defined in the sentence before. -->

<The symbols, then the equation.>

### Boundaries

<!-- At most 5 bullets (boundaries): what the process does at its edges, with invalid samples, when cancelled. -->

- <A boundary.>

### Worked example

<!-- The setup, then the result. Where the result is the point - the groups under `before_after_groups` in
     data/limits.yml - a before-and-after pair: two crops of one region at 100 %, under the display stretch the
     caption names. -->

<The image and the settings.>

```figure
images:
  - image: ../../assets/images/processes/<group>/<page>-before.webp
    alt: <The region before.>
    label: Before
  - image: ../../assets/images/processes/<group>/<page>-after.webp
    alt: <The region after.>
    label: After
caption: <What changed, and the display stretch both crops are shown under.>
```

## Starting points

<!-- Optional. One table of at most 4 rows (starting_points_rows) and at most 3 columns (starting_points_columns),
     headed as below. -->

| Situation | Start | Then adjust |
| --- | --- | --- |
| <A kind of image> | <A setting> | <What to move next> |

## When it goes wrong

<!-- At most 4 warning and danger callouts on the page (warnings), then a definition list of at most 8 entries
     (troubleshooting). The term is the symptom in the reader's words, or the exact message. -->

!!! warning "<A mistake that costs work>"
    <What to do instead.>

<The symptom, or "the exact message">
:   **Cause.** <One sentence.>
:   **Fix.** <An instruction.>

## Related

<!-- At most 6 links (related), one line each, by the pages' titles. -->

- [<Page title>](<page>.md), <why the reader would go there>.

## Credits

<!-- Required when `derived: true`, left out when there is nothing to credit. At most 8 entries (credits): the
     upstream work, its authors, its licence and a link; the papers the explanation rests on. -->

- [<Upstream work>](<https://address>), by <its authors>, under <its licence>.
