---
kind: interface
summary: The window an image opens in, and what each part of it is for.
menu: View > Image Viewer
---
# Image Viewer

The viewer shows one image and the tools that act on what is shown.

```figure
image: ../assets/window.png
alt: The viewer, with its tools on the left and the image on the right.
caption: The parts of the viewer.
pins:
  - {n: 1, x: 15, y: 40}
  - {n: 2, x: 66, y: 74}
```

## What you see

1. **The tools.** What acts on the view.
2. **The image.** Zoom with ++ctrl+plus++ and ++ctrl+minus++.

## What you can do

- Zoom to fit with ++ctrl+0++.
- Open a second view of the same image.

## When it goes wrong

The image is black
:   **Cause.** A linear image is shown without a display stretch.
:   **Fix.** Turn the display stretch on.

## Related

- [Levels](../processes/stretching/levels.md)
