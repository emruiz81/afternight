# User Guide Style

> **Audience:** whoever writes or reviews a page of the end-user guide in `docs/user-guide/`.
>
> **Source of truth:** for how a page is written. [USER_GUIDE_AUTHORING.md](USER_GUIDE_AUTHORING.md) says how the
> guide is built, what a page's statements are read from and which checks hold it; the templates under
> `docs/user-guide/templates/` give each kind of page its sections; `docs/user-guide/data/limits.yml` and
> `docs/user-guide/data/vocabulary.yml` hold the numbers and the words this document names.

The guide is for a person processing an image tonight. A page says what a thing does and where it is, what to do
and what you should see, and what to do when you don't see it. What proves a sentence true - a source file, a
test, a data set, a hash, a review record - belongs in the repository and in the pull request, not on the page.

## Contents

1. [Who reads which page](#who-reads-which-page)
2. [Voice](#voice)
3. [Sentences](#sentences)
4. [What a page does not say](#what-a-page-does-not-say)
5. [The interface in words](#the-interface-in-words)
6. [Numbers](#numbers)
7. [Pictures](#pictures)
8. [Equations](#equations)
9. [Callouts](#callouts)
10. [Troubleshooting](#troubleshooting)
11. [Links and the glossary](#links-and-the-glossary)
12. [Credits and other programs](#credits-and-other-programs)
13. [Length](#length)
14. [What the check holds](#what-the-check-holds)

## Who reads which page

| Kind | Read | So the page |
| --- | --- | --- |
| Process | With the window open, between two tries of a setting | Opens with the window, puts the steps before the explanation, and lets each control be found by its label. |
| Interface | While finding one's way around a part of the application | Names what is on the screen in the order the eye meets it, then says what it is for. |
| Tutorial, workflow | On a first run, one step at a time | Gives one instruction per step and says what the step leaves on the screen. |
| Reference | To look one thing up | Is ordered for finding: by name, by letter, by format. It is not read through. |

The reader is an astrophotographer who knows what a flat frame and a histogram are and may be new to AfterNight.
Explain what AfterNight does with a thing; do not teach the thing from the beginning, and link the
[glossary](#links-and-the-glossary) for a term that needs it.

## Voice

- Address the reader as "you". There is no "we" and no "I".
- Describe behavior in the present tense: "Apply replaces the target", not "Apply will replace the target".
- Give instructions in the imperative: "Lower the midpoint until the faint signal shows."
- Define a term where the page first uses it, once, and give it an entry in the glossary.
- Write plainly, without idioms, slang or cultural references, so that a page translates and a reader whose first
  language is not English follows it.

## Sentences

- One idea per sentence, about 20 words. Two short sentences beat one with a semicolon in the middle; a chain of
  semicolons is a list that was not written as one.
- A parenthesis holds three words at most. What needs more is a sentence of its own, or does not belong.
- Start a step with what to do, and end it with what the reader then sees.
- Say a thing once. A fact that sits in a control's row is not repeated in the steps, and a warning is not given in
  three places.

## What a page does not say

`docs/user-guide/data/vocabulary.yml` lists the words below as patterns, and rule R9 refuses them on every
page. The file's allow-list takes a label of the application that holds one of them, with the page and the reason.

- **No proof.** Not "evidence", "verified", "validated", "authoritative", "source-derived", "focused tests", or a
  date on which something was checked. Where a page needs a reason, it gives the reason, not the citation:

    | Not | But |
    | --- | --- |
    | Source and focused tests establish that the preview is never clamped. | The preview is never clamped. |
    | Verify that the histogram has moved off the left edge. | Check that the histogram has moved off the left edge. |
    | Validation rejects a kernel wider than the image. | A kernel wider than the image is refused: "Kernel exceeds the image". |

- **No code.** Not the name of a function, a class, a source file or a test, in a code span or out of one. A
  reader cannot use it. Say what the application does. A code span is for what the reader types or reads on the
  screen: a file name, a Pixel Math expression, a command, a value.
- **The version is on the cover.** The guide ships with the application it describes, and its footer names the
  version. Delete "current", "currently", "the current application", "present behavior" and "as implemented": "the
  current build clamps the result" is "the result is clamped".
- **No hedging about the guide.** A page does not say that it is incomplete, provisional or under review, and does
  not say what it "does not promise". The home page carries the guide's one notice of what it still lacks:
  its pictures not yet taken.
- **No note for the authors.** A page has no HTML comment: a picture that is owed, a doubt and a task are issues.
  The comments of a template are deleted with its placeholders.

## The interface in words

- A control is called by its label, in bold, exactly as the window shows it: **Shadows**. Its type is named only
  where it matters: "the **Shadows** spin box" when the slider beside it has the same label.
- A menu path is bold, with `>` between its levels: **Process > Intensity Transformations > Histogram
  Transformation**.
- A key or a shortcut is written with the keys markup, which draws key caps: `++ctrl+shift+s++`.
- A message of the application is quoted exactly, in double quotes, so that a reader who searches for it finds it.
- What the reader types, and a file or folder name, is a code span.
- The title of a page is its window's title, or the name the application gives the part.

## Numbers

A control's default, range, step and choices live in its row of the `controls` block, where rule R14 compares
them with the window. What the window shows beside a control - a count, a status - is a `readout` row there too. The prose quotes a number only when the reader must type it: "Set **Midtones** to 0.25."
Prose that repeats a default goes stale when the default moves, and nothing notices.

## Pictures

- Every process page and every interface page opens with its window, as a `figure` block, before the first
  section. The picture is taken as "Media" in [USER_GUIDE_AUTHORING.md](USER_GUIDE_AUTHORING.md) says. A process
  window is shown at work, with the settings of the page's worked example, and the caption names them.
- A pin sits just left of the control it points at, or of its label when the two touch, never over a value.
- A tutorial step that points at a control has a pin on the picture, with the step's number. On an interface page
  every item of "What you see" has its pin.
- A caption says in one sentence what the picture shows. The alternative text describes the picture for a reader
  who cannot see it; it is not the caption again.
- A process that is judged by its result shows the result: a before-and-after pair in its worked example, the
  same region under the same display mode, which the caption names. A process that changes the tone of the whole
  picture shows the whole frame; one whose effect is in the detail shows a crop at 100 %.
- A change that alters a window recaptures the window's picture in the same pull request.
- Every picture is a `figure` block. A Markdown image has no caption.

## Equations

An equation helps the reader predict what a control does. Write it as LaTeX, `\( ... \)` in a sentence and
`\[ ... \]` on its own, number a displayed equation with `\tag{}`, and define every symbol in the sentence before
it. An equation the page never refers to, or one that only restates the code, is left out.

## Callouts

| Callout | For |
| --- | --- |
| `!!! note` | Something worth knowing that the steps do not need. |
| `!!! tip` | A faster or safer way. |
| `!!! warning` | A common mistake, or something that costs work. |
| `!!! danger` | Something that loses work or silently gives a wrong result. |
| `!!! see "You should see"` | The checkpoint of a tutorial step: what the step leaves on the screen. |
| `??? wrong "If it goes wrong: <symptom>"` | The one thing most likely to go wrong in a tutorial step, folded. |

A page full of callouts has none: a process page has at most four warnings and dangers. What every reader must do
is a step, not a callout.

## Troubleshooting

An entry of "When it goes wrong" has three parts:

```markdown
The preview is black
:   **Cause.** The black point is above most of the image.
:   **Fix.** Lower it to the foot of the histogram.
```

- The symptom is in the reader's words, or is the exact message in double quotes.
- The cause is one sentence.
- The fix is an instruction.

List what goes wrong for a reader who followed the steps, most likely first. An entry no reader will meet is
left out.

## Links and the glossary

- Link a related page by its title, with a relative link to its `.md` file, which the strict build checks.
- "Related" says in a few words why the reader would go there.
- A term the glossary defines is linked to its entry the first time a page uses it, and not again. A term inside a
  name the page repeats - the title of a linked page, a bold label, a quoted message - is not a use of it: the first
  plain use after it is the one to link. The glossary's entries are the terms as pages write them, and leave out
  process names and everyday words, so that a link never lands on a word used in another sense.
- A link's words say where it leads. "Click here" and a bare address do not.

## Credits and other programs

- A derived process - one whose result is computed by another project's code, library, tool or data - credits the
  upstream work, its authors and its licence, with a link, under `## Credits`. Any page credits there the papers
  and standards its explanation rests on. A page with nothing to credit has no such section.
- A page names another program only to credit derived work or to state compatibility with a file format or a
  behavior, and the sentence says which: "derived from", "reads the files it writes". A page never compares
  AfterNight with another program.

## Length

`docs/user-guide/data/limits.yml` holds the budgets: the words of a page and of its opening, its steps, bullets,
callouts and troubleshooting entries, the columns of a table. Each is an upper bound, and there is no lower one: a
floor invites padding, and a small process deserves a small page.

A page over its budget does not grow more headings. First cut what is said twice and what only proves. Then move
what a reader looks up, and does not read, into the facets of a control's row. What is still too much goes to a
reference page beside the page, `<page>-reference.md`, which the page links under "Related". Every fact the page
stated before stays stated in one of those places.

The pages `limits.yml` lists as large - the windows with workflow-scale controls, and the Settings dialog among
the interface pages - have budgets of their own.

## What the check holds

`build/scripts/tools/check_user_guide.py` holds every page to this document, as far as a program can:

| Rule | Holds |
| --- | --- |
| R7 | The sections of the page's template, in order, and the shape of the parts the theme draws. |
| R8 | The budgets of `limits.yml`. |
| R9 | The words of `vocabulary.yml`, and no HTML comment. |
| R10 | The opening figure, captions and alternative texts, pins that match steps, the before-and-after pair. |
| R11 | The format, size and weight of each picture, and its provenance row. |
| R12 | The front matter a process page states. |
| R13 | Glossary terms linked on first use. |

It cannot tell whether a sentence is true, whether a step can be followed or whether a caption fits its picture.
Read the page once as its reader would, with the window open.
