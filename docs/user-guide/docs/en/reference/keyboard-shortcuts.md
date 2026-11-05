# Keyboard Shortcuts

## Purpose, scope, audience, prerequisites, expected result, and authority

This is the authoritative reference for keyboard commands defined directly by
AfterNight. Use it to identify what a key does in the main window, a focused
image view, the Dashboard, Session Console, Image Integration, Blink Comparator,
or Mask Drawing, and to check the focus and state required before pressing it.

The inventory covers AfterNight's supported Windows MSVC and Linux builds. It
does not list ordinary Qt text-editing keys, generic table/tree
navigation, dialog default-button behavior, menu mnemonics such as `Alt+<letter>`,
desktop/window-manager bindings, or accessibility keys supplied entirely by the
operating system. Those behaviors are platform, locale, widget, and focus
dependent rather than AfterNight bindings.

Shortcuts are fixed in the current application: there is no shortcut editor,
settings key, import/export record, or per-project override.

## Reachability or applicability

- Main-window actions work while the AfterNight main window is active and their
  corresponding `QAction` is enabled. Open, Settings, and User Guide do not
  require an active image; Save, Undo, Redo, and zoom commands do.
- Dashboard quick actions are ordinary buttons with Qt's own button keys; there
  is no AfterNight card-navigation key. Dashboard `Ctrl+F` is a main-window
  shortcut that works while the Dashboard page and its recent-file search are
  visible, and focuses that search.
- Image-view keys handled below require focus in the image canvas itself. The
  standard, real-time-preview, and static-preview modes do not expose the same
  actions.
- Session Console actions belong to the window that holds its dock: the main
  window while it is docked, its own window only while it floats. `Return`
  belongs specifically to the open Search log dialog.
- Image Integration menu actions use window scope. Tree, table, chart, badge,
  and issue-card keys require focus in the named control.
- Mask Drawing uses widget-with-children scope, so its local keys remain active
  while a child control in that process window has focus.

The displayed chord is the canonical way to invoke each command. `QAction` and
`QShortcut` rows use Qt sequence matching. Several direct key handlers compare
only the key code, or only the Control/Shift bits: generic process close, image-view dispatch, frame-tree navigation, quality-chart
navigation, issue cards/badges, and Blink Comparator can therefore accept
additional modifiers. Their rows and the implementation notes below identify
that broader matching; it is current behavior, not a recommended alternate
binding.

The menu or toolbar is the quickest state check. A disabled action remains
disabled when invoked through its `QAction` shortcut. Direct control handlers
have the narrower prerequisites stated in their rows and do not all mirror a
menu action's state.

## Complete reference matrix

### Main window

| Item/exact label | Type/unit/category | Default/current value | Valid values/constraints | Meaning/effect | Dependencies/applicability | Trade-off/side effect | Scope | Persistence/metadata |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Open | Fixed action shortcut | `Ctrl+O` | Exact key string | Opens the normal image/sequence picker | Main window active | In Image Integration the same key adds files instead | Main window | Binding not configurable; opening updates normal recent-file state |
| Save as... | Fixed action shortcut | `Ctrl+Shift+S` | Exact key string | Opens Save As for the active image or sequence | Enabled only with a compatible active view | Saving can overwrite after confirmation; see Saving and Exporting | Main window/active view | Binding not configurable; normal save-path state applies |
| Undo | Fixed action shortcut plus direct view handler | `Ctrl+Z` | Main action matches the exact sequence; the direct `ImageViewControl` handler accepts Control+Z with additional modifiers | Restores the previous image-history state | Main action is enabled only when the active view's history has an undo step; the direct path runs if the key event reaches `ImageViewControl` and does not repeat that gate | Changes the image state; not Image Integration project undo | Main window action or receiving image-view control | Binding not configurable; image history changes |
| Redo | Fixed action shortcut plus direct view handler | `Ctrl+Y` | Main action matches the exact sequence; the direct `ImageViewControl` handler accepts Control+Y with additional modifiers | Reapplies the next image-history state | Main action is enabled only when the active view's history has a redo step; the direct path runs if the key event reaches `ImageViewControl` and does not repeat that gate | Mask Drawing also accepts `Ctrl+Shift+Z` for its own redo | Main window action or receiving image-view control | Binding not configurable; image history changes |
| Zoom In | Fixed action shortcut | `Ctrl++` | Exact key string; physical key can require `Shift` on some layouts | Increases active-view display zoom | Compatible active image view | Keyboard layout can make the plus key awkward; pixels are unchanged | Main window/active image view | Display state only; binding not configurable |
| Zoom Out | Fixed action shortcut | `Ctrl+-` | Exact key string | Decreases active-view display zoom | Compatible active image view | Pixels are unchanged | Main window/active image view | Display state only; binding not configurable |
| Zoom 100% | Fixed action shortcut | `Ctrl+0` | Exact key string | Sets one image pixel per display pixel | Compatible active image view | May exceed the visible viewport | Main window/active image view | Display state only; binding not configurable |
| Settings... | Fixed action shortcut | `Ctrl+,` | Exact key string | Opens application Settings | Main window active | Can conflict with desktop/application conventions | Main window | Binding not configurable; saved settings change only when edited |
| User Guide | Fixed action shortcut | `F1` | Exact key string | Opens bundled help, then the configured online fallback if allowed | Main window active | In Image Integration, `F1` opens local shortcut help instead | Main window and Dashboard action path | Binding not configurable |

### Dashboard

| Item/exact label | Type/unit/category | Default/current value | Valid values/constraints | Meaning/effect | Dependencies/applicability | Trade-off/side effect | Scope | Persistence/metadata |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Focus recent-file search | Fixed `QShortcut` | `Ctrl+F` | Exact key string | Moves focus to the Dashboard search field and selects its text | Dashboard page shown in the main window and its search field visible (at least one recent file) | Same key searches Session Console or Image Integration where those own the key | Main window, while the Dashboard page is current | Search text follows Dashboard persistence; binding does not |

### Image viewer and preview canvases

For this direct event-filter group, Control and Shift are the only modifier bits
used to select a branch. Extra Alt or Meta modifiers are not rejected. `F11`
requires Control and Shift to be absent, but likewise does not reject Alt/Meta.

| Item/exact label | Type/unit/category | Default/current value | Valid values/constraints | Meaning/effect | Dependencies/applicability | Trade-off/side effect | Scope | Persistence/metadata |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Maximize image view / restore | Direct canvas handler | `F11` | Control and Shift absent; extra Alt/Meta are not rejected | Maximizes or restores the docked image view | Focused standard canvas, dock support, and a maximize action; no effect for preview modes | The direct handler can restore even when the visible action is disabled in an already-maximized layout | Focused standard image canvas | Layout state only; not a saved binding |
| Fit To Window | Direct canvas handler | `Ctrl+Shift+F` | Control and Shift required; extra Alt/Meta are not rejected | Fits the displayed image to the viewport | Focused standard, RT-preview, or static-preview canvas with display content | Display zoom changes; pixels do not | Focused image canvas | Display state only |
| Histogram | Direct canvas handler with action gate | `Ctrl+H` | Control required and Shift absent; extra Alt/Meta are not rejected | Opens Histogram | Focused standard canvas and Histogram action visible/enabled | A second dialog can be opened; analysis can cost time on large data | Focused standard image canvas | No image change |
| Statistics | Direct canvas handler with action gate | `Ctrl+Shift+T` | Control and Shift required; extra Alt/Meta are not rejected | Opens Statistics | Focused standard canvas and Statistics action visible/enabled | Analysis can cost time on large data | Focused standard image canvas | No image change |
| EXIF Viewer | Direct canvas handler | `Ctrl+Shift+X` | Control and Shift required; extra Alt/Meta are not rejected | Opens EXIF metadata viewer | Any focused image canvas; useful data depends on source metadata | The handler does not check action visibility or an already-open dialog | Focused standard/preview image canvas | No image change |
| FITS Header | Direct canvas handler | `Ctrl+Shift+H` | Control and Shift required; extra Alt/Meta are not rejected | Opens FITS header viewer | Any focused image canvas; meaningful rows require FITS-style metadata | The handler does not check action visibility or an already-open dialog | Focused standard/preview image canvas | No image change |
| Summary | Direct canvas handler with action gate | `Ctrl+Shift+M` | Control and Shift required; extra Alt/Meta are not rejected | Opens image Summary | Focused standard canvas and Summary action visible/enabled | Computes/readies summary information | Focused standard image canvas | No image change |
| Image History | Direct canvas handler | `Ctrl+Shift+Z` | Control and Shift required; extra Alt/Meta are not rejected | Opens Image History | Supported only by a focused standard canvas; preview canvases have no history, so the key does nothing there even though the direct handler lacks a mode gate | From a preview canvas it is a safe no-op, as are the direct `Ctrl+Z`/`Ctrl+Y` paths; this key is also Mask Drawing redo | Focused image canvas; standard mode is the valid scope | No binding persistence; dialog actions can change history |
| Autostretch Settings | Direct canvas handler | `Ctrl+Shift+A` | Control and Shift required; extra Alt/Meta are not rejected | Opens Autostretch Settings | Any focused image canvas | The standard and static-preview toolbars expose the action and RT Preview does not, but the direct handler is not mode-gated; each press opens a new window | Focused standard/preview image canvas | Settings changes follow their normal persistence |
| Plate Solve | Direct canvas handler with action gate | `Ctrl+P` | Control required and Shift absent; extra Alt/Meta are not rejected | Opens or activates Plate Solve | Focused standard still-image canvas and Plate Solve action visible/enabled | Requires configured solver/catalog prerequisites | Focused standard image canvas | Solve results can add astrometric metadata |
| Astrometry Info | Direct canvas handler with action gate | `Ctrl+Shift+P` | Control and Shift required; extra Alt/Meta are not rejected | Opens or activates astrometry solution details | Focused standard still-image canvas and Astrometry Info visible/enabled | No effect without a valid celestial WCS | Focused standard image canvas | No image change |
| Show Astrometry | Direct canvas handler with action gate | `Ctrl+Shift+W` | Control and Shift required; extra Alt/Meta are not rejected | Toggles the astrometry overlay master switch | Focused solved standard still-image canvas and action visible/enabled | Display work increases with enabled layers | Focused standard image canvas | Master state is session-transient |
| Equatorial Grid | Direct canvas handler with action gate | `Ctrl+1` | Control required and Shift absent; extra Alt/Meta are not rejected | Toggles the coordinate grid | Solved standard still, Astrometry Settings visible/enabled, layer action present | Conflicts with Image Integration's Bias-tab key in that window | Focused standard image canvas | Layer preference persists |
| Deep-Sky Annotations | Direct canvas handler with action gate | `Ctrl+2` | Control required and Shift absent; extra Alt/Meta are not rejected | Toggles deep-sky labels | Same astrometry prerequisites | Catalog projection/rendering cost | Focused standard image canvas | Layer preference persists |
| Star Labels | Direct canvas handler with action gate | `Ctrl+3` | Control required and Shift absent; extra Alt/Meta are not rejected | Toggles bright-star labels | Same astrometry prerequisites | Catalog projection/rendering cost | Focused standard image canvas | Layer preference persists |
| Detected Stars | Direct canvas handler with action gate | `Ctrl+4` | Control required and Shift absent; extra Alt/Meta are not rejected | Toggles detected-star markers | Same astrometry prerequisites | Detection/rendering cost | Focused standard image canvas | Layer preference persists |
| SIP Distortion Map | Direct canvas handler with action gate | `Ctrl+5` | Control required and Shift absent; extra Alt/Meta are not rejected | Toggles SIP distortion vectors | Same astrometry prerequisites and SIP metadata for visible vectors | Rendering cost grows with sampled vectors | Focused standard image canvas | Layer preference persists |
| Peek at original | Press-and-release canvas handler | Hold `Space` | RT Preview only; modifiers are not rejected; auto-repeat ignored | Press toggles alternate/original display and release toggles back | Focused RT-preview canvas | Losing focus mid-gesture can leave the current toggle state until another switch | Focused RT-preview canvas | Display only; no image/history change |
| Switch original/preview | Direct canvas handler | `Ctrl+Shift+O` | RT Preview only; Control and Shift required; extra Alt/Meta are not rejected | Toggles original versus processed preview without hold behavior | Focused RT-preview canvas | Same key adds a recursive folder in Image Integration | Focused RT-preview canvas | Display only |
| Take snapshot | Direct canvas handler | `Ctrl+Shift+K` | RT Preview only; Control and Shift required; extra Alt/Meta are not rejected | Creates the process preview snapshot | Focused RT-preview canvas and process snapshot support | Allocates a new image/view and captures the current preview result | Focused RT-preview canvas | Snapshot has its normal image/history behavior |

### Session Console

| Item/exact label | Type/unit/category | Default/current value | Valid values/constraints | Meaning/effect | Dependencies/applicability | Trade-off/side effect | Scope | Persistence/metadata |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Search... | Fixed action shortcut | `Ctrl+F` | Exact key code | Opens the Search log dialog | Session Console open and Search action enabled | Same key targets Dashboard or Image Integration in those windows | Window holding the console: the main window while docked, its own window while floating | Search cursor is transient |
| Export visible session text | Fixed action shortcut | `Ctrl+S` | Exact key code | Opens export for the currently visible console text | Session Console open | Exports the filtered/visible session, not the complete log sink | Window holding the console: the main window while docked, its own window while floating | Writes the chosen text file |
| Find next | Button shortcut | `Return` | Search log dialog open, nonempty query, button enabled | Finds the next or previous match using dialog options | Search dialog active | `Return` is consumed by this search action | Search log dialog | Search cursor changes; no log mutation |

### Process windows and Image Integration

| Item/exact label | Type/unit/category | Default/current value | Valid values/constraints | Meaning/effect | Dependencies/applicability | Trade-off/side effect | Scope | Persistence/metadata |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Close process window | Generic direct handler | `Escape` | Key event must reach `TopLevelProcessBaseControl` | Requests close and runs that process's normal close checks | Top-level native process window; a focused child can consume Escape first | Can close a process; dirty/running-process safeguards still apply | Most native top-level process windows | Normal process settings/geometry close behavior applies |
| Auto-sort by file type... (Add Files) | Image Integration action shortcut | `Ctrl+O` | Exact key string | Adds selected files and auto-detects frame type | Image Integration window active | Overrides main Open in this window | Image Integration window | Project/model and recent input state change |
| Auto-sort by file type... (Recursive Folder) | Image Integration action shortcut | `Ctrl+Shift+O` | Exact key string | Adds supported files recursively and auto-detects type | Image Integration window active | Overrides RT Preview switch key in this window | Image Integration window | Project/model state changes |
| Remove Selected | Image Integration standard-key shortcut | `Delete` on supported Windows/Linux builds | Enabled when the current source tree - or, from the Calibration, Registration, Stacking, or Summary tab, the most recently used one - has a selection | Removes selected frames from the project model, including a hidden selection of the last used source tab | A source-tree selection exists | Does not delete source files; registration quality table has a different Delete action | Image Integration window | Undoable model edit when command wiring applies |
| Remove All Frames... | Image Integration action shortcut | `Ctrl+Shift+Delete` | Exact key string; action enabled when any frames exist | Requests removal of every loaded frame | Image Integration window with frames | Large project edit; asks for confirmation, which gives no undo hint | Image Integration window | Project/model state changes |
| Go to Group... | Image Integration action shortcut | `Ctrl+G` | Exact key string | Opens group search and centers the chosen group in the current source tree | Image Integration window active | No effect if no matching group is chosen | Image Integration window/current frame tree | Selection/expansion only |
| Import Project... | Image Integration action shortcut | `Ctrl+I` | Exact key string | Opens a saved Image Integration project | Image Integration window active | Replaces/merges state according to import workflow | Image Integration window | Project state and recent-project list can change |
| Export Project... | Image Integration action shortcut | `Ctrl+E` | Exact key string | Saves current project state | Image Integration window active | Writes project data, not integrated image output | Image Integration window | Project file and recent-project state can change |
| Execute Integration | Image Integration action shortcut | `Ctrl+Enter` | Exact key string, which Qt reads as Control with the keypad `Enter` key; Control with the main `Return` key is not bound; action enabled by current validation/run state | Starts the configured workflow | Valid project and no incompatible active run | CPU, memory, and disk intensive; review Summary and output path first | Image Integration window | Creates workflow outputs, records, and report |
| Bias tab | Image Integration action shortcut | `Ctrl+1` | Exact key string | Selects Bias | Image Integration window active | Same key toggles an astrometry layer in a focused image view | Image Integration window | Tab selection is transient |
| Darks tab | Image Integration action shortcut | `Ctrl+2` | Exact key string | Selects Darks | Image Integration window active | Context resolves duplicate meanings | Image Integration window | Tab selection is transient |
| Flats tab | Image Integration action shortcut | `Ctrl+3` | Exact key string | Selects Flats | Image Integration window active | Context resolves duplicate meanings | Image Integration window | Tab selection is transient |
| Lights tab | Image Integration action shortcut | `Ctrl+4` | Exact key string | Selects Lights | Image Integration window active | Context resolves duplicate meanings | Image Integration window | Tab selection is transient |
| Calibration tab | Image Integration action shortcut | `Ctrl+5` | Exact key string | Selects Calibration | Image Integration window active | Context resolves duplicate meanings | Image Integration window | Tab selection is transient |
| Registration tab | Image Integration action shortcut | `Ctrl+6` | Exact key string | Selects Registration | Image Integration window active | None beyond focus context | Image Integration window | Tab selection is transient |
| Stacking tab | Image Integration action shortcut | `Ctrl+7` | Exact key string | Selects Stacking | Image Integration window active | None beyond focus context | Image Integration window | Tab selection is transient |
| Summary tab | Image Integration action shortcut | `Ctrl+8` | Exact key string | Selects Summary | Image Integration window active | None beyond focus context | Image Integration window | Tab selection is transient |
| Keyboard Shortcuts | Image Integration standard-key shortcut | `F1` on supported Windows/KDE/GNOME builds | Qt `HelpContents` primary binding | Opens Image Integration's local shortcut help | Image Integration window active | Overrides main User Guide; its current help text incorrectly advertises unbound `Ctrl+Z`/`Ctrl+Y` | Image Integration window | No state change |
| Refresh View | Image Integration `QShortcut` | `F5` | Window shortcut | Refreshes theme, Registration and Stacking data, the tab badges, and the frame-tab labels | Image Integration window active | May re-query/rebuild large tables | Image Integration window | View state only |
| Focus Search Bar | Image Integration standard-key shortcut | `Ctrl+F` on supported Windows/Linux builds | Every Qt `Find` binding, the dedicated Find key included; the tab's own key handler also takes `Ctrl+F` | Focuses and selects search text in the active Bias/Darks/Flats/Lights source tab | One of the four source tabs active | Overrides Dashboard/Session Console search in this window | Active frame-source tab and children | Current search text is tab state; binding does not persist |
| Select All Frames | Direct tree handler | `Ctrl+A` | Focused `FrameTreeView` | Selects every row in that source tree | Frame tree focused | A large selection can make the next bulk edit consequential | Focused Bias/Darks/Flats/Lights tree | Selection only |
| Clear frame selection | Direct tree handler | `Escape` | Focused `FrameTreeView` | Clears selection without closing Image Integration | Frame tree focused | Consumes the generic process-close key | Focused Bias/Darks/Flats/Lights tree | Selection only |
| Go to First Frame | Direct tree handler | `Home` | Focused `FrameTreeView` with a model | Selects the first child frame, or first group if empty | Frame tree focused | Replaces the current selection | Focused Bias/Darks/Flats/Lights tree | Selection only |
| Go to Last Frame | Direct tree handler | `End` | Focused `FrameTreeView` with a model | Selects the last child frame, or last group if empty | Frame tree focused | Replaces the current selection | Focused Bias/Darks/Flats/Lights tree | Selection only |
| Open registration group settings | Widget-with-children shortcuts | `Return` or keypad `Enter` | Registration group table itself must have focus | Opens settings for the selected registration group | Selected group and focused Registration groups table | Child editors do not trigger because the table focus check is exact | Registration groups table | Settings changes follow project undo/persistence |
| Open stacking group settings | Widget-with-children shortcuts | `Return` or keypad `Enter` | Stacking group table itself must have focus | Opens settings for the selected integration group | Selected group and focused Stacking groups table | Child editors do not trigger because the table focus check is exact | Stacking groups table | Settings changes follow project undo/persistence |
| Exclude Selected | Widget-with-children standard-key shortcut | `Delete` on supported Windows/Linux builds, plus `Ctrl+D` on Linux (every Qt `Delete` binding) | Registration frame-quality table focused and Exclude Selected enabled | Clicks Exclude Selected for the current quality rows | Valid focused selection | When the last used source tab still holds a selection, Remove Selected is enabled too; Qt then treats `Delete` as ambiguous and neither command fires - clear that selection or use the button | Registration frame-quality table | Changes workflow exclusion state |
| Move quality-chart selection | Direct chart handler | `Left`/`Up` or `Right`/`Down` | Nonempty sorted quality points | Selects previous or next point; from no selection, previous starts at last and next at first | Quality distribution chart focused | Emits frame selection and can update linked inspection | Registration quality chart | Selection only |
| Select first quality point | Direct chart handler | `Home` | Nonempty sorted quality points | Selects the first sorted point | Quality distribution chart focused | Sort/filter choice defines “first” | Registration quality chart | Selection only |
| Select last quality point | Direct chart handler | `End` | Nonempty sorted quality points | Selects the last sorted point | Quality distribution chart focused | Sort/filter choice defines “last” | Registration quality chart | Selection only |
| Activate tab issue badge | Direct accessibility handler | `Return`, keypad `Enter`, or `Space` | Focused warning/error badge | Selects the badge's owning tab | Badge must have focus and remain attached to a tab | Navigation only; does not repair the issue | Image Integration tab bar | Tab selection only |
| Open Summary issue target | Direct accessibility handler | `Return`, keypad `Enter`, or `Space` | Focused issue card | Runs the card's target-navigation callback | Summary issue card focused | Navigation only; follow the target's validation guidance | Image Integration Summary | Selection/navigation only |

### Blink Comparator

| Item/exact label | Type/unit/category | Default/current value | Valid values/constraints | Meaning/effect | Dependencies/applicability | Trade-off/side effect | Scope | Persistence/metadata |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Play/Pause | Direct dialog handler | `Space` | Dialog receives key | Toggles timed blink playback | Blink Comparator active | Same key is RT Preview peek or focused-card activation elsewhere | Blink Comparator dialog | Playback state is transient |
| Previous image/target | Direct dialog handler | `Left` | Dialog receives key | Moves backward, wrapping normal frame lists; skips the fixed reference in history comparison | At least one usable row | Current inspection target changes | Blink Comparator dialog | Selection only |
| Next image/target | Direct dialog handler | `Right` | Dialog receives key | Moves forward with the same mode-specific rules | At least one usable row | Current inspection target changes | Blink Comparator dialog | Selection only |
| Close | Direct dialog handler | `Escape` | Dialog receives key | Closes the comparator | Blink Comparator active | Result collection retains current exclusions/ratings | Blink Comparator dialog | Window size is remembered; result state returns to caller |
| Exclude current frame | Direct dialog handler | `X` | Dialog receives key | Marks the current canonical path excluded; pressing it again does not unexclude | Current path must resolve | Affects later caller decisions; does not delete the file | Blink Comparator dialog | Exclusion is retained in the returned dialog result |
| Rate current frame | Direct dialog handler | `1` through `9` | Digit key and nonempty current canonical path; modifiers are not rejected | Stores that integer rating for the current path | Current path must resolve | A modified digit can also replace the prior rating for that path | Blink Comparator dialog | Rating is retained in the returned dialog result |

### Mask Drawing

| Item/exact label | Type/unit/category | Default/current value | Valid values/constraints | Meaning/effect | Dependencies/applicability | Trade-off/side effect | Scope | Persistence/metadata |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Decrease Size | Widget-with-children shortcut | `[` | Unmodified key | Decreases brush size by 1, clamped at 1 | Mask Drawing window active | Keyboard layout affects bracket placement | Mask Drawing and children | Tool state lasts for the process window |
| Increase Size | Widget-with-children shortcut | `]` | Unmodified key | Increases brush size by 1, clamped at 200 | Mask Drawing window active | Larger brushes change more pixels per stroke | Mask Drawing and children | Tool state lasts for the process window |
| Toggle Brush/Eraser | Widget-with-children shortcut | `X` | Unmodified key | Switches Brush to Eraser or Eraser to Brush; from another tool it selects Brush | Mask Drawing window active | Same key excludes a frame in Blink Comparator | Mask Drawing and children | Tool state lasts for the process window |
| Fill | Widget-with-children shortcut | `F` | Unmodified key | Selects Fill | Mask Drawing window active | Can affect a large connected region | Mask Drawing and children | Tool state lasts for the process window |
| Gradient | Widget-with-children shortcut | `G` | Unmodified key | Selects Gradient | Mask Drawing window active | Requires an intentional drag direction/extent | Mask Drawing and children | Tool state lasts for the process window |
| Delete selected shape | Widget-with-children shortcut | `Delete` | Unmodified primary Delete key | Removes the selected editable shape | Mask Drawing window active and selected shape | Does not clear raster strokes; undo is available | Mask Drawing and children | Drawing document/history changes |
| Undo mask edit | Platform-standard widget shortcut | `Ctrl+Z`; also `Alt+Backspace` on Windows, `F14` on Linux, and the Undo key | Every Qt `Undo` binding | Undoes the last drawing-document command | Mask Drawing window active and undo available | This is local mask history, not main image history | Mask Drawing and children | Drawing document/history changes |
| Redo mask edit | Platform-standard widget shortcut | Windows `Ctrl+Y`, `Ctrl+Shift+Z`, or `Alt+Shift+Backspace`; KDE/GNOME `Ctrl+Shift+Z`; the Redo key on both | Every Qt `Redo` binding | Redoes the next drawing-document command | Mask Drawing window active and redo available | `Ctrl+Shift+Z` matches the image-view History key on both platforms, but active-window/focus context resolves it | Mask Drawing and children | Drawing document/history changes |

## How to use this reference

1. Identify the active window and the focused control. If uncertain, click the
   intended canvas, tree, table, chart, card, or process window.
2. Find that context's row and check its prerequisite. For an action-backed
   command, also inspect whether the matching menu/toolbar action is enabled.
3. Check the conflict column before using repeated keys such as `Ctrl+O`,
   `Ctrl+F`, `Ctrl+1`–`Ctrl+5`, `Delete`, `Escape`, `Space`, or `X`.
4. Invoke the shortcut once and verify the observable result: a dialog opens, a
   tab/focus/selection changes, a display toggle changes, or a workflow prompt
   appears. Do not repeat an expensive or destructive command because focus was
   uncertain.
5. If the result differs, use the equivalent visible action and consult the
   troubleshooting table below.

## Implementation behavior and model

AfterNight uses three keyboard-dispatch patterns:

- Main, Session Console, and most Image Integration menu bindings are
  `QAction` shortcuts. Qt's default action context is window scope, so the
  action belongs to the active window and its enabled state applies.
- Image Integration refresh/search and several table controls use `QShortcut`.
  The default is also window scope, while the source explicitly selects
  widget-with-children scope for frame-source search, registration/stacking
  table commands, and every Mask Drawing binding.
- Image canvases, frame trees and the frame-source tab, charts, badges, Summary
  issue cards, process windows, and Blink Comparator handle key events directly.
  These require the event to reach that exact control; a child editor or another
  focused widget can consume the key first.

Direct handlers do not share one modifier policy. The image canvas selects its
branches from Control and Shift but does not reject Alt/Meta. Generic process
close, frame-tree `Escape`/`Home`/`End`, quality-chart
navigation, badges/cards, and Blink Comparator compare only key codes. Frame-tree
Select All requires Control but does not reject additional modifiers. Treat the
matrix chords as the supported canonical inputs; modified variants accepted by
these handlers are implementation behavior and can conflict with desktop or
widget commands.

The image canvas ignores auto-repeat in its event filter. RT Preview `Space`
therefore changes display once on press and once on release. Other direct
handlers are press-only. Astrometry, Histogram, Statistics, Summary, Plate
Solve, and Astrometry Info use explicit action-availability checks; FITS, EXIF,
History, Autostretch Settings, and Fit do not all share those same visibility
checks.

Qt `StandardKey` bindings are platform-dependent. A `QAction` given a standard
key (`HelpContents`, Remove Selected's `Delete`) takes only its primary
sequence, while a `QShortcut` built from one (`Find` in the frame-source tab,
the quality table's `Delete`, Mask Drawing's `Undo` and `Redo`) binds every
alternative Qt returns. Consequently the material supported-platform
difference in this inventory is Mask Drawing Redo: Linux lacks `Ctrl+Y`, while
both platforms accept `Ctrl+Shift+Z`. Most
other displayed shortcuts are explicit `Ctrl` strings or key codes. Qt maps
the `Ctrl` naming to Command on Apple platforms, but macOS is not an AfterNight
target covered by this guide.

## Inputs, outputs, persistence, and metadata

The input to a shortcut is the active window, focused widget, modifier state,
key, and current action/control state. A binding produces exactly the same
command as its connected action or handler. It does not bypass validation,
confirmation, cancellation, masks, sequence rules, metadata handling, or
output policy documented by the destination feature.

Bindings themselves are not persisted. Some invoked commands change transient
selection/display state; others change image history, Image Integration project
state, drawing history, application settings, metadata, or files. Follow the
linked feature page for those effects.

## Performance and resource implications

Keyboard dispatch is negligible. The invoked operation has the same cost as its
menu, toolbar, button, or card equivalent. Histogram/Statistics/Summary can
scan image data, Plate Solve can use external/catalog resources, an RT Preview
snapshot allocates image storage, Image Integration refresh can rebuild large
tables, and Execute Integration can consume substantial CPU, memory, and disk.
Shortcut use does not make those operations cheaper or synchronous.

## Worked lookup example

- Evidence mode: Source-and-test-derived; the focused tests were last run on
  the Windows MSVC test build.
- Question: Why does `Ctrl+O` add calibration/light frames instead of opening a
  normal image picker?
- Lookup: The main **File > Open** action owns `Ctrl+O` in the main window.
  Image Integration defines another window-scoped `Ctrl+O` on **Frames > Add
  Files > Auto-sort by file type...**.
- Verified conclusion: the active Image Integration window resolves the key to
  Add Files. Activate the main window before pressing `Ctrl+O` to use normal
  Open. The focused tests verify the Image Integration binding; current action
  sources establish the main binding and window scope. No file picker was
  opened and no dataset was loaded for this example.

## Warnings and limitations

- There is no current shortcut customization or conflict-resolution UI.
- The Dashboard onboarding panel currently advertises `Ctrl+N` for New
  Sequence, but `actionNew_Sequence` has no assigned shortcut. Use the Dashboard
  card or **File > New Sequence...**.
- Image Integration's local `F1` help currently lists `Ctrl+Z` and `Ctrl+Y`, but
  its undo/redo actions do not assign either shortcut. Use **Edit > Undo** and
  **Edit > Redo** there. Its selected-frame and per-tab remove-all confirmation
  text also says removal can be undone with `Ctrl+Z`; the edit is undoable, but
  the key is not bound. The window-wide Remove All Frames prompt gives no undo
  hint. The consolidated matrix intentionally does not present those stale
  hints as active bindings.
- `Ctrl+Shift+H` and `Ctrl+Shift+X` call their viewers without checking whether
  the corresponding toolbar action is visible or whether another viewer is
  already open. Use the standard-view toolbar when state is uncertain.
- A desktop environment, input method, accessibility tool, or window manager
  can intercept keys such as `F1`, `F5`, `F11`, or `Ctrl+Space` before
  AfterNight receives them. This page does not claim cross-desktop runtime
  delivery.
- `Ctrl++`, brackets, punctuation, and shifted digits depend on physical
  keyboard layout. Use the visible action if the documented sequence is not
  produced by your layout.
- Text editors and standard Qt widgets can consume editing/navigation keys.
  Move focus to the named target rather than assuming a window-wide handler.
- Shortcut labels describe interaction, not data safety. Review confirmation,
  output, history, and cancellation behavior before bulk removal, workflow
  execution, snapshot creation, undo/redo, or save/export.

## Troubleshooting

| Symptom/question | Verified interpretation or cause | Resolution/next page | Evidence source |
| --- | --- | --- | --- |
| A shortcut does nothing | Wrong active window/focus, disabled action, missing image/model state, or desktop interception | Focus the named control and try the equivalent visible action; see [Toolbar & Menus](../interface/toolbar-and-menus.md) | Action scope/state and direct-handler sources |
| `Ctrl+O` adds frames | Image Integration owns that key while its window is active | Activate the main window for normal Open; see [Image Integration](../processes/stacking-integration/image-integration.md) | Main UI builder and Image Integration menu source/tests |
| Dashboard `Ctrl+N` does nothing | The onboarding hint is stale; New Sequence has no shortcut | Use the New Sequence card or **File > New Sequence...** | Dashboard onboarding and main UI-builder sources |
| `Ctrl+F` searches the wrong surface | Dashboard, Session Console, and Image Integration each define a local search command | Activate the intended window/tab first | Dashboard, Log Viewer, and Frame Source Tab sources |
| `Delete` excludes quality rows instead of removing source frames, or does nothing in the quality table | Registration's focused quality table owns the local exclusion command; when the last used source tab still holds a selection, Remove Selected is also enabled and Qt treats `Delete` as ambiguous, so neither fires | Focus the intended frame-source tree, clear the hidden source selection, or use the named button/menu action | Registration Tab and Image Integration action-state sources |
| `Escape` does not close a process | A focused frame tree consumes it to clear selection, or another child consumes it | Check selection/focus, then use the process close control | Frame Tree and top-level process handlers |
| `Ctrl+1`–`Ctrl+5` switches tabs instead of overlays | Image Integration is the active window | Focus a solved standard image canvas to control astrometry layers | Image Integration actions and image-view handler |
| Mask Drawing redo is not `Ctrl+Y` on Linux | It uses Qt's platform-standard primary Redo sequence | Use `Ctrl+Shift+Z` on KDE/GNOME or the Redo control | Qt `QKeySequence` and Mask Drawing source |
| Image Integration `Ctrl+Z`/`Ctrl+Y` does nothing | The help rows and removal-confirmation `Ctrl+Z` promise are stale; no shortcut is assigned | Use **Edit > Undo/Redo** | Image Integration menu/help/confirmation sources and QAction's no-default-shortcut contract |
| `Space` behaves unexpectedly | It activates a focused button or Summary issue card, peeks in RT Preview, or plays/pauses Blink according to context | Confirm focus and use the visible action if needed | Direct key-handler sources |

## Related pages

- [Toolbar & Menus](../interface/toolbar-and-menus.md) is the authority for
  menu hierarchy, action availability, and non-shortcut commands.
- [Image Viewer](../interface/image-viewer.md) explains display modes, zoom,
  analysis tools, snapshots, and focused-view behavior.
- [Astrometry Overlays](../interface/astrometry-overlays.md) defines WCS and
  layer prerequisites for `Ctrl+1` through `Ctrl+5`.
- [Session Console](../interface/log-viewer.md) explains search and export
  semantics.
- [Image Integration](../processes/stacking-integration/image-integration.md)
  defines project edits, validation, execution, tables, charts, Summary, and
  Blink review flows.
- [Mask Drawing](../processes/masking/mask-drawing.md) explains drawing tools,
  document history, generated masks, and output/assignment.
- [Troubleshooting And Diagnostics](troubleshooting.md) routes shortcut and
  focus symptoms back to this authoritative inventory and its scope rules.

## References and provenance

- AfterNight source and focused tests, re-read 2026-10-18.
- [Qt 6 QKeySequence](https://doc.qt.io/qt-6/qkeysequence.html), including
  platform `StandardKey` mappings and keyboard-layout notes, reviewed
  2026-07-20 for the project-pinned Qt 6.10.3 behavior family.
- [Qt 6 QAction](https://doc.qt.io/qt-6/qaction.html), including the default
  `WindowShortcut` context and the absence of a default shortcut, rechecked
  2026-07-20.
- [Qt 6 QShortcut](https://doc.qt.io/qt-6/qshortcut.html), including shortcut
  contexts, activation, ambiguity, and auto-repeat behavior, rechecked
  2026-07-20.

<!-- Media is intentionally Not needed; this reference is more reliable as searchable text. -->
