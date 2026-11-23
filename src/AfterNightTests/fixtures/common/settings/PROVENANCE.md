# A settings store written by AfterNight 1.1.0

`afternight-1.1.0.db` is the settings database AfterNight 1.1.0 wrote, byte for byte and kept so by `.gitattributes`.
It is the input of `SettingsCarryOver` in `Test_Persistence`, which opens a copy of it as this version opens a person's
store and holds that nothing a 1.1 user chose is lost. It cannot be captured again from this tree - no build here is
1.1.0 - so it is never regenerated: a case copies it and never writes it, and a store of another release is a file of
its own beside it.

## How it was written

On 2026-11-22, on Windows 11, by AfterNight 1.1.0 as its installer had installed it (its log's first line:
`AfterNight version 1.1.0 - engine 1.6.2`), started once over an empty storage root (`AFTERNIGHT_TEST_STORAGE_ROOT`),
with `LANG=en_US` and `TEMP` and `TMP` naming a folder outside any person's profile, so that the temporary folder the
store keeps names no one. Driven through the window's own controls by UI Automation:

1. the dashboard's Settings button, and the Interface page;
2. Theme: Light; Theme Contrast: High; Accent Family: Rose; Card Surface Tint: Amethyst;
3. "Lock dock layout by default" and "Hold Ctrl key to zoom with mouse wheel" ticked;
4. OK, and the main window closed, so the application kept what it keeps as it ends.

The rest is what 1.1.0 writes of its own as it starts and ends: its defaults for the General page, the dashboard's
order and schema version, the window's geometry, and two keys it stores with no value (`interface/dashboardHiddenActions`
and `interface/recentProcessIds`, written as SQL NULL).

## What it holds

The database states format 2 in its `migrations` table and is in SQLite's write-ahead mode, with nothing left in the
log: the one file is the whole store. Its SHA-256 is
`abfe73712135ac2c421a8449f3cd1a393c9b61a6421745222c9aabdb15398e5e`. `global_settings` holds 30 keys; `folder_tracker`,
`thumbnails` and `process_settings` are empty.

| Key | Value |
|---|---|
| `general/checkAppUpdatesOnStartup` | `0` |
| `general/checkExtensionUpdatesOnStartup` | `1` |
| `general/clearTempOnExit` | `1` |
| `general/confirmCloseUnsaved` | `1` |
| `general/defaultSaveFormat` | `tiff` |
| `general/language` | `system` |
| `general/recentFilesLimit` | `20` |
| `general/recentProcessesLimit` | `10` |
| `general/tempFilesLocation` | `E:/an-store-1.1.0/Temp/AfterNight` |
| `interface/animationsEnabled` | `1` |
| `interface/dashboardActionOrder` | six lines: `open_file`, `new_sequence`, `image_integration`, `extension_manager`, `clear_recents`, `system_info` |
| `interface/dashboardHiddenActions` | NULL |
| `interface/dashboardLastAction` | `Open settings` |
| `interface/dashboardRecentSortMode` | `recent` |
| `interface/dashboardSettingsSchemaVersion` | `4` |
| `interface/lockLayoutByDefault` | `1` |
| `interface/recentProcessIds` | NULL |
| `interface/rememberWindowSize` | `1` |
| `interface/theme` | `light` |
| `interface/themeAccentFamily` | `rose` |
| `interface/themeContrastProfile` | `high` |
| `interface/themeSurfaceTintFamily` | `amethyst` |
| `interface/useSystemFileBrowser` | `1` |
| `interface/windowGeometryHeight` | `950` |
| `interface/windowGeometryWidth` | `1267` |
| `interface/windowGeometryX` | `-1913` |
| `interface/windowGeometryY` | `221` |
| `interface/windowMaximized` | `1` |
| `interface/zoomRequiresCtrl` | `1` |
| `interface/zoomSensitivity` | `50` |

1.1.0 knew no colour preset, tint strength, density or shell, so none is stored: its accent and tint, Rose and
Amethyst, read with the Balanced strength an earlier version's settings are read with, are the Nebula preset.
