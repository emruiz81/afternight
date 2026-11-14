# AfterNight Theme System

> **Audience:** AI coding agents and human contributors implementing or modifying UI controls.
>
> **Source of truth:** The files in `src/AfterNightControls/theme/` and `src/AfterNightControls/components/`.

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Design Tokens (`AnDesignTokens`)](#design-tokens-andesigntokens)
3. [Proxy Style (`AnProxyStyle`)](#proxy-style-anproxystyle)
4. [KDDockWidgets Style (`KddwProxyStyle`)](#kddockwidgets-style-kddwproxystyle)
5. [Layout Utilities (`AnLayoutUtils`)](#layout-utilities-anlayoututils)
6. [Animation Utilities (`AnimationUtils`)](#animation-utilities-animationutils)
7. [Icon Management (`IconManager`)](#icon-management-iconmanager)
8. [Semantic Color Bridge (`UiStyleTokens`)](#semantic-color-bridge-uistyletokens)
9. [Reusable Components (`components/`)](#reusable-components-components)
10. [ABSOLUTE RULES — What Is Forbidden](#absolute-rules--what-is-forbidden)
11. [How to Style a New Widget or Control](#how-to-style-a-new-widget-or-control)
12. [Custom Widget with `paintEvent()` — Step by Step](#custom-widget-with-paintevent--step-by-step)
13. [Dynamic Properties for Variant Styling](#dynamic-properties-for-variant-styling)
14. [Theme Switching & Refresh Flow](#theme-switching--refresh-flow)
15. [Responsiveness & Resolution Policy](#responsiveness--resolution-policy)
16. [Spacing, Radius, and Typography Token Reference](#spacing-radius-and-typography-token-reference)
17. [Color Token Reference](#color-token-reference)
18. [Contrast Validation & Accessibility](#contrast-validation--accessibility)
19. [Checklist for New Controls](#checklist-for-new-controls)

---

## Architecture Overview

```
┌──────────────────────────────────────────────────────────┐
│                     Application Startup                   │
│  installAnProxyStyle()  →  QApplication::setStyle(proxy) │
│  installKddwProxyStyle(dockWindow)                       │
└───────────────┬──────────────────────────┬───────────────┘
                │                          │
    ┌───────────▼──────────┐   ┌───────────▼──────────────┐
    │    AnProxyStyle       │   │    KddwProxyStyle        │
    │  (all standard Qt     │   │  (KDDockWidgets-specific │
    │   widgets)            │   │   overrides)             │
    └───────────┬──────────┘   └───────────┬──────────────┘
                │                          │
    ┌───────────▼──────────────────────────▼──────────────┐
    │              AnDesignTokens (singleton)              │
    │  ColorTokens · SpacingTokens · RadiusTokens         │
    │  LayerTokens · MaterialTokens · TypographyTokens    │
    │  MetricTokens                                       │
    └─────────────────────────────────────────────────────┘
```

**Key principles:**

- **No QSS.** Ever. See [Absolute Rules](#absolute-rules--what-is-forbidden).
- Every visual aspect is controlled by either `AnProxyStyle` (C++ `QProxyStyle` overrides) or custom `paintEvent()` implementations.
- All colors, spacings, radii, fonts and control sizes are resolved through the `AnDesignTokens` singleton — never hardcoded.
- The system supports dark/light themes, multiple accent families, contrast profiles, surface tints and, under the Observatory profile, two densities — all switchable at runtime.

### File Map

| File | Purpose |
|---|---|
| `theme/AnDesignTokens.h/.cpp` | Token definitions, resolution pipeline, theme preference API |
| `theme/StyleProfile.h` | The style profiles, `Classic` and `Observatory` (see [Profiles](#profiles)) |
| `theme/ObservatoryPalette.h/.cpp` | The Observatory profile's tables, derivations and contrast rules (see [The surface ladder](#the-surface-ladder)), its sizes (see [Density](#density)) and Night's tones and rules (see [Night](#night)) |
| `theme/NightImage.h/.cpp` | Night's form of a colour and of a picture: `nightColor`, `colorAsShown`, `nightImage` and its cache, `NightPicture` (see [Night](#night)) |
| `theme/NightImageWidgets.h` | Every widget that paints a picture, and how it shows it under Night |
| `widgets/NightPictureLabel.h/.cpp` | A label showing a picture in its Night form under Night |
| `controls/settings/ThemePresetTile.h/.cpp` | A colour preset's tile on the Interface page (see [Presets](#presets)) |
| `theme/AnProxyStyle.h/.cpp` | Global `QProxyStyle` for all standard Qt widgets (~3000 LOC) |
| `theme/KddwProxyStyle.h/.cpp` | `QProxyStyle` for KDDockWidgets subsystem (~700 LOC) |
| `theme/AnimationUtils.h/.cpp` | Global animation toggle (accessibility) |
| `theme/PainterGuard.h` | `an::ui::PainterGuard`, the RAII `QPainter` save/restore guard |
| `components/AnLayoutUtils.h` | Layout spacing/margin normalization to design tokens |
| `app/UiStyleTokens.h/.cpp` | Palette-based semantic color bridge utilities |
| `app/IconManager.h/.cpp` | SVG icon loading with theme-aware colorization |
| `app/ThemeManager.h/.cpp` | Applies a theme change: preference, both proxy styles, icons, open windows |
| `src/AfterNightStylePlugin/` | The `afternight` Qt style plugin; creates an `AnProxyStyle`, initial variant from `AN_THEME` |

---

## Design Tokens (`AnDesignTokens`)

Design tokens are the single source of truth for all visual parameters in the application. They live in `namespace an::ui` and are accessed through free functions.

### Token Categories

#### ColorTokens
Organized into semantic groups accessible via helper methods:

| Group | Accessor | Members |
|---|---|---|
| **Background** | `colors.bg()` | `primary`, `surface`, `card`, `cardPrimary`, `cardSecondary`, `input`, `hover` |
| **Text** | `colors.text()` | `primary`, `secondary`, `tertiary`, `accent`, `onAccent` |
| **Border** | `colors.border()` | `primary`, `subtle`, `focus` |
| **Status** | `colors.status()` | `success`, `warning`, `error`, `info`, plus `*Soft` variants |
| **Accent** | `colors.accent()` | `primary`, `hover`, `pressed`, `disabled`, `pinned`, `surfaceTint`, `surfaceBorderTint` |
| **Effects** | `colors.effects()` | `shadowOverlay` |
| **Observatory roles** | members only | `bgCanvas`, `bgWell`, `bgSnack`, `textOnSnack`, `stateLayer`, `scrim`, `scrimSoft`, `edge`, `textOnImage`, `textOnImageSecondary`, `accentOnImage`, `textOnAccentImage`, `accentContainer`, `textOnAccentContainer`, `accentTrack`, `accentSoft`, `accentSofter`, `accentLine`, `channelRed`/`Green`/`Blue`/`Gray`, `plotRed`/`Green`/`Blue`/`Gray` |

The Observatory roles are what the Observatory profile's design names. Each holds a colour under both profiles: under
`Classic` the colour AfterNight 1.1 paints in that role (the view's background around an image, the histogram's well,
the selected row's fill, the channel selectors' dots, the histogram's curves, and so on), so a widget shared by both
profiles that reads a role paints under `Classic` what it painted. `NewTokens.ClassicValuesAreWhatClassicPaints` in
`Test_Widgets` reads each from where 1.1 has it.

#### SpacingTokens
```cpp
struct SpacingTokens {
    int xs = 4;   // Tight spacing (indicator margins, small gaps)
    int sm = 8;   // Default element spacing
    int md = 12;  // Layout margins
    int lg = 16;  // Section spacing, tab padding
    int xl = 24;  // Dialog margins, large separations
};
```

#### RadiusTokens
```cpp
struct RadiusTokens {
    int none = 0;   // Sharp corners (never used for interactive controls)
    int sm   = 4;   // Buttons, inputs, combo boxes, tabs, menus
    int md   = 8;   // Group boxes, cards
    int lg   = 12;  // Process shell cards
    int xl   = 14;  // Large overlay panels
};
```

These are the `Classic` profile's radii. The `Observatory` profile's are `sm` 6 (small parts: tooltips, palette rows),
`md` 8 (controls), `lg` 12 (cards and popups), `xl` 16 (sheets); a pill is half its height.

#### LayerTokens
Five elevation layers derived from background colors:
```
surface0 → bgPrimary      (deepest, window background)
surface1 → bgSurface      (standard surface)
surface2 → bgCardSecondary (raised panels)
surface3 → bgCardPrimary   (elevated cards)
surface4 → bgHover         (highest elevation)
```

#### MaterialTokens
Deterministic fallbacks for native backdrop effects (blur/acrylic/mica):
- `solidSurface` — opaque surface (toolbar, status bar)
- `micaSurface` — window/palette background
- `acrylicSurface` — translucent card background

#### TypographyTokens
```cpp
struct TypographyTokens {
    QFont heading1;   // body.pointSize + 4, Bold
    QFont heading2;   // body.pointSize + 2, DemiBold
    QFont heading3;   // body.pointSize + 1, DemiBold
    QFont body;       // System default (10pt)
    QFont caption;    // body.pointSize - 1 (min 8pt)
    QFont monospace;  // "Cascadia Mono", Monospace hint
};
```

These are the `Classic` profile's fonts. The `Observatory` profile sizes its type scale by the density
([Density](#density)).

#### MetricTokens
The sizes of the interface in logical pixels: what a control, a row and a bar are high, the font sizes, a panel's
padding and gaps, a scroll bar's thickness (`fontBody`, `fontSmall`, `fontCaption`, `control`, `controlSmall`,
`controlXSmall`, `controlLarge`, `rowTable`, `rowDense`, `rowKeyValue`, `rowSettings`, `rowPalette`, `barTop`, `strip`,
`statusLine`, `rail`, `pad`, `gap`, `sectionHeader`, `chip`, `tag`, `floatingToolbar`, `scrollBar`). Under the
`Observatory` profile they are the density's column; under `Classic`, 1.1's sizes. See [Density](#density).

### Token Resolution Pipeline

Tokens go through a multi-stage resolution before becoming active. The resolution of one combination is a pure function,
`resolveDesignTokens(const ThemeChoices&)`: its inputs are a struct - the theme, the contrast profile, the accent family,
the surface tint, the tint strength, the style profile, the density and whether the native backdrop is used - and it reads and writes
no global, so a test resolves any combination without touching the application's state. `refreshTokens()` builds the
choices in force for each theme, resolves both and publishes them. Under the `Classic` profile the stages are these:

```
Base Tokens (createDarkTokens / createLightTokens)
       │
       ▼
Apply Accent Family (Blue, Teal, Amber, Amethyst, Rose)
       │
       ▼
Apply Surface Tint Family (Neutral, Ocean, Mint, Amethyst, Rose, Amber)
       │
       ▼
Apply Contrast Profile (High, Balanced, Soft, Oled)
       │
       ▼
Compute textOnAccent (WCAG contrast auto-selection)
       │
       ▼
Resolve Material Tokens (native vs. fallback backdrop)
       │
       ▼
Validate Contrast Ratios (debug builds only)
       │
       ▼
Publish Snapshot (lock-free atomic pointer swap)
```

Under the `Observatory` profile the base, accent, tint and contrast stages are replaced by the tables and derivations of
`theme/ObservatoryPalette.cpp` ([The surface ladder](#the-surface-ladder)); the materials and the publication are the
same, and the debug-build validation runs the profile's own rules. After the materials the profile's sizes are filled in:
the density's metrics, the radii and the type scale ([Density](#density)).

### Profiles

Two **style profiles** resolve the tokens. `Classic` resolves every token to the value AfterNight 1.1 paints;
`TokenBaseline.ClassicValuesAreRecorded` holds all 240 combinations of theme, contrast profile, accent family and surface
tint to a record, which is never written again whole. `Observatory` is the 2.0 look: the same user choices, resolved by
other tables, plus a **tint strength**.

- **The shell names the profile.** `ShellDescriptor::styleProfile` (`core/IShell.h`) says which profile a shell is drawn
  in; the application puts it in force with `setStyleProfile()` from the style step of `startApplication`, after the
  settings stores open and before the first widget exists.
- **A profile is fixed once a widget exists.** A widget is sized and painted under one profile for its life, so
  `setStyleProfile()` refuses a profile other than the one in force once `QApplication::allWidgets()` is not empty,
  logs the refusal and returns false. `styleProfile()` answers the profile in force; it is `Classic` until a shell
  names another.
- **Suites test a profile through `an::ui::testing::ScopedStyleProfile`** (`src/AfterNightTests/fixtures/ScopedStyleProfile.h`),
  which puts a profile in force for its scope whatever widgets exist and puts the previous one back. Only the test
  executables compile it; the library's `an::ui::testing::forceStyleProfile()` exists for it alone. A suite that needs
  only token values calls `resolveDesignTokens()` with the profile in its choices and needs neither.
- **The developer switch.** `AFTERNIGHT_STYLE_PROFILE=observatory` (or `classic`, in any case) in the environment starts
  the chosen shell under that profile instead of the one its descriptor names (`ShellSelection::kStyleProfileVariable`);
  an unknown value is passed over with a warning in the log. It is a developer aid, to look at the Observatory colours
  in the classic shell, and goes when every shell draws in a profile of its own.
- **The tint strength** - `ThemeTintStrength {Off, Subtle, Balanced, Strong}`, `setThemeTintStrength()`, stored as
  `interface/themeTintStrength` (`off`, `subtle`, `balanced`, `strong`; default `balanced`) and read at start with the
  other theme choices - is an input of both profiles; `Classic` ignores it.

### The surface ladder

The Observatory profile builds its surfaces as a **ladder**: five neutral steps per theme and contrast profile, each with
its share of a softened tint. Everything below is data in `theme/ObservatoryPalette.cpp`; `mixColors(a, p, b)` is `p`
of `a` and the rest of `b` per channel in sRGB with premultiplied alpha, as CSS `color-mix(in srgb)` mixes them, each
component rounded to the nearest 8-bit value.

- **Neutrals and text.** Each theme and contrast profile has five neutral surfaces `n0`-`n4`, three text tones and the
  opacity of its line and outline (white in the dark theme, `#18181B` in the light one). OLED is the dark theme's: the
  light theme resolves it as Balanced, as `Classic` does. The Soft profile's secondary text is set by its third surface
  under the Strong tint, the most tinted that surface gets: it is the lightest tone (dark theme) or the darkest (light
  theme) that keeps 4.5 : 1 there.
- **The softened tint.** A tint's hue is first mixed with a lift grey (`#808086` dark, `#8A8A90` light), keeping a share
  of itself: all of it for Ocean, and in the dark theme 66 % for Mint, 78 % for Amethyst, 44 % for Rose and 40 % for
  Amber (78 % and 72 % for Rose and Amber in the light theme). A raw warm hue turns a dark neutral brown or maroon; pulled
  toward the grey first, it warms the graphite instead.
- **The strength's shares.** Off gives the tint no share, Subtle 5 % to surfaces and 8 % to lines, Balanced 8 % and 12 %
  (1.1's blend), Strong 12 % and 18 %. The Neutral tint is Off whatever the strength.
- **The ladder.** Surface `i` is `mixColors(tint, share x k_i, n_i)` with `k` = 1, 1.5, 2, 2.5, 2.5 in the dark theme
  (a tint shows less on dark) and 0.75, 1, 1.5, 1.75, 2 in the light one. Under OLED surfaces 0 and 1 take no tint:
  black stays black. The line and the outline take the line share; the well and the canvas the surface share; the
  transient message's fill two and a half times it.
- **The existing roles read the ladder**, so every widget repaints under the profile without an edit: `bgPrimary` is
  surface 0, `bgSurface` 1, `bgCard` and `bgCardPrimary` 2, `bgCardSecondary` and `bgInput` 3, `bgHover` 4;
  `borderSubtle` is the line and `borderPrimary` the outline; `borderFocus` is the accent's text tone;
  `cardSurfaceTint` and `cardSurfaceBorderTint` are the softened tint while the surfaces carry any of it.
- **Accents.** The five families keep their 300-800 steps. The dark theme fills with 400, hovers 300, presses 500 and
  writes accent text in 300, with `#0B1020` on the fill; the light theme uses 600, 700, 800 and 700 with white on the
  fill, a tone darker for Teal and Amber so white keeps 4.5 : 1. The container is the accent mixed into the third
  neutral, untinted (28 % dark, 18 % light), its text the accent's text tone at 45 % over the primary text, the track
  the accent at 30 % over the fourth neutral, and the soft forms the accent at 16 %, 8 % and 45 %. Mixed over the bare
  neutrals, the container and the track read the same whatever the tint, and the container's text keeps 4.5 : 1 under
  every tint and strength, which over the tinted steps it did not.
- **Status, channels, images.** The status colours, the pinned colour and the channels' dots are per theme; a curve is
  drawn in a well, dark in both themes, so the plot colours are the dark ones in both. What is laid over an image - the
  text, the accent's 300 step, the text on it - is the same in both themes: an image is judged against dark.
- **The rules.** `observatoryContrastFailures()` holds a resolved set to the profile's rules: primary and secondary text
  4.5 : 1 and tertiary 3 : 1 against surfaces 0 to 3; the text on the accent and on the container 4.5 : 1; text over
  an image 4.5 : 1 on the scrim over black and over white; the outline 3 : 1 against surfaces 1 to 3 in High and
  1.6 : 1 elsewhere; adjacent surfaces different (except OLED's black pair); status colours 3 : 1 against surfaces 1 and
  2. `ObservatoryValidation.EveryCombinationMeetsTheContrastRules` enumerates every combination a user can choose; a
  combination that fails is a test failure. Where one fails, a table value moves, never the rule.

### Density

The `Observatory` profile lays the interface out at one of two **densities**: `ThemeDensity {Compact, Comfortable}`,
`setThemeDensity()`, stored as `interface/density` (`compact`, `comfortable`; default `compact`) and read at start with the
other theme choices. A density is a column of `MetricTokens` (`tokens().metrics`), in logical pixels:

| Metric | Compact | Comfortable |
|---|---|---|
| `fontBody`, `fontSmall`, `fontCaption` | 13, 12, 11 | 14, 13, 12 |
| `control`, `controlSmall`, `controlXSmall`, `controlLarge` | 32, 28, 24, 40 | 36, 32, 26, 44 |
| `rowTable`, `rowDense`, `rowKeyValue`, `rowSettings`, `rowPalette` | 30, 28, 24, 38, 30 | 36, 32, 28, 42, 34 |
| `barTop` | 44 | 48 |
| `strip` (a tab strip, the history strip, a task's header) | 34 | 40 |
| `statusLine` | 26 | 28 |
| `rail` | 44 | 44 |
| `pad`, `gap` | 12, 8 | 14, 10 |
| `sectionHeader` | 32 | 36 |
| `chip`, `tag` | 24, 20 | 26, 22 |
| `floatingToolbar` | 40 | 44 |
| `scrollBar` | 8 | 8 |

- **The proxy style answers sizes from them.** Under `Observatory`, `AnProxyStyle` makes a push button, a field (a
  line edit with a frame), a combo box and a spin box `control` high, a button's text padded 14 at each side; a tool
  button and a push button with no text a `control` square; a tab `strip` high, or `controlSmall` in a tab bar with
  `anTabCompact` (a panel's tabs); a row of a table, a list or a tree, and a table's header, `rowTable` high (the
  vertical header's default section size included); a menu's item, and so a combo box's popup row, `rowDense` high;
  a scroll bar `scrollBar` thick. A control is never smaller than its contents need: a large icon grows it. A line
  edit with no frame is a spin box's or a combo box's text and keeps its own size. Every other answer is the
  `Classic` one; the drawing is not the density's.
- **The type scale follows the density.** The body and the third heading are `fontBody`, the caption `fontCaption`,
  the first two headings 24 and 17 in both, every heading semibold; the font is the platform's interface font, the one
  the `Classic` body is, with tabular figures; the monospace is the first of Cascadia Mono, Cascadia Code and Consolas
  the platform has, or its own monospace, at `fontBody`. Sizes are set as points at the 96 logical dots per inch Qt 6
  presents to an application, which is the same size, so that code that makes a font from another by its point size
  keeps working. The application's font is the body.
- **A density changed while windows are shown sizes them again.** `AnProxyStyle::refreshThemeTokens()` - which
  `ThemeManager::applyTheme()` calls - compares the sizes and the type scale it held with the new ones; when they
  moved, it makes the body the application's font and sends every widget the `StyleChange` event Qt sends when a style
  is set: a widget drops the size hint it keeps (a button, a combo box, a spin box, a tab bar, a header) and asks its
  layout to run again, and a shown window grows to hold its controls. The new font reaches only the widgets that take
  the application's; a widget given a font of its own, and what it holds, hears of the density through the style change
  alone. A top-level widget's `updateGeometry()` would do nothing: a window has no layout above it to ask. Under
  `Classic` the sizes never move, so none of it runs.
- **`Classic` keeps 1.1's sizes and ignores the density.** Its `metrics` are 1.1's where 1.1 has a size for the role -
  `control` 24, the least its style makes a field, a combo box, a spin box or a button, `scrollBar` 12, the font sizes
  of its type scale (`fontSmall` is its caption's) - and the Compact column where it has none, so a widget made for the
  `Observatory` profile is sized as designed if it is shown under `Classic`. The proxy style's `Classic` answers read
  none of them: `Density.ClassicMetricsAreUnchanged` holds them, under either density, to a record taken before the
  metrics existed (`src/AfterNightTests/Test_ThemeSystem/data/ClassicMetrics.txt`), edited by the change that means to
  move an entry. The density setting is offered only under `Observatory`.
- **A new widget sizes itself from `tokens().metrics`** in its `sizeHint()`, read there and never kept from a
  constructor, and is checked in both densities ([Checklist for New Controls](#checklist-for-new-controls)).

### Presets

A **colour preset** (`ThemePreset`) names an accent, a surface tint and a tint strength chosen together; the theme and
the contrast profile stay the user's:

| Preset | Setting | Accent | Tint | Strength |
|---|---|---|---|---|
| Slate Blue (the default) | `slate` | Blue | Neutral | Balanced |
| Observatory | `observatory` | Amber | Ocean | Strong |
| Deep Sea | `deepsea` | Teal | Ocean | Balanced |
| Graphite | `graphite` | Amber | Neutral | Balanced |
| Nebula | `nebula` | Rose | Amethyst | Balanced |
| Aurora | `aurora` | Teal | Mint | Subtle |

- **A preset is a name for its three parts.** `applyThemePreset()` puts them in force with one resolution;
  `themePreset()` is the preset the parts in force make, `themePresetOf()` the one any three parts make, and a
  combination no preset names is `Custom`. So a part changed by hand leaves the preset, and nothing has to remember that
  it did: there is one source of truth, the parts.
- **The setting `interface/themePreset`** is what the Interface page last saved. Read at start, a preset it names puts
  its parts in force, so a preset whose parts change in a later version brings its users the new ones; `custom`, or no
  setting at all, leaves the parts stored as they are. The settings of AfterNight 1.1 hold an accent and a tint and no
  strength (Balanced): they are the preset they make, or `Custom`.
- **The presets are the `Observatory` profile's.** Under `Classic` the page offers none and writes no preset, strength,
  density or Night setting, so a 1.1 user's settings file stays as 1.1 wrote it.

### Night

**Night** keeps the eyes dark-adapted: everything on screen is red. `setNightMode()` sets it; `isNightMode()` says
whether it is in force, which it is under `Observatory` alone (the `Classic` profile has no Night, as it has no strength
or density). Night is never kept with the other choices: it is off at every start unless `interface/startInNight` asks
to start in it. It is three things, and none of them is an effect laid over the window:

- **The tokens' last step.** `ThemeChoices::night` makes the resolver resolve the dark theme whatever the preference,
  with the tint off whatever the tint and its strength - a hue cannot be seen in red, and a tinted surface would leave
  red text under its contrast - and then give Night's tones: the primary text white, the secondary and tertiary text
  raised until they reach 3 : 1 against the second surface once in red (they may meet), the accent's fill its 300 step
  under black text. Then every colour - every role, surface and material - is mapped by `nightColor()` to
  `(Y, 0, 0)` with `Y = 0.299 R + 0.587 G + 0.114 B` on the 8-bit components, its alpha kept. Red carries a fifth of
  white's luminance, so the rules are Night's own: the primary text 4.5 : 1 against surfaces 0 to 3, the secondary and
  tertiary text and the text on the accent 3 : 1, the status colours 2 : 1 against surfaces 1 and 2 - which is why a
  status never goes without its icon. `Night.*` (`Test_ThemeSystem`) holds every combination to them, and to having no
  green or blue. The user's tint and strength are kept, and return when Night goes; `isDarkTheme()` answers dark while
  it lasts.
- **Every picture in its Night form, made off the GUI thread.** The viewer maps its presented bytes as the
  presentation's last stage, on the worker that presents (`ViewPresentationSettings::night`); the mask layer is mapped
  with it, once. Any other picture goes through `nightImage()` (`theme/NightImage.h`) - or a `NightPicture`, or a
  `NightPictureLabel`, which hold one - which keeps the form per picture and makes it on the thread pool when the GUI
  thread asks: the widget paints the picture's place empty until the form is kept, and is told to paint again then. A
  picture is never mapped on the GUI thread; a colour no token holds - a user's choice, a fixed colour of a plot - is
  drawn through `colorAsShown()`. Switching Night reaches a widget as a change of the application's palette
  (`QEvent::PaletteChange`), which is where a widget that shows a picture asks for it again. The icon manager loads its
  coloured icons in their Night form, as it tints the others.
- **Where hue told things apart, shape does.** The histogram draws its curves in the plot's colours, which Night makes
  red, and strokes them red solid, green dashed, blue dotted (`HistogramWidget::channelStroke`).

**A widget that paints a picture is on the list.** `theme/NightImageWidgets.h` names every widget of Controls that
draws an image or a pixmap, sets a label's pixmap or hands a picture to a `NightPicture`, and says how it shows the
picture under Night: a presentation's Night stage, a Night picture, a picture its owner hands it already in Night's
form, an icon of the icon manager, colours Night maps, or never (a file written, not shown).
`NightImage.EveryWidgetThatPaintsAPictureIsOnTheList` fails a widget that paints a picture and is not listed.

### Accessing Tokens

```cpp
#include "theme/AnDesignTokens.h"

// Current active theme tokens (dark or light depending on preference)
const auto& t = an::ui::tokens();
QColor bg = t.colors.bgSurface;
int pad = t.spacing.md;
int r = t.radius.sm;
QFont font = t.typography.body;

// Force a specific variant
const auto& dark = an::ui::resolvedDarkTokens();
const auto& light = an::ui::resolvedLightTokens();

// Semantic color API (for palette-agnostic code)
QColor successBg = an::ui::semanticColor(an::ui::SemanticColorRole::SuccessBackground);

// Query current theme state
bool isDark = an::ui::isDarkTheme();
```

### Thread Safety

Token access is thread-safe. The resolution pipeline uses atomic pointer swaps and old snapshots are kept alive in a history vector. Reading `tokens()` never blocks.

---

## Proxy Style (`AnProxyStyle`)

`AnProxyStyle` is a `QProxyStyle` subclass that sits on top of Fusion (Qt's cross-platform base style). It overrides **every significant paint path** so that all standard Qt widgets rendered in the application use design tokens instead of platform defaults.

### Installation

Called once at startup:
```cpp
an::ui::AnProxyStyle* style = an::ui::installAnProxyStyle();
```
Qt takes ownership of the style object. Access the currently installed instance:
```cpp
an::ui::AnProxyStyle* style = an::ui::installedAnProxyStyle();
```

The full extension host loads the same style through the `afternight` style plugin
(`QStyleFactory::create("afternight")`) and registers it with `registerInstalledAnProxyStyle()`,
or calls `installAnProxyStyle()` when the plugin is not found. The application installs the style
unless `AN_UI_TOKENS_ENABLED` is set false.

### What It Overrides

| Override | Widgets/Elements |
|---|---|
| `drawPrimitive()` | `PE_PanelLineEdit`, `PE_IndicatorCheckBox`, `PE_IndicatorRadioButton`, `PE_PanelItemViewItem`, `PE_PanelTipLabel`, `PE_FrameGroupBox`, `PE_Frame` (cards, banners, context messages, item views, generic `anFrameRadius` frames), `PE_FrameTabWidget`, `PE_FrameMenu`, `PE_PanelMenu`, `PE_PanelMenuBar`, `PE_PanelStatusBar`, `PE_PanelToolBar`, `PE_IndicatorToolBarSeparator`, `PE_PanelButtonCommand`, `PE_PanelButtonTool`, `PE_IndicatorTabClose` |
| `drawControl()` | `CE_PushButtonBevel`, `CE_PushButtonLabel`, `CE_TabBarTabShape`, `CE_TabBarTabLabel`, `CE_ProgressBar*`, `CE_HeaderSection`, `CE_HeaderLabel`, `CE_MenuItem`, `CE_MenuBarItem`, `CE_MenuBarEmptyArea`, `CE_ComboBoxLabel`, `CE_ToolButtonLabel`, `CE_Splitter`, `CE_DockWidgetTitle`, `CE_ItemViewItem` |
| `drawComplexControl()` | `CC_ComboBox`, `CC_SpinBox`, `CC_Slider`, `CC_ScrollBar`, `CC_GroupBox` |
| `sizeFromContents()` | `CT_PushButton`, `CT_LineEdit`, `CT_ComboBox`, `CT_SpinBox`, `CT_TabBarTab`; under the Observatory profile also `CT_ToolButton`, `CT_HeaderSection`, `CT_ItemViewItem`, `CT_MenuItem`, all from the metric tokens ([Density](#density)) |
| `subControlRect()` | `CC_SpinBox`, `CC_ComboBox`, `CC_GroupBox` |
| `subElementRect()` | `SE_LineEditContents` |
| `pixelMetric()` | Margins, frame widths, scroll bar extent, slider dimensions, layout spacing, toolbar metrics, splitter width, indicator sizes; under the Observatory profile the scroll bar extent and a table's row height from the metric tokens |
| `styleHint()` | Tooltip delay, etch-disabled-text, item-view decoration |
| `standardIcon()` | Maps Qt standard pixmaps to themed SVG icons from `IconManager` |
| `polish()` | Sets `WA_Hover` on interactive widgets, applies palette colors to `QLabel`, configures `QDialogButtonBox` roles/icons, normalizes layout spacing to tokens, promotes process-shell frames |

### PainterGuard Pattern

All drawing methods use `an::ui::PainterGuard` (`theme/PainterGuard.h`). It calls `QPainter::save()` when
constructed and `restore()` when destroyed, ignores a null painter, and cannot be copied. Code outside the style
includes the same header.
```cpp
#include "theme/PainterGuard.h"

// Usage in every draw method:
void AnProxyStyle::drawSomething(...) const {
    const PainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    // ... paint using m_tokens->colors, m_tokens->radius, etc.
}
```

### State Helper Functions

```cpp
bool isHovered(const QStyleOption* opt);   // State_MouseOver && State_Enabled
bool isPressed(const QStyleOption* opt);   // State_Sunken && State_Enabled
bool isFocused(const QStyleOption* opt);   // State_HasFocus && State_Enabled
bool isEnabled(const QStyleOption* opt);   // State_Enabled
bool isSelected(const QStyleOption* opt);  // State_Selected
bool isChecked(const QStyleOption* opt);   // State_On
```

---

## KDDockWidgets Style (`KddwProxyStyle`)

A secondary `QProxyStyle` installed specifically on the KDDockWidgets main window. It intercepts paint calls for KDDW-specific widgets (title bars, dock tabs, separators, group frames, title-bar buttons) and renders them with design tokens.

```cpp
auto* kddwStyle = an::ui::installKddwProxyStyle(dockMainWindow);
// On theme change:
kddwStyle->refreshThemeTokens();
```

It uses `isKddwWidget()` ancestry checks to determine whether a widget belongs to the KDDW subsystem, and delegates everything else to the base proxy chain.

---

## Layout Utilities (`AnLayoutUtils`)

Header-only (`components/AnLayoutUtils.h`). Provides:

### `normalizeSpacingValueToTokens(int value, SpacingTokens)`
Maps hardcoded spacing values to the nearest design token:
```
4       → spacing.xs
6-9     → spacing.sm
10-13   → spacing.md
14-18   → spacing.lg
20-24   → spacing.xl
```

### `normalizeLayoutSpacingToTokens(QLayout*, SpacingTokens)`
Recursively walks a layout tree and normalizes all spacing and margins to design tokens. **Called automatically by `AnProxyStyle::polish()`** on every widget's root layout.

Layouts can opt out with dynamic properties:
- `anKeepLayoutSpacing` — skip spacing normalization
- `anKeepLayoutMargins` / `anKeepPreviewLayoutMargins` — skip margin normalization

### `reparentLayoutWidgets(QLayout*, QWidget*)`
Utility for reassigning widget parents when transferring layouts.

---

## Animation Utilities (`AnimationUtils`)

Header-only global toggle for all UI animations (accessibility support):

```cpp
#include "theme/AnimationUtils.h"

// Gate every animation:
m_animation->setDuration(an::ui::AnimationUtils::duration(170));

// Check before complex animation setup:
if (!an::ui::AnimationUtils::animationsEnabled()) {
    applyInstantly();
    return;
}
```

Every animated component **must** use `AnimationUtils::duration()` instead of hardcoded durations. This ensures the global "Disable animations" preference is respected.

---

## Icon Management (`IconManager`)

SVG icons are loaded and colorized at startup per the current theme tokens:

```cpp
IconManager iconMgr;
iconMgr.setColors(tokens.colors.textPrimary, tokens.colors.textTertiary);
iconMgr.setSemanticColors(
    tokens.colors.statusInfo,
    tokens.colors.statusWarning,
    tokens.colors.statusError,
    /* question */ tokens.colors.textPrimary,
    tokens.colors.statusSuccess);
iconMgr.loadIcons(":/AfterNight/icons/SVG/");
```

Icons named `info`, `warning`, `priority_high`, `help` automatically receive their semantic color. All other icons get the normal text color.

The `AnProxyStyle::standardIcon()` override maps Qt's `SP_*` standard pixmaps to icons from `IconManager`, so system dialogs (file dialogs, message boxes) display themed icons.

---

## Semantic Color Bridge (`UiStyleTokens`)

`app/UiStyleTokens.h` provides palette-based utilities for code that doesn't directly include theme headers:

```cpp
#include "app/UiStyleTokens.h"

// Determine theme variant from a widget's palette
bool dark = an::ui::styling::isDarkPalette(widget->palette());

// Get a semantic color against a specific palette
QColor bg = an::ui::styling::semanticColor(widget->palette(),
    an::ui::SemanticColorRole::SuccessBackground);
```

---

## Reusable Components (`components/`)

The `components/` directory contains pre-themed composite widgets that follow all theme rules. **Prefer reusing these** over creating one-off widgets:

| Component | Purpose |
|---|---|
| `AnButtonGroup` | Grouped exclusive/non-exclusive button set |
| `AnCardWidget` | Elevated card with rounded corners, drop shadow, border tint |
| `AnCollapsibleSection` | Expandable/collapsible section with smooth animation |
| `AnElidedLabel` | Label of a set number of lines: wraps its text, cuts it with an ellipsis past the last line and puts the whole text in its tooltip, so what lies around it never moves |
| `AnInfoBanner` | Status-toned info/warning/error banner |
| `AnInputField` | Labeled input with validation styling |
| `AnProgressCard` | Card with embedded progress bar and status |
| `AnSectionHeader` | Section heading with optional action widgets |
| `AnSliderControl` | Labeled slider with value display |
| `AnStatusBadge` | Colored status indicator chip |
| `AnStyledTable` | Pre-themed `QTableWidget`: row selection, alternating rows, token palette and header font; `refreshTheme()` re-applies them |

All components use `paintEvent()` with design tokens for custom drawing, and standard Qt widgets (via proxy style) for embedded controls.

---

## ABSOLUTE RULES — What Is Forbidden

### 1. QSS / `setStyleSheet()` Is FORBIDDEN

**Never use `setStyleSheet()`, `QWidget::setStyleSheet()`, or any form of Qt Style Sheets (QSS).**

Reasons:
- QSS is parsed and selector-matched at every paint event — significant performance cost.
- QSS overrides `QProxyStyle` rendering unpredictably, breaking the token-driven pipeline.
- QSS strings are opaque to the compiler, lose type safety, and are impossible to validate at build time.
- QSS does not participate in the token resolution pipeline (contrast profiles, accent families, surface tints).

**No code under `src/` calls `setStyleSheet()`: `SourceGuards.NoStyleSheetsInSrc` in `Test_ImageIntegration` reads the product's sources for a stylesheet set or read, the stylesheet style and a `.qss` file, and the process-window tests assert that a window and its children carry no style sheet (`expectNoStyleSheet` in `src/AfterNightTests/Test_ProcessFramework/ProcessWindowCaseSupport.h`). Meet a styling need with an `AnProxyStyle` override or a custom `paintEvent()`.**

### 2. No Hardcoded Colors

**Never write literal color values** (hex, RGB, or named constants) in widget code. Always reference design tokens:

```cpp
// ❌ WRONG
painter->setPen(QColor(96, 165, 250));
label->setStyleSheet("color: #60A5FA;");

// ✅ CORRECT
const auto& c = an::ui::tokens().colors;
painter->setPen(c.accentPrimary);
```

### 3. No Hardcoded Spacing or Radii

**Always use `SpacingTokens` and `RadiusTokens`:**

```cpp
// ❌ WRONG
layout->setContentsMargins(12, 12, 12, 12);
painter->drawRoundedRect(rect, 4, 4);

// ✅ CORRECT
const auto& t = an::ui::tokens();
layout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);
painter->drawRoundedRect(rect, t.radius.sm, t.radius.sm);
```

### 4. No Direct Font Construction for UI Text

**Use `TypographyTokens`** for all fonts:

```cpp
// ❌ WRONG
label->setFont(QFont("Arial", 14, QFont::Bold));

// ✅ CORRECT
label->setFont(an::ui::tokens().typography.heading2);
```

### 5. No Animations Without `AnimationUtils`

```cpp
// ❌ WRONG
animation->setDuration(200);

// ✅ CORRECT
animation->setDuration(an::ui::AnimationUtils::duration(200));
```

---

## How to Style a New Widget or Control

### Decision: AnProxyStyle Override vs. Custom `paintEvent()`

| Use `AnProxyStyle` override | Use custom `paintEvent()` |
|---|---|
| Standard Qt widget (QPushButton, QComboBox, etc.) | Composite widget with unique visual structure |
| The widget already has a `PrimitiveElement`, `ControlElement`, or `ComplexControl` enum | No existing Qt style element matches the widget |
| You want Fusion fallback for states you don't handle | Full control over every pixel is needed |
| The widget appears in many places with the same look | The widget is a one-of-a-kind composite |

### Path A: Adding to AnProxyStyle

1. **Identify the correct override point** — is it a primitive (`drawPrimitive`), a control (`drawControl`), a complex control (`drawComplexControl`), or a size/metric override?

2. **Add a private helper method** to `AnProxyStyle`:
   ```cpp
   // In AnProxyStyle.h:
   void drawMyWidget(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
   ```

3. **Implement the helper** using the established pattern:
   ```cpp
   void AnProxyStyle::drawMyWidget(const QStyleOption* option,
                                    QPainter* painter,
                                    const QWidget* widget) const
   {
       const PainterGuard guard(painter);
       painter->setRenderHint(QPainter::Antialiasing, true);

       const auto& c = m_tokens->colors;
       const auto r = m_tokens->radius.sm;

       // Determine state
       const bool enabled = isEnabled(option);
       const bool hovered = isHovered(option);
       const bool pressed = isPressed(option);

       // Compute colors based on state
       QColor bgColor = enabled ? c.bgCard : c.bgSurface;
       QColor borderColor = hovered ? c.borderFocus : c.borderSubtle;

       // Draw
       painter->setPen(QPen(borderColor, 1));
       painter->setBrush(bgColor);
       const QRectF rect = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
       painter->drawRoundedRect(rect, r, r);
   }
   ```

4. **Wire the dispatch** in the appropriate switch statement:
   ```cpp
   case PE_MyElement:
       drawMyWidget(option, painter, widget);
       return;
   ```

5. **Add `polish()` setup** if the widget needs hover tracking or palette adjustments.

### Path B: Using Dynamic Properties for Variants

When the same widget type needs different visual treatments, use Qt dynamic properties instead of subclassing:

```cpp
// In widget setup code:
button->setProperty("anButtonVariant", "primary");
button->setProperty("anProcessButtonRole", "danger");
frame->setProperty("anFrameRadius", tokens().radius.md);
progressBar->setProperty("anUsageTone", "high");
label->setProperty("anStatusTone", "success");
```

The proxy style checks these properties:
```cpp
const bool isPrimary = hasProperty(widget, "anButtonVariant", "primary");
```

Naming convention for custom properties: **`an` prefix + PascalCase descriptor** (e.g., `anStatusTone`, `anFrameRadius`, `anSplitterVariant`).

---

## Custom Widget with `paintEvent()` — Step by Step

Use this approach for widgets that don't map to standard Qt style elements.

### Template

```cpp
#pragma once

#include <QWidget>

#include <verdigris/wobjectdefs.h>

namespace an::ui {

struct DesignTokens;

class MyCustomWidget : public QWidget
{
    W_OBJECT(MyCustomWidget)

public:
    explicit MyCustomWidget(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    // No m_tokens pointer needed — call an::ui::tokens() directly
};

} // namespace an::ui
```

```cpp
#include "stdafx.hpp"
#include "MyCustomWidget.h"

#include "theme/AnDesignTokens.h"

#include <QPainter>
#include <QPaintEvent>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(an::ui::MyCustomWidget)

namespace an::ui {

MyCustomWidget::MyCustomWidget(QWidget* parent)
    : QWidget(parent)
{
    // Enable styled background if needed
    setAttribute(Qt::WA_StyledBackground, true);

    // Use size policy for responsive layouts
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void MyCustomWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const auto& t = an::ui::tokens();
    const auto& c = t.colors;

    // Background
    const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(c.borderPrimary, 1));
    painter.setBrush(c.bgCard);
    painter.drawRoundedRect(bounds, t.radius.md, t.radius.md);

    // Text
    painter.setPen(c.textPrimary);
    painter.setFont(t.typography.body);
    painter.drawText(bounds.adjusted(t.spacing.md, t.spacing.sm, -t.spacing.md, -t.spacing.sm),
                     Qt::AlignVCenter | Qt::AlignLeft, tr("My Widget"));
}

} // namespace an::ui
```

### Key Practices

1. **Always call `an::ui::tokens()`** inside `paintEvent()` (not cached in constructor) so the widget automatically picks up theme changes.
2. **Use `QRectF` adjusted by 0.5px** for anti-aliased borders so pen strokes don't exceed widget bounds.
3. **Enable `Qt::WA_StyledBackground`** if you want the widget to participate in palette-based background filling.
4. **Handle disabled state:** Check `isEnabled()` and use `textTertiary` / `bgSurface` colors accordingly.
5. **Respect surface tints** for card-like backgrounds:
   ```cpp
   QColor bg = c.bgCard;
   if (c.cardSurfaceTint.isValid()) {
       const bool dark = an::ui::isDarkTheme();
       bg = blendRgb(bg, c.cardSurfaceTint, dark ? 0.52 : 0.40);
       if (dark) bg = blendRgb(bg, QColor(0, 0, 0), 0.22);
   }
   ```

---

## Dynamic Properties for Variant Styling

The proxy style uses dynamic properties to differentiate visual variants without subclassing:

### Button Properties

| Property | Values | Effect |
|---|---|---|
| `buttonVariant` | `"primary"` | Accent-colored button |
| `anButtonVariant` | `"primary"` | Same as above (preferred prefix) |
| `anProcessButtonRole` | `"primary"`, `"secondary"`, `"neutral"`, `"danger"` | Dialog/process button roles (set automatically on `QDialogButtonBox`) |

### Frame / Card Properties

| Property | Values | Effect |
|---|---|---|
| `anFrameRadius` | integer (e.g., `8`) | Generic rounded card via `PE_Frame` |
| `anFrameBorderDashed` | `true` | Dashed border style |

### Label Properties

| Property | Values | Effect |
|---|---|---|
| `anStatusTone` | `"success"`, `"warning"`, `"error"`, `"secondary"` | Semantic text color + DemiBold |

### ProgressBar Properties

| Property | Values | Effect |
|---|---|---|
| `anUsageTone` | `"high"`, `"medium"`, `"low"` | Chunk color: error/warning/success |

### Splitter Properties

| Property | Values | Effect |
|---|---|---|
| `anSplitterVariant` | `"process"` | Wider handle (8px) with groove indicator |

### Tab Properties

| Property | Values | Effect |
|---|---|---|
| `anTabCompact` | `true` | Reduced vertical padding |
| `anTabHeightScale` | `double` (e.g., `0.8`) | Scale tab height |
| `anTabMinWidth` | integer | Minimum tab width |
| `anTabMinHeight` | integer | Minimum tab height |
| `anTabAccentLinePosition` | `"top"` | Place accent line on top instead of bottom |
| `forceUnselected` | `true` | Visually de-select all tabs (KDDW) |

### Toolbar Properties

| Property | Values | Effect |
|---|---|---|
| `anUseCardBackground` | `true` | Use acrylic surface + tint instead of solid |

### Context Message Properties

| Property | Values | Effect |
|---|---|---|
| `anContextMessageTone` | `"info"`, `"warning"`, `"error"` | Border color tone |

### Banner, Group Box and Combo Box Properties

| Property | Values | Effect |
|---|---|---|
| `anProcessMessageTone` | `"info"`, `"warning"`, `"error"` | Border tone of an `anProcessShellBanner` |
| `anGroupVariant` | `"flat"` | Flat (process-shell) group box, as `QGroupBox::isFlat()` |
| `anComboPopupAnchored` | `"true"` | Drop-down list opens below the combo box (`SH_ComboBox_Popup` = 0) |

### Layout Opt-Out Properties

| Property | Values | Effect |
|---|---|---|
| `anKeepLayoutSpacing` | `true` | Skip spacing normalization |
| `anKeepLayoutMargins` | `true` | Skip margin normalization |
| `anKeepPreviewLayoutMargins` | `true` | Skip margin normalization (preview layouts) |

### Named Object Identifiers

Some widgets are identified by `objectName()` for specific styling:

| Object Name | Applied To | Effect |
|---|---|---|
| `anProcessShellCard` | `QFrame` | Elevated card with surface tint, rounded `radius.lg` |
| `anProcessShellBanner` | `QWidget`/`QFrame` | Status-toned banner |
| `anProcessShellHeaderTitle` | `QLabel` | `heading1` font |
| `anProcessShellHeaderDescription` | `QLabel` | `textSecondary` color, `body` font |
| `anProcessShellBannerMessage` | `QLabel` | `body` font |
| `anContextMessage` | `QFrame` | Generic semantically-toned info card |
| `anMessageBox` | `QMessageBox` | Styled message box variant |

---

## Theme Switching & Refresh Flow

```
Settings applies contrast/accent/tint (set*()) and the theme
       ▼
ThemeManager::applyTheme(themeId)              (app/ThemeManager.cpp)
  ├─ setThemePreference(...) → refreshTokens() → publishResolvedTokenSnapshot()
  ├─ installedAnProxyStyle()->refreshThemeTokens()
  │     refreshTokens(); re-reads tokens(); app palette via setupPalette(); polish()+update() every widget;
  │     when the sizes or the type scale moved (a density): the body as the app font, StyleChange to every widget
  ├─ installedKddwProxyStyle()->refreshThemeTokens()
  │     re-reads tokens(); dock-window palette; polish()+update() its children
  ├─ clears the KDDW view factory's icon cache
  ├─ loadIcons(): IconManager recoloured from the new tokens; action icons reset
  └─ refreshCascade(): dashboard, image views, dock tabs/stacks, open processes; native backdrop
```

The same flow applies for accent family, contrast profile, surface tint, tint strength and density changes. Each `set*()` function calls `refreshTokens()` internally.

### Environment Variable

Set `AN_THEME_TOKEN_DEBUG_DUMP=1` to log the resolved token values each time the tokens are resolved, once the logger is initialized, in any build configuration.

Set `AFTERNIGHT_STYLE_PROFILE=observatory` to start the chosen shell under the Observatory profile (see
[Profiles](#profiles)): a developer aid.

---

## Responsiveness & Resolution Policy

### Target Resolutions

| Resolution | Status | Priority |
|---|---|---|
| **1080p** (1920×1080) | **Must work perfectly** | Primary target |
| **1440p** (2560×1440) | Should work well | Secondary |
| **4K** (3840×2160) with scaling | Should work well | Secondary |
| **720p** (1280×720) | **Best effort** — must remain usable | Tertiary |

### Principles

1. **Use Qt layout managers exclusively.** Never use absolute positioning (`setGeometry()`, `move()`) for layout. Use `QVBoxLayout`, `QHBoxLayout`, `QGridLayout`, `QFormLayout`, and `QStackedLayout`.

2. **Set appropriate size policies.** Every custom widget must declare a meaningful `QSizePolicy`:
   ```cpp
   setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
   ```

3. **Use `minimumSize()` / `minimumSizeHint()` conservatively.** Keep minimum sizes small enough that the UI remains usable at 720p. Prefer `setMinimumWidth()/setMinimumHeight()` over `setFixedSize()`.

4. **Avoid fixed sizes.** Use `setMinimumSize()` and `setMaximumSize()` to constrain rather than `setFixedSize()`. Fixed-size widgets break responsiveness.

5. **Use stretch factors** in layouts for proportional resizing:
   ```cpp
   layout->addWidget(sidebar, 1);     // 25% of space
   layout->addWidget(content, 3);     // 75% of space
   ```

6. **Use `QSplitter`** for user-resizable panel divisions. Set minimum sizes on child widgets to prevent collapse below usable dimensions.

7. **Elide text** when space is constrained:
   ```cpp
   const QString elided = fontMetrics().elidedText(text, Qt::ElideMiddle,
       (std::max)(0, availableWidth));
   ```

8. **Use scroll areas** for content that may overflow at low resolutions:
   ```cpp
   auto* scroll = new QScrollArea(this);
   scroll->setWidgetResizable(true);
   scroll->setWidget(contentWidget);
   ```

9. **Spacing tokens are your friend.** The token values (4/8/12/16/24px) are designed to work at 1080p. At 720p, the automatic layout normalization keeps spacing consistent and compact.

10. **Test at 1080p AND 720p.** Before submitting UI changes, resize the window to approximately 1280×720 and verify everything is still usable — controls reachable, text readable, no overlapping elements.

### Size Guidelines for 720p Compatibility

| Element | Maximum recommended minimum size |
|---|---|
| Dialogs | 640×400 (avoid larger minimums) |
| Sidebar panels | 200px minimum width |
| Toolbar height | 32-36px |
| Button minimum width | 70px (standard), 90px (dialog) |
| Input field minimum height | 24px |
| Tab bar tab | No fixed minimum (use `sizeFromContents`) |

### What NOT to Do

```cpp
// ❌ WRONG: Fixed size breaks responsive layout
widget->setFixedSize(400, 300);

// ❌ WRONG: Absolute positioning
widget->move(100, 200);
widget->setGeometry(50, 50, 300, 200);

// ❌ WRONG: Large minimum sizes that break at 720p
dialog->setMinimumSize(1200, 800);

// ✅ CORRECT: Flexible sizing
widget->setMinimumSize(200, 150);
widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
```

---

## Spacing, Radius, and Typography Token Reference

### Spacing Usage Guide

| Token | Value | When to Use |
|---|---|---|
| `xs` (4px) | Tight gaps: indicator-to-label, checkbox-to-text, inner padding |
| `sm` (8px) | Default spacing between controls, horizontal padding in inputs, layout element spacing |
| `md` (12px) | Layout margins, card internal padding, section separations |
| `lg` (16px) | Tab horizontal padding, section headers, group box title horizontal offset |
| `xl` (24px) | Dialog margins, large panel separations |

### Radius Usage Guide

| Token | Value | When to Use |
|---|---|---|
| `none` (0px) | Never use for interactive controls |
| `sm` (4px) | Buttons, inputs, combo boxes, spin boxes, tabs, menus, line edits, tooltips, item views |
| `md` (8px) | Group boxes, cards, banners |
| `lg` (12px) | Process shell cards, elevated panels |
| `xl` (14px) | Large overlay panels, prominent floating UI |

Under the Observatory profile: `sm` 6 for tooltips, palette rows and other small parts, `md` 8 for controls, `lg` 12
for cards and popups, `xl` 16 for sheets.

### Typography Usage Guide

| Token | When to Use |
|---|---|
| `heading1` | Page/process titles |
| `heading2` | Section headings |
| `heading3` | Subsection headings, header views |
| `body` | All body text, default application font |
| `caption` | Secondary info, timestamps, footnotes |
| `monospace` | Code, paths, technical values |

---

## Color Token Reference

### State-Based Color Selection

The proxy style follows consistent patterns for widget states. Follow these when implementing new controls:

| State | Background | Border | Text |
|---|---|---|---|
| **Normal** | `bgCard` / `bgInput` | `borderPrimary` / `borderSubtle` | `textPrimary` |
| **Hover** | `bgCard` (border-only highlight) | `textAccent` | `textPrimary` |
| **Pressed** | `bgCardSecondary` | `borderPrimary` | `textPrimary` |
| **Focused** | (unchanged) | `borderFocus` | `textPrimary` |
| **Disabled** | `bgSurface` / `bgCard` | `borderSubtle` | `textTertiary` |
| **Selected** | `accentPrimary` (alpha-blended) | `borderFocus` | `textAccent` |

### Primary (Accent) Button States

| State | Background | Border | Text |
|---|---|---|---|
| Normal | `accentPrimary` | `accentPrimary` | `textOnAccent` |
| Hover | `accentHover` | `accentHover` | `textOnAccent` |
| Pressed | `accentPressed` | `accentPressed` | `textOnAccent` |
| Disabled | Computed blend | Computed blend | Best-contrast auto |

### Danger Button States

| State | Background | Border | Text |
|---|---|---|---|
| Normal | `statusError` | `statusError` | Auto (white/black by contrast) |
| Hover | `statusError.lighter(112)` | Same | Auto |
| Pressed | `statusError.darker(112)` | Same | Auto |
| Disabled | Computed blend | Computed blend | Best-contrast auto |

---

## Contrast Validation & Accessibility

### WCAG Compliance

In debug builds, the token resolution pipeline validates contrast ratios against WCAG 2.1 guidelines:

| Pair | Minimum Ratio |
|---|---|
| `textPrimary` on `bgSurface` | 4.5:1 |
| `textPrimary` on `bgCardPrimary` | 4.5:1 |
| `textSecondary` on `bgSurface` | 3.0:1 |
| `textSecondary` on `bgCardPrimary` | 3.0:1 |
| `textAccent` on `bgSurface` | 3.0:1 |
| `textOnAccent` on `accentPrimary` | 4.5:1 |
| Status text on status background (all 4) | 3.0:1 |

**`textOnAccent` is auto-computed** — white when white reaches 4.5:1 against `accentPrimary`, otherwise whichever of white and black contrasts more.

### Best-Contrast Text Helper

For situations where you need to pick the best text color against an arbitrary background, use `an::ui::bestContrastTextColor`, exported from `theme/AnDesignTokens.h` with `compositeOver` and `contrastRatio`:

```cpp
QColor textColor = bestContrastTextColor(
    background,
    preferred,     // try this first
    fallbackA,     // if preferred fails minimum ratio
    fallbackB,     // if fallbackA also fails
    minimumRatio   // default 3.0
);
```

---

## Checklist for New Controls

Before submitting a new control or widget, verify:

- [ ] **No `setStyleSheet()` calls anywhere in the implementation**
- [ ] **All colors from `an::ui::tokens().colors`** — no hardcoded hex/RGB values
- [ ] **All spacing from `an::ui::tokens().spacing`** — no hardcoded pixel values (except for sub-pixel antialiasing adjustments like `0.5`)
- [ ] **All radii from `an::ui::tokens().radius`**
- [ ] **All control, row and bar sizes from `an::ui::tokens().metrics`**, read in `sizeHint()` — never kept from a constructor
- [ ] **Both densities tested** — under the Observatory profile, Compact and Comfortable each size the widget from their metrics
- [ ] **All fonts from `an::ui::tokens().typography`** — no `QFont` constructed with literal family/size
- [ ] **Animations gated through `AnimationUtils::duration()`**
- [ ] **Disabled state handled** — uses `textTertiary`, `bgSurface`, `borderSubtle`
- [ ] **Hover state handled** (if interactive) — `WA_Hover` enabled
- [ ] **Focus state handled** (if focusable) — uses `borderFocus`
- [ ] **`QSizePolicy` set appropriately** — no `setFixedSize()` without strong justification
- [ ] **Tested at 1080p resolution** — fully functional
- [ ] **Tested at 720p resolution** — remains usable (no overflow, no inaccessible controls)
- [ ] **Theme switch tested** — widget updates correctly when dark↔light is toggled
- [ ] **Surface tint tested** — card backgrounds reflect the active tint family (if applicable)
- [ ] **Accent family tested** — accent-colored elements reflect the active accent (if applicable)
- [ ] **Contrast profiles tested** — widget remains readable in High and Soft profiles
- [ ] **Night tested** — under the Observatory profile with Night on, the widget paints no green or blue; a widget that paints a picture shows its Night form (`nightImage`, `NightPicture`, `NightPictureLabel`) and is on `theme/NightImageWidgets.h`, and a colour no token holds goes through `colorAsShown()`
- [ ] **`PainterGuard` used** for all `QPainter::save()`/`restore()` cycles
- [ ] **`QPainter::Antialiasing` enabled** for all rounded shapes

---

## Appendix: Helper Utilities Available in AnProxyStyle

These helpers are file-local to `AnProxyStyle.cpp` and can be reused when adding new draw methods; outside the style use the exported `compositeOver`, `contrastRatio` and `bestContrastTextColor` from `theme/AnDesignTokens.h`:

| Function | Purpose |
|---|---|
| `roundedRectPath(rect, radius)` | Creates a `QPainterPath` for a rounded rect |
| `blendRgb(base, tint, ratio)` | Linear RGB blend (preserves base alpha) |
| `compositeOver(foreground, background)` | Alpha-composite foreground over background |
| `contrastRatio(a, b)` | WCAG relative luminance contrast ratio |
| `bestContrastTextColor(bg, preferred, fallbackA, fallbackB, minRatio)` | Pick best contrast text |
| `stripMnemonicMarkers(text)` | Remove `&` mnemonic markers from label text |
| `hasProperty(widget, name, value)` | Check dynamic property string equality |
| `hasParentProperty(widget, name, value)` | Walk ancestors checking dynamic property |
| `intPropertyOrDefault(widget, name, default)` | Read integer property with fallback |
| `colorizePixmap(source, color)` | Recolor a pixmap using `CompositionMode_SourceIn` |
| `iconFromManager(iconName)` | Get a themed icon through `appContext()->getIconProvider()` |
| `isHovered/isPressed/isFocused/isEnabled/isSelected/isChecked` | State flag checks |
