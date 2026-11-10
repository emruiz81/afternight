---
# A tutorial or a workflow: a task done from start to finish, read on a first run. A tutorial (`kind: tutorial`) is
# one sitting under docs/en/getting-started/; a workflow (`kind: workflow`) takes a kind of data through the pipeline
# under docs/en/workflows/ and always states its `stages`.
# Copy this file, list the page under `nav:` in mkdocs.yml, replace every <...>, and delete every comment.
# docs/dev/USER_GUIDE_STYLE.md says how to write it; `check_user_guide.py --page <page>` holds it.
kind: tutorial
# One sentence, at most 40 words (summary_words): what the reader will have done at the end.
summary: >-
  <What the reader will have done at the end.>
stages: [calibrate, register, stack]    # the stages it walks through, in pipeline order
# menu: <Process > Submenu > Action>    # optional: the window the tutorial works in
needs: []                               # what it needs from outside AfterNight; empty when nothing
---
# <Title>

<!-- The opening, without a heading: what the reader starts with and ends with, and how long it takes. The whole
     page: at most 2,500 words (words). -->

<What the reader starts with, and what they end with.>

## What you need

<!-- A task list, which the page draws as a checklist the reader ticks. -->

- [ ] <Something to have at hand>
- [ ] <Something else>

## Your camera

<!-- Optional, and titled `## Your camera` or `## Your data`: a list of two items, drawn as two cards, for the one
     choice that changes the steps. Leave the section out when there is none. -->

- **<One case.>** <What changes for it.>
- **<The other case.>** <What changes for it.>

## Steps

<!-- At most 12 steps (steps), one `###` heading each. A figure before the first step stays beside the steps; its
     pins carry the numbers of the steps they point at. -->

```figure
image: ../assets/images/<section>/<page>.webp
alt: <The window, described for a reader who cannot see it.>
caption: <One sentence that says what the picture shows.>
pins:
  - {n: 1, x: 25, y: 50}
  - {n: 2, x: 75, y: 50}
```

### <The first step, as an instruction>

<What to do.>

<!-- A checkpoint wherever the page can name one, and at most 1 `wrong` box in a step (wrong_per_step). -->

!!! see "You should see"
    <What the step leaves on the screen.>

??? wrong "If it goes wrong: <the symptom>"
    <What to do about it.>

### <The second step>

<What to do.>

!!! see "You should see"
    <What the step leaves on the screen.>

## The result

<What the reader has now, and where it is.>

## Next

<!-- Links, drawn as cards: where to go from here. -->

- [<Page title>](<page>.md) <says what it does next>.

## Credits

<!-- Only when the page owes something. Leave the section out otherwise. -->

- [<Work>](<https://address>), by <its authors>, under <its licence>.
