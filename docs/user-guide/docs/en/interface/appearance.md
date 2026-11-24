---
kind: interface
summary: >-
  How AfterNight looks: a colour preset, the theme and its contrast, the accent, the surface tint and its strength,
  the density, Night for dark-adapted eyes, the animations and the menu bar.
menu:
  - View > Night
  - View > Density
---
# Appearance

The appearance is how the main window and every dialog look: their colours, how large their bars and controls are, and
Night, which turns everything red to keep your eyes adapted to the dark. It changes no image and nothing you save. Its
choices are the **Colours and Size** group at the top of the Interface page of [Settings](settings-dialog.md): choose
**Settings** at the foot of the [rail](../reference/glossary.md#rail), **Edit > Settings...** or ++ctrl+comma++, then **Interface**. The
[command palette](command-palette.md) finds each row by its name, such as `Interface > Density`.

Each choice shows at once, in every window, as you make it. **Apply** or **OK** keeps it, and **Cancel** puts back what
was in force when the page opened.

```figure
image: ../assets/images/interface/appearance.webp
alt: >-
  The Settings dialog on its Interface page, Interface selected in the list at the left, under the title Colours and
  Size. Colour preset shows six tiles, each a small window in its colours: Slate Blue, framed as chosen, Observatory,
  Deep Sea, Graphite, Nebula and Aurora. Theme shows three tiles, Follow System half dark and half light, Light, and
  Dark, framed as chosen. Then rows of segments and swatches: Contrast with Soft, Balanced ticked, High and OLED; Accent
  with five coloured discs, the blue one ticked; Surface tint with six squares, the first ticked; Tint strength with
  Off, Subtle, Balanced ticked and Strong; Density with Compact ticked and Comfortable; and, cut by the page's foot,
  Night with a switch, off, reading Red light for dark adaptation.
caption: >-
  The Colours and Size group of the Interface page, at the defaults of a new profile.
pins:
  - {n: 1, x: 27.0, y: 21.3}
  - {n: 2, x: 27.0, y: 49.1}
  - {n: 3, x: 27.0, y: 62.8}
  - {n: 4, x: 27.0, y: 67.7}
  - {n: 5, x: 27.0, y: 72.7}
  - {n: 6, x: 27.0, y: 77.7}
  - {n: 7, x: 27.0, y: 82.7}
  - {n: 8, x: 27.0, y: 87.3}
```

## What you see

1. **Colour preset.** Six named sets of an accent, a surface tint and a tint strength, each tile a small window in
   its colours.
2. **Theme.** Dark, Light, or the choice of your system.
3. **Contrast.** How far the dark theme's text and surfaces stand apart.
4. **Accent.** The colour of buttons, selections and the other parts you act on.
5. **Surface tint.** A faint colour in the surfaces of the window, its cards and its lines.
6. **Tint strength.** How much of the surface tint goes into them.
7. **Density.** **Compact** or **Comfortable**: how large the text, the bars and the controls are.
8. **Night.** Red light for dark adaptation, and under it **Start in Night**. The page goes on with the animations and
   the menu bar.

## What you can do

### Choose a preset

Choose a tile. A preset sets the accent, the surface tint and the tint strength together, and leaves the theme and the
contrast as they are:

| Preset | Accent | Surface tint | Tint strength |
| --- | --- | --- | --- |
| Slate Blue | Blue | Neutral | Balanced |
| Observatory | Amber | Ocean | Strong |
| Deep Sea | Teal | Ocean | Balanced |
| Graphite | Amber | Neutral | Balanced |
| Nebula | Rose | Amethyst | Balanced |
| Aurora | Teal | Mint | Subtle |

Change one of the three by hand and the preset becomes **Custom**, a seventh tile that shows while the choices match no
preset.

### Choose the theme and its colours

**Follow System** takes the light or dark choice of your system and follows it. **Contrast** sets the dark theme:
**Soft** lowers contrast and saturation, **High** raises both, and **OLED** puts pure black behind everything. The
**Accent** discs and the **Surface tint** squares show each colour as the theme in force draws it. **Tint strength**
mixes the tint into the surfaces at 5 % for **Subtle**, 8 % for **Balanced** and 12 % for **Strong**, and into the lines
at 8, 12 and 18 %. The **Neutral** tint adds no colour at any strength.

### Change the density

Choose **View > Density > Compact** or **View > Density > Comfortable**, or the **Density** row of the page.
**Comfortable** sets the text a point larger and makes the bars, rows and controls taller, for a larger screen or for
eyes that need it. Windows already open are laid out again, and a window grows to hold its controls. A choice made in
the **View** menu is kept at once.

### Work in Night

Press ++ctrl+shift+n++, choose the moon in the top bar, or choose **View > Night**. Everything turns red, the images
and their thumbnails as well, so that your eyes stay adapted to the dark at the telescope. Night uses the dark theme
whatever the theme, and no tint. The histograms draw red solid, green dashed and blue dotted, since hue no longer tells
the channels apart. The open and save windows are AfterNight's own, which Night draws red, whatever **Use system file
browser** says. On Windows 11 the title bar turns dark as well. A picture is drawn red in the background, so its place
can stay empty for a moment.

Night is not kept: AfterNight starts out of it, unless **Start in Night** is on.

### Show the menu bar

The menu bar is hidden. Press and release ++alt++ to show it, or press ++alt++ with a menu's underlined letter to open
that menu; it hides again once its menus close. **Always show the menu bar** keeps it shown, at once. **All menus** in
the top bar holds the same menus. See [Top Bar and Menus](top-bar-and-menus.md).

### Choose a shell

A **Shell** row follows the group when more than one main window is installed, and takes effect when AfterNight
restarts. With the Observatory window alone, the row is hidden.

## Controls

```controls
window: Colours and Size
controls:
  - name: "Colour preset:"
    kind: radio
    default: Slate Blue
    options: [Slate Blue, Observatory, Deep Sea, Graphite, Nebula, Aurora, Custom]
    effect: Sets the accent, the surface tint and the tint strength to the preset's.
    enabled: "**Custom** shows only while the three choices match no preset, and cannot be chosen."
    persistence: Stored as `interface/themePreset`. A preset stored by name puts its parts in force at every start.
  - name: "Theme:"
    kind: radio
    default: Dark
    options: [Follow System, Light, Dark]
    effect: The colours of the windows. **Follow System** takes your system's light or dark choice.
    persistence: Stored as `interface/theme`.
  - name: "Contrast:"
    kind: radio
    default: Balanced
    options: [Soft, Balanced, High, OLED]
    effect: The contrast of the dark theme, as its tooltip says.
    enabled: >-
      **OLED** is greyed while **Theme:** is **Light**, and choosing **Light** while **OLED** is chosen returns to
      **Balanced**.
    persistence: Stored as `interface/themeContrastProfile`.
  - name: "Accent:"
    kind: radio
    default: Blue
    options: [Blue, Teal, Amber, Amethyst, Rose]
    effect: The colour of buttons, selections and the parts you act on.
    persistence: Stored as `interface/themeAccentFamily`.
  - name: "Surface tint:"
    kind: radio
    default: Neutral
    options: [Neutral, Ocean, Mint, Amethyst, Rose, Amber]
    effect: A faint colour in the window's surfaces and lines.
    persistence: Stored as `interface/themeSurfaceTintFamily`.
  - name: "Tint strength:"
    kind: radio
    default: Balanced
    options: [Off, Subtle, Balanced, Strong]
    effect: How much of the surface tint goes into the surfaces and the lines. **Off** leaves them neutral.
    persistence: Stored as `interface/themeTintStrength`.
  - name: "Density:"
    kind: radio
    default: Compact
    options: [Compact, Comfortable]
    effect: How large the text, the bars, the rows and the controls are.
    persistence: >-
      Stored as `interface/density`. **View > Density** stores it at once; the page stores it with **Apply** or **OK**.
  - name: Red light for dark adaptation
    kind: check
    default: "Off"
    effect: The **Night:** switch. Turns Night on or off, as ++ctrl+shift+n++ does.
    persistence: Never stored. **Reset Page** leaves it as it is.
  - name: Start in Night
    kind: check
    default: "Off"
    effect: On, AfterNight starts in Night.
    persistence: Stored as `interface/startInNight`, and read at the next start.
  - name: Enable UI animations
    kind: check
    default: "On"
    effect: >-
      The **Animations:** switch: small animations, such as the highlight of a card under the pointer and the opening of
      collapsible sections. Off removes them, for less motion or less work.
    persistence: Stored as `interface/animationsEnabled`.
  - name: Always show the menu bar
    kind: check
    default: "Off"
    effect: The **Menu bar:** switch. On, the menu bar stays shown. Off, it shows when ++alt++ is pressed and released.
    persistence: Stored as `interface/alwaysShowMenuBar`.
  - name: "Shell:"
    kind: combo
    default: Observatory
    options: [Observatory]
    effect: The main window AfterNight starts with, as its note says, "Takes effect when AfterNight restarts."
    enabled: Shown only while more than one shell is installed.
    persistence: Stored as `interface/shell`. The command line's `--shell=<id>` and `AFTERNIGHT_SHELL` choose before it.
```

## When it goes wrong

**OLED** cannot be chosen
:   **Cause.** **Theme:** is **Light**, and OLED is for the dark theme.
:   **Fix.** Choose **Dark** or **Follow System** first.

The colours went back when Settings closed
:   **Cause.** The dialog was closed with **Cancel**, which puts back what was in force.
:   **Fix.** Choose again, then **Apply** or **OK**.

AfterNight did not start in Night
:   **Cause.** Night is never kept from one session to the next.
:   **Fix.** Turn **Start in Night** on, and choose **OK**.

The preset reads **Custom**
:   **Cause.** An accent, a tint or a strength was changed by hand, and the three match no preset.
:   **Fix.** Choose the preset's tile again.

The surface tint shows no colour
:   **Cause.** **Tint strength:** is **Off**, the tint is **Neutral**, or Night is on.
:   **Fix.** Choose another tint and a strength, and leave Night.

The menu bar is gone
:   **Cause.** It is hidden until ++alt++ is pressed.
:   **Fix.** Press ++alt++, use **All menus** in the top bar, or turn **Always show the menu bar** on.

## Related

- [Settings](settings-dialog.md), for the rest of the Interface page and every other page.
- [Top Bar and Menus](top-bar-and-menus.md), for Night's button, **View > Density** and the menu bar.
- [The Main Window](overview.md), for the parts the density sizes.
- [Command Palette](command-palette.md), to reach a row of Settings by its name.
