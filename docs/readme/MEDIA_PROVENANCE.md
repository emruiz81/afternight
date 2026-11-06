# README Media Provenance

The images of the repository's `README.md` that are not user-guide assets. Each was captured on 2026-11-06 from
AfterNight 1.0.0 on Windows, in the dark theme at 100% display scaling, from a profile of its own and the
maintainer's own data, and is a project asset distributed under the repository licence. The user-guide screenshots
the README also shows are recorded in
[../user-guide/docs/en/assets/MEDIA_PROVENANCE.md](../user-guide/docs/en/assets/MEDIA_PROVENANCE.md).

No capture was retouched. Two were cut from a capture, and one joins two captures; the table says which.

| Asset | Shows | Data and state | SHA-256 |
| --- | --- | --- | --- |
| `dashboard.png` | The Dashboard with seven recent files | Three finished PNG images (M 20, M 45, NGC 2070) and four display-stretched copies of stacked masters (Carina Nebula colour, luminance and red; M 83 luminance) written by `AfterNightCLI`. The copies stand in for the masters because a recent file's thumbnail is stretched only when the file records itself as linear, which these masters do not | `1274e23c1c68abd6cca6b95bf6b9a84b135359d2d7ca40d4897535401cbe2d28` |
| `stretch-before-after.jpg` | The preview panel of Statistical Stretch showing Original, and showing Preview | The linear colour master of the Carina Nebula integration (718 frames loaded, 714 stacked); the process at its default settings. Two captures of one window, each cut to the preview panel and set side by side | `8a7001ebff912641153c0f873c82669347051abb57f929796204239376899335` |
| `curves-live-preview.png` | Curves Transformation with Preview area over the right half of the image | The same image after Statistical Stretch at its defaults and SCNR (Average Neutral, amount 1), both applied in the session | `f2ab0236a87a946c2ff4ff3231afd8c5708115ddb2c604b198ee08bd599a3193` |
| `image-history.png` | Image History after the three applied steps | The same image after the curve was applied | `1eeb1697697add8b33bdd1db7f2f02524177aa2be4cb194b54b999d7d53267c2` |
| `astrometry-overlay.jpg` | The main window with the Equatorial Grid and Star Labels overlays | The same image; the plate solution is the one its file carries | `5b926ece443de871533988cf36aa9ec449fcd03dfb41eab2da3c6c18dd41a78a` |
| `carina-nebula.jpg` | The processed image alone | A band cut from a capture of the image view, fitted to a 2560 x 1440 display | `edd2cf7bff1fcc98379252a50b9118ab247ad9134e701155a57d4f19ad309253` |

Recapture an image when the window it shows changes, and replace its digest
(`Get-FileHash -Algorithm SHA256 docs/readme/<file>`). A capture shows only the maintainer's own images and file
names; it shows no path above a file's folder, no credential and no device serial.
