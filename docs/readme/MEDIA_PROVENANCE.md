# README Media Provenance

The images of the repository's `README.md` that are not user-guide assets. Each window was captured on 2026-11-24 (Home,
`dashboard.webp`, again on 2026-11-27) from AfterNight 2.0.0 on Windows by
`build/scripts/tools/capture_guide_screenshots.ps1` - its recipes are in
[../user-guide/captures/readme.json](../user-guide/captures/readme.json) - in the dark theme, from a profile of its own
and the maintainer's own data, and is a project asset distributed under the repository licence. A window is drawn at 150
% and kept at most 1600 pixels wide; a picture of the image view alone is drawn at 100 %, where the view shows the
image's own pixels. The user-guide screenshots the README also shows are recorded in
[../user-guide/docs/en/assets/MEDIA_PROVENANCE.md](../user-guide/docs/en/assets/MEDIA_PROVENANCE.md).

No capture was retouched. One joins two captures, and one was cut from a capture; the table says which.

| Asset | Shows | Data and state | SHA-256 |
| --- | --- | --- | --- |
| `dashboard.webp` | Home with the two images opened last under the actions that begin a session | The linear colour master of the Carina Nebula and a master flat of the same set, each opened and closed in the session | `418f44e54f5d0e2fe010422878a2759bf93c73d01381b2a33879a334cd8dc4b4` |
| `stretch-before-after.jpg` | The preview panel of Statistical Stretch showing Original, and showing Preview | The linear colour master of the Carina Nebula integration (718 frames loaded, 714 stacked); the process at its default settings. Two captures of one task, each cut to the preview panel and set side by side | `5746267523b0daf08525e6c2cab6d5913dad5957c70bbe50cb5a8ba5ba6e0ae3` |
| `curves-live-preview.webp` | Curves Transformation with an S-curve and a preview area over the right half of the image | The same image after Histogram Transformation (shadows 0.02, midtones 0.02) applied in the session | `8bb54b129dd979989e384964b19be7b5a5dc8ecc537e901e1b829d7804c46f23` |
| `image-history.webp` | An image with three steps in its history strip | The same image after Histogram Transformation, Gamma Correction (0.8) and a curve, each applied in the session | `5f07ff75ba51081704bca1b87b0cd411bf38a5f05c46f0a055c2ad0a7cbf9487` |
| `astrometry-overlay.jpg` | The image view with the equatorial grid and catalog star labels | The same linear master shown with Autostretch; the plate solution is the one its file carries | `9e8ce8410be834cf2158c5a1f506e7f45ec8ee85f3e4fb7b7377502660f961ba` |
| `carina-nebula.jpg` | The processed image alone | Captured on 2026-11-06 from AfterNight 1.0.0: a band cut from a capture of the image view, fitted to a 2560 x 1440 display | `edd2cf7bff1fcc98379252a50b9118ab247ad9134e701155a57d4f19ad309253` |

Recapture an image when the window it shows changes, and replace its digest
(`Get-FileHash -Algorithm SHA256 docs/readme/<file>`). A capture shows only the maintainer's own images and file
names; it shows no path above a file's folder, no credential and no device serial.
