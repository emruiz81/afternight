---
kind: workflow
summary: From calibrated frames to a finished picture, stage by stage.
stages: [calibrate, register, stack, color, noise, stretch, finish]
needs: [a plate solver, a star catalog]
---
# Deep Sky

A workflow shares the tutorial's layout; its steps have no figure, so they run the full width.

## What you need

- [ ] A stacked master
- [ ] A solver and a catalog

## Your data

- **Broadband.** Calibrate the color against a catalog.
- **Narrowband.** Map the filters to a palette instead.

## Steps

### Calibrate and stack

Follow [First Stack](../start/first-stack.md).

### Stretch

Use [Levels](../processes/stretching/levels.md).

!!! see "You should see"
    The object against a dark, even background.

## The result

A finished picture and the history that made it.

## Next

- [The catalog](../processes/index.md), for every other process.
