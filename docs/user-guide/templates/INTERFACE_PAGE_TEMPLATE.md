---
# An interface page: a window, panel or part of the application that is not a process.
# Copy this file to docs/en/interface/<page>.md, list it under `nav:` in mkdocs.yml, replace every <...>, and delete
# every comment. docs/dev/USER_GUIDE_STYLE.md says how to write it; `check_user_guide.py --page <page>` holds it.
kind: interface
# One sentence, at most 40 words (summary_words): what the part is and what it is for.
summary: >-
  <What the part is and what it is for.>
# menu: <Menu > Action>          # optional: where the application opens it
---
# <Title, as the application shows it>

<!-- The opening, without a heading: what this part of the application is and when the reader meets it. The whole
     page: at most 1,200 words (words). -->

<What it is, and when the reader meets it.>

<!-- One figure, with a pin for every item of "What you see". -->

```figure
image: ../assets/images/interface/<page>.webp
alt: <The window, described for a reader who cannot see it.>
caption: <One sentence that says what the picture shows.>
pins:
  - {n: 1, x: 25, y: 50}
  - {n: 2, x: 75, y: 50}
```

## What you see

<!-- A numbered list; item 1 is pin 1. Each item is a bold lead and what the part is for. -->

1. **<The first part.>** <What it is for.>
2. **<The second part.>** <What it is for.>

## What you can do

<!-- Steps or a short task list. A page with several tasks may give each one a `### <task>` heading. -->

- <A task, as an instruction.>
- <Another task.>

## Controls

<!-- Optional: `controls` blocks for the part's own controls, with `### <window, tab or page>` headings where there
     are several. Leave the section out when the list above says it all. -->

```controls
- name: <Label, as the application shows it>
  kind: check
  default: "Off"
  effect: <What it does.>
```

## When it goes wrong

<!-- A definition list of at most 6 entries (troubleshooting): the symptom in the reader's words or the exact
     message, the cause in one sentence, the fix as an instruction. -->

<The symptom, or "the exact message">
:   **Cause.** <One sentence.>
:   **Fix.** <An instruction.>

## Related

<!-- At most 6 links (related), one line each, by the pages' titles. -->

- [<Page title>](<page>.md), <why the reader would go there>.

## Credits

<!-- Only when the page owes something: a standard, a paper, an upstream work, with its authors, licence and a
     link. Leave the section out otherwise. -->

- [<Work>](<https://address>), by <its authors>, under <its licence>.
