# Waiting Spinner Provenance

Source compared: 2026-11-04.

The busy indicator `WaitingSpinnerWidget` is AfterNight's copy of QtWaitingSpinner, a small Qt widget distributed
under the MIT licence, changed to fit the theme system.

## Upstream Source

- Project: QtWaitingSpinner, https://github.com/snowwlex/QtWaitingSpinner
- Files: `waitingspinnerwidget.h` and `waitingspinnerwidget.cpp`, as they have stood on the repository's `master`
  since 2015: the header last changed in `f47f5114e` (2015-02-03) and the implementation in `014d6ad4d`
  (2015-10-30); at `master` (`a1325a2ef1bac0ce37e9fe05d7be26554e33bba5`) their git blobs are
  `8129718a53d9af459dd3687fe931333202903d2e` and `0ef03a0bae0dd18d12ef0f8967fc117eab44a0f2`
- The revision the copy was taken from: not recorded; the notice it carries is the one those files carry
- The notice, as both files state it, kept byte for byte at the top of AfterNight's copies:
  - `Original Work Copyright (c) 2012-2014 Alexander Turkin`
  - `Modified 2014 by William Hallatt`
  - `Modified 2015 by Jacob Dawid`
  - followed by the MIT permission and warranty text. The repository's `LICENSE` file states the first line as
    `Original Work Copyright (c) 2012-2015 Alexander Turkin`; the files' own notice is the one kept.
- Licence: MIT, compatible with the product and with the GPL it is distributed beside. The notice has to accompany
  every copy, so the About dialog's licence page reproduces it.

## What AfterNight Changed

- The class is exported from `AfterNightControls`, and Qt's `moc` is not used: the slots are plain member functions
  and the timer is connected by function pointer.
- The colour is the theme's accent rather than black, and follows a palette or theme change.
- The line corners take the theme's small radius scaled by the roundness rather than a relative radius.
- The painter's state is restored by a guard, and the code is formatted to the repository's style.

What the widget draws and how it animates - the lines, their count, length, width and fade, the revolution rate and
the centring on a parent - is upstream's.

## Where the copied code lives

`src/AfterNightControls/widgets/waitingspinnerwidget.h` and `src/AfterNightControls/widgets/waitingspinnerwidget.cpp`,
each of which keeps the upstream notice and says, after it, where the code came from and what changed.
