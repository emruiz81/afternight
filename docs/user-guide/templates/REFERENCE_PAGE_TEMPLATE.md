---
# A reference page: something to look up, not to read through.
# Copy this file to docs/en/reference/<page>.md, list it under `nav:` in mkdocs.yml, replace every <...>, and delete
# every comment. docs/dev/USER_GUIDE_STYLE.md says how to write it; `check_user_guide.py --page <page>` holds it.
#
# The reference page of a process, `<page>-reference.md` beside the process page, is one of these: it holds the
# facts the process page's budgets leave out, under headings of its own.
kind: reference
# One sentence, at most 40 words (summary_words): what the reader looks up here.
summary: >-
  <What the reader looks up here.>
rewritten: true
---
# <Title>

<!-- The opening, without a heading: what the page lists and how it is ordered. -->

<What the page lists, and how it is ordered.>

<!-- The sections are the page's own: name each one after what a reader looks for. A reference page has no word
     budget. A table has at most 5 columns (table_columns); what needs more is a definition list. -->

## <What a reader looks for>

| <Thing> | <Fact> | <Fact> |
| --- | --- | --- |
| <A thing> | <Its fact> | <Its fact> |

## <Another thing a reader looks for>

<Term>
:   <Its definition.>

<!-- The glossary alone has a fixed shape: `## <letter>` sections in alphabetical order (`## 0-9` first), each
     holding `### <term>` entries of at most 80 words (entry_words). A rewritten page links a term to its entry on
     first use. -->
