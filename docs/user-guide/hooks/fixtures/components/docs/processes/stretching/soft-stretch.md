---
kind: process
stage: stretch
menu:
  - Process > Stretching > Soft Stretch
  - Image > Soft Stretch
summary: A page that states only the keys the build cannot draw a process page without.
data_in: linear
data_out: nonlinear
---
# Soft Stretch

A page that states its stage, its menus and its summary, and no `glance`. The build draws its position and no "At a
glance" card: it is the guide's check, not the build, that asks a process page for the rest.

## When it goes wrong

Nothing changes
:   **Cause.** Amount is 0.
:   **Fix.** Raise it.
