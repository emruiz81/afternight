---
kind: reference
summary: Everything Markdown and its extensions write, as the theme draws it.
---
# Markdown

The opening paragraph of a page, with **bold**, *emphasis*, `inline code`, a [link](../index.md), ==marked text==,
H~2~O, x^2^, {++an insertion++}, {--a deletion--} and a footnote[^1]. The HTML abbreviation is explained on hover.

*[HTML]: HyperText Markup Language

[^1]: The footnote's text.

## Callouts

!!! note "A note"
    Something worth knowing.

!!! tip "A tip"
    A faster way.

!!! warning "A warning"
    Something that can cost work.

!!! danger "A danger"
    Something that cannot be undone.

!!! see "You should see"
    What a step leaves on the screen.

??? wrong "If it goes wrong: the window is empty"
    What to do about it.

??? note "A folded note"
    Folded until opened.

## Lists

- A bullet
    - A nested bullet
- Another

A numbered list:

1. A numbered item
2. Another

A task list:

- [x] A done task
- [ ] An open task

Term
:   Its definition.

Another term
:   Its definition, in a plain definition list.

## Tables

| Format | Reads | Writes |
| --- | --- | --- |
| FITS | Yes | Yes |
| XISF | Yes | Yes |

| One | Two | Three | Four | Five | Six | Seven |
| --- | --- | --- | --- | --- | --- | --- |
| A table of six columns or more | is set smaller | and keeps | its first column | in view | while the rest | scrolls |

## Code

```python
def stretch(image, midpoint=0.25):
    """Bend the image around the midpoint."""
    return image ** midpoint  # 1 leaves it as it is
```

```bash
AfterNightCLI --help
```

=== "Windows"

    ```cmd
    AfterNightCLI.exe input.fits output.png
    ```

=== "Linux"

    ```bash
    ./AfterNightCLI input.fits output.png
    ```

Press ++ctrl+s++ to save.

> A quotation.

---

### A third-level heading

Text under it.
