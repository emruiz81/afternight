# UG-57 Validation Evidence

This record retains the exact validation scope for the Keyboard Shortcuts
reference at revision `da208eba2a8ba587be3b7fc6342d764bd86764db` and the
subsequent documentation-only review remediation.

## Source audit

The active inventory was derived from repository-wide searches for
`setShortcut()`, `QShortcut`, `QKeySequence`, key press/release handlers, event
filters, and `Qt::Key_*` comparisons under `src/`. A separate user-visible hint
search covered shortcut text in tooltips, onboarding content, help, and
confirmation messages. That second pass found three inactive hint families:

- Dashboard onboarding advertises unbound `Ctrl+N` for New Sequence.
- Image Integration local help advertises unbound `Ctrl+Z` and `Ctrl+Y`.
- Image Integration removal confirmations advertise unbound `Ctrl+Z`.

The 83-row matrix contains active commands only. These inactive hints are
recorded in the page's warnings and troubleshooting sections.

The source audit also distinguishes Qt sequence matching from direct key
handlers. The direct `ImageViewControl` Undo/Redo path and the broader modifier
matching in direct handlers are source-derived; the focused tests below do not
establish those delivery paths.

## Focused MSVC tests

The commands were run from each deployed `bin/msvc/<config>` directory with
`QT_QPA_PLATFORM=offscreen`.

### Dashboard adjacent UI cases

```powershell
Test_MainDashboard.exe --gtest_filter="MainDashboardInteraction.EnterOnFocusedPrimaryButtonEmitsOpenFileRequested:MainDashboardInteraction.ToolbarActionsWithShortcutsExposeTooltips" --gtest_brief=1
Test_MainDashboard_d.exe --gtest_filter="MainDashboardInteraction.EnterOnFocusedPrimaryButtonEmitsOpenFileRequested:MainDashboardInteraction.ToolbarActionsWithShortcutsExposeTooltips" --gtest_brief=1
```

- Release: 2/2 PASS.
- Debug: 2/2 PASS.
- Limit: despite its name, `EnterOnFocusedPrimaryButtonEmitsOpenFileRequested`
  calls `QPushButton::click()` because the offscreen platform does not reliably
  deliver the key. These cases establish adjacent action wiring and displayed
  hints, not Dashboard key delivery. `ActionCardEventFilter.h` is the source of
  truth for Return/Enter/Space/arrow dispatch.

### Image-view action-availability helper

```powershell
Test_ImageViewControl.exe --gtest_filter="AstrometryStarOverlayUtils.ShortcutActionRequiresVisibleAndEnabledState" --gtest_brief=1
Test_ImageViewControl_d.exe --gtest_filter="AstrometryStarOverlayUtils.ShortcutActionRequiresVisibleAndEnabledState" --gtest_brief=1
```

- Release: 1/1 PASS.
- Debug: 1/1 PASS.
- Limit: this case establishes only the visible/enabled action gate. It does not
  inject image-canvas key events or cover press/release and modifier dispatch;
  those claims are source-derived from `ImageViewControl.cpp`.

### Image Integration selected keyboard cases

```powershell
Test_ImageIntegration.exe --gtest_filter="KeyboardActionsTest.ShortcutsAreConfiguredOnMenuActions:KeyboardActionsTest.NavigateMenuRegistersCtrl1ThroughCtrl8:KeyboardActionsTest.RefreshShortcutIsRegisteredAsQShortcut:KeyboardNavigation.*:FrameSourceTab.CtrlFFocusesSearch:QualityDistributionWidgetTest.KeyboardNavigationEmitsFrameClicked:BlinkComparatorModesTest.KeyboardRating:BlinkComparatorResult.ExclusionsRemainReadableAfterEscapeClose:BlinkComparatorResult.ExclusionsAndRatingsRemainAttachedToCanonicalPathAfterSort" --gtest_brief=1
Test_ImageIntegration_d.exe --gtest_filter="KeyboardActionsTest.ShortcutsAreConfiguredOnMenuActions:KeyboardActionsTest.NavigateMenuRegistersCtrl1ThroughCtrl8:KeyboardActionsTest.RefreshShortcutIsRegisteredAsQShortcut:KeyboardNavigation.*:FrameSourceTab.CtrlFFocusesSearch:QualityDistributionWidgetTest.KeyboardNavigationEmitsFrameClicked:BlinkComparatorModesTest.KeyboardRating:BlinkComparatorResult.ExclusionsRemainReadableAfterEscapeClose:BlinkComparatorResult.ExclusionsAndRatingsRemainAttachedToCanonicalPathAfterSort" --gtest_brief=1
```

- Release: 12/12 PASS.
- Debug: 12/12 PASS.
- Scope: registered menu/tab/refresh shortcuts, frame-tree navigation, frame-tab
  search, quality-chart navigation, and selected Blink rating/exclusion/close
  result behavior. Registration/stacking table Return, issue badge/card, and
  modified-key behavior remain source-derived.

### Mask Drawing full executable

```powershell
Test_MaskDrawing.exe --gtest_brief=1
Test_MaskDrawing_d.exe --gtest_brief=1
```

- Release: 25/25 PASS.
- Debug: 25/25 PASS.
- Limit: the executable covers brush, drawing-document, process UI/registration,
  and shape behavior. It does not activate the process's eight `QShortcut`
  objects. Shortcut registration, context, and callbacks are source-derived from
  `MaskDrawingProcess.cpp`; Qt primary Undo/Redo mappings are standards-derived.

## Documentation and audit validation

```powershell
build\scripts\msvc\build_user_guide_docs.bat
powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\validate_ug00_user_guide_foundation.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\audit_user_guide.ps1 -OutputPath <temporary-report>
powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\validate_ug01_user_guide_audits.ps1
git diff --check
```

- Review-remediation strict MkDocs: PASS in 5.20 seconds.
- UG-00 foundation: PASS with 111 tracker rows/17 fields, 58 completed
  phase-ledger rows, four templates, and 32 prerequisite groups.
- Deterministic audit regeneration: PASS in 242.0 seconds; 111 navigated/tracked
  pages, zero parity findings, six pages awaiting later phases, 78 required-heading
  findings, 72 recorded-source drift pages, zero partial source records, and
  SHA-256 `369c4ee7a200fbd3170e7adda62d1dbb54056ad40ff36b2a40d2715800cdb9df`.
- Full UG-01 baseline/repeatability/parity/drift validator: PASS in 483.6
  seconds. Two earlier remediation attempts failed on stale case-sensitive
  declarative fragments; both assertions were corrected and the complete
  validator was rerun successfully with explicit exit-code propagation.
- Final whitespace and eight-file scope checks: PASS.

The UG-57 declarative validator checks the 83-row shape, required
active/inactive guidance, source/tracker synchronization, and this evidence
record. It does not independently extract or prove shortcut semantics from C++
source; the source audit above remains the authority for those claims.
