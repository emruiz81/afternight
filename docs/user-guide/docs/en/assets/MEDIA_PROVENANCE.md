---
kind: reference
summary: >-
  Where every image, diagram and script of the guide comes from, under which licence, and how to update it.
---
# Media Provenance

Where every file of the guide that is not a page comes from, under which licence, and how to update it. The pictures
of the application's windows are listed under [Captured windows](#captured-windows), by the group of their pages.
Every other file - the logo, the fonts, the scripts the pages load, the inline diagrams, and the few pictures taken
before the guide's capture script - is listed first, by its path, with its source, its licence, the day it was added,
its SHA-256, how to update it and, for a picture, the data it shows.

A file is listed here in the change that adds it. No third-party astrophotography image, capture of another
application or downloaded media is added unless its licence allows it to be distributed in the repository and the
installers.

## Other files

`assets/images/interface/logo.png`
:   **Source.** A copy of repository icon `icons/afternight_icon_128x128_blue.png`
:   **Licence.** Project asset, distributed with AfterNight under the repository license
:   **Added** 2026-06-10. **SHA-256** `5c585d19978c1bd3408fd2266e014107369caa569f128dcc63232f2ba3296b18`.
:   **To update.** Copy the matching repository icon again when the application icon changes. Every page loads the logo and the header draws it 30 px wide, so it is the 128 px icon and not a larger one.

`assets/images/interface/favicon.png`
:   **Source.** Derived from repository icon `icons/afternight_icon_128x128_blue.png`
:   **Licence.** Project asset, distributed with AfterNight under the repository license
:   **Added** 2026-06-10. **SHA-256** `5c585d19978c1bd3408fd2266e014107369caa569f128dcc63232f2ba3296b18`.
:   **To update.** Regenerate from the matching repository icon when the application icon changes.

`assets/images/interface/hero-screenshot.png`
:   **Source.** Captured from a fresh AfterNight profile by the project maintainer
:   **Licence.** Project asset, distributed with AfterNight under the repository license
:   **Added** 2026-06-12. **SHA-256** `169599c34f66fbdc655f2f0749bb440454a7302469279806232bea9fa6715d28`.
:   **To update.** Recapture the fresh-profile dark-theme Dashboard when its onboarding, start cards, quick actions, drop zone, keyboard hints, or surrounding application shell changes; then rerun `Get-FileHash -Algorithm SHA256 docs/user-guide/docs/en/assets/images/interface/hero-screenshot.png`.
:   **Data.** Fresh isolated profile; no astronomical sample data

`assets/images/interface/process-windows.png`
:   **Source.** Captured from the AfterNight Non-Local Bayes process window by the project maintainer
:   **Licence.** Project asset, distributed with AfterNight under the repository license
:   **Added** 2026-07-27. **SHA-256** `b695e994ea8e25e2c39b384d5f1de91cfe22bf801740446375530b270e86d043`.
:   **To update.** Recapture the representative process when the generic target/parameter/preview shell or Reset/Close/Apply footer changes; then rerun `Get-FileHash -Algorithm SHA256 docs/user-guide/docs/en/assets/images/interface/process-windows.png`.
:   **Data.** Maintainer-owned Caldwell 77 target; approved target title retained; preview illustrates UI state, not denoise quality

`assets/images/processes/stacking-integration/image-integration.png`
:   **Source.** Captured from AfterNight Image Integration by the project maintainer
:   **Licence.** Project asset, distributed with AfterNight under the repository license
:   **Added** 2026-07-28. **SHA-256** `7106df84cbdf017b94393480f58f2dd51452babf1399cb1cebe0d41cbaf5abb5`.
:   **To update.** Recapture when frame-source/workflow tabs, populated grouping, calibrated preview, output selection, or workspace layout changes; then rerun `Get-FileHash -Algorithm SHA256 docs/user-guide/docs/en/assets/images/processes/stacking-integration/image-integration.png`.
:   **Data.** Maintainer-owned Carina Nebula integration project; visible project, frame, and output paths, including personal directories, are intentionally retained; no credentials, tokens, secrets, or unrelated account data

`assets/javascripts/mermaid/mermaid.min.js`
:   **Source.** Mermaid npm package `mermaid@11.16.0`, `dist/mermaid.min.js` from `https://registry.npmjs.org/mermaid/-/mermaid-11.16.0.tgz`
:   **Licence.** MIT; local license copy at `assets/javascripts/mermaid/LICENSE-MIT`
:   **Added** 2026-07-20. **SHA-256** `74d7c46dabca328c2294733910a8aa1ed0c37451776e8d5295da38a2b758fb9b`.
:   **To update.** Run `npm pack mermaid@11.16.0 --pack-destination <temp>`, extract `package/dist/mermaid.min.js`, copy it to this path, check the package's integrity `sha512-Zvm3kbstgdpvIJPPItlL7fppIZ3kibvc1oZIGxdvk9t6UFz6flv+Jw7FtRGKwfcI8OckmH04LqG6LlS6X4B1pA==`, then rerun the diagram browser validator.

`assets/javascripts/mermaid/LICENSE-MIT`
:   **Source.** Mermaid npm package `mermaid@11.16.0`, `LICENSE` from `https://registry.npmjs.org/mermaid/-/mermaid-11.16.0.tgz`
:   **Licence.** MIT
:   **Added** 2026-07-20. **SHA-256** `ec9fb67dcb25eccc416ed56e1aab819222c805a2a4bfe4cb19e7556bf2ffde80`.
:   **To update.** Copy from the same Mermaid package tarball used for `mermaid.min.js` whenever the runtime is updated.

`assets/javascripts/mermaid/mermaid-init.js`
:   **Source.** AfterNight documentation maintainers; local file-mode renderer for the vendored Mermaid runtime
:   **Licence.** Project asset, distributed with AfterNight under the repository license
:   **Added** 2026-11-09. **SHA-256** `8031e6edf5fc3af55c780d41f4a3870549ec4650e7f99278faf61d2faf8341ff`.
:   **To update.** Keep strict Mermaid security, explicit-node rendering, the colors taken from the theme's tokens and the redraw on a theme change; rerun the strict build and `node build/scripts/render_user_guide.cjs --offline` in Chromium and Firefox.

`assets/javascripts/mathjax/tex-mml-chtml.js`
:   **Source.** MathJax npm package `mathjax@4.1.2`, `tex-mml-chtml.js` from `https://registry.npmjs.org/mathjax/-/mathjax-4.1.2.tgz`
:   **Licence.** Apache-2.0; local license copy at `assets/javascripts/mathjax/LICENSE-Apache-2.0.txt`, upstream license at `https://github.com/mathjax/MathJax/blob/master/LICENSE`
:   **Added** 2026-06-10. **SHA-256** `74f577e646a82ead6b83e25b61ff29d644eb65a9b0318f9723069650fc2ab695`.
:   **To update.** Run `npm pack mathjax@4.1.2 --pack-destination <temp>`, extract `package/tex-mml-chtml.js`, copy it to this path, then run `Get-FileHash -Algorithm SHA256 docs/user-guide/docs/en/assets/javascripts/mathjax/tex-mml-chtml.js`.

`assets/javascripts/mathjax/LICENSE-Apache-2.0.txt`
:   **Source.** MathJax npm package `mathjax@4.1.2`, `LICENSE` from `https://registry.npmjs.org/mathjax/-/mathjax-4.1.2.tgz`
:   **Licence.** Apache-2.0
:   **Added** 2026-06-10. **SHA-256** `cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30`.
:   **To update.** Copy from the same MathJax package tarball used for `tex-mml-chtml.js` whenever the runtime is updated.

`assets/javascripts/mathjax/mathjax-config.js`
:   **Source.** AfterNight documentation maintainers; the MathJax configuration `mkdocs.yml` loads before `tex-mml-chtml.js`
:   **Licence.** Project asset, distributed with AfterNight under the repository license
:   **Added** 2026-11-09. **SHA-256** `6297dd82a30a902a28bd726e0350b909b01a1ee1c196612698bbfd8e5f7cf35f`.
:   **To update.** Keep the font path on the vendored fonts, enrichment off and assistive MathML on; rerun the strict build and `node build/scripts/render_user_guide.cjs --offline` on the pages with math.

`assets/javascripts/mathjax/a11y/assistive-mml.js`
:   **Source.** MathJax npm package `mathjax@4.1.2`, `a11y/assistive-mml.js` from `https://registry.npmjs.org/mathjax/-/mathjax-4.1.2.tgz`; loaded because the configuration turns assistive MathML on
:   **Licence.** Apache-2.0; local license copy at `assets/javascripts/mathjax/LICENSE-Apache-2.0.txt`
:   **Added** 2026-11-09. **SHA-256** `6acc217aa1c8f923a75c8708a6f6fad5a2c4f6d7854924f4be5a1c59f195fad8`.
:   **To update.** Copy from the same MathJax package tarball used for `tex-mml-chtml.js` whenever the runtime is updated.

`assets/javascripts/mathjax/fonts/mathjax-newcm-font/chtml/` (105 `woff2/*.woff2` web fonts and 40 `dynamic/*.js` font-data files, 2.17 MB)
:   **Source.** MathJax npm package `@mathjax/mathjax-newcm-font@4.1.2`, `chtml/` from `https://registry.npmjs.org/@mathjax/mathjax-newcm-font/-/mathjax-newcm-font-4.1.2.tgz`, copied unchanged; the web fonts are MathJax's subsets of the New Computer Modern fonts by Antonis Tsolomitis
:   **Licence.** The package declares Apache-2.0 (local copy at `assets/javascripts/mathjax/fonts/mathjax-newcm-font/LICENSE-Apache-2.0.txt`); the glyphs are "(C) 2019-2021 Antonis Tsolomitis", released under the GUST Font License, as each web font's copyright record states (local copy at `assets/javascripts/mathjax/fonts/mathjax-newcm-font/GUST-FONT-LICENSE.txt`)
:   **Added** 2026-11-09. **SHA-256** `Not recorded per file; the package integrity is `sha512-lZHMjNP2XbABHA3kVn40rbse5ERUeMEmrGH03qLkCwxq4/5Z/eNLr0akw1MmQcqTwCbvkx1BFcmJ7RCfbRlw3Q==``.
:   **To update.** Update with `tex-mml-chtml.js`, to the font package version its MathJax version names: run `npm pack @mathjax/mathjax-newcm-font@<version> --pack-destination <temp>`, check the package's integrity, replace this folder with the package's `chtml/` unchanged, then rerun the strict build and `node build/scripts/render_user_guide.cjs --offline` in Chromium and Firefox.

`assets/javascripts/mathjax/fonts/mathjax-newcm-font/LICENSE-Apache-2.0.txt`
:   **Source.** The Apache-2.0 text of the MathJax npm package `mathjax@4.1.2` (`LICENSE`); the font package declares Apache-2.0 and ships no license file
:   **Licence.** Apache-2.0
:   **Added** 2026-11-09. **SHA-256** `cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30`.
:   **To update.** Copy from the MathJax package tarball whenever the fonts are updated.

`assets/javascripts/mathjax/fonts/mathjax-newcm-font/GUST-FONT-LICENSE.txt`
:   **Source.** GUST Font License version 1.0, from `https://tug.org/fonts/licenses/GUST-FONT-LICENSE.txt`, the license the New Computer Modern glyphs name
:   **Licence.** GUST Font License (LaTeX Project Public License 1.3c or later)
:   **Added** 2026-11-09. **SHA-256** `49ea6cb9257bbee0a3979c48a774cd221550ac1c20c95549efe45fc99cc18050`.
:   **To update.** Fetch again whenever the fonts are updated, and check the license each web font's copyright record names.

`assets/guide/fonts/inter-latin-wght-normal.woff2` (theme: `docs/user-guide/theme/`)
:   **Source.** Fontsource npm package `@fontsource-variable/inter@5.3.0`, `files/inter-latin-wght-normal.woff2` from `https://registry.npmjs.org/@fontsource-variable/inter/-/inter-5.3.0.tgz`; the Inter typeface, variable in weight, Latin subset; the same file as the website's
:   **Licence.** SIL Open Font License 1.1; local license copy at `assets/guide/fonts/LICENSE-inter.txt`
:   **Added** 2026-11-09. **SHA-256** `3100e775e8616cd2611beecfa23a4263d7037586789b43f035236a2e6fbd4c62`.
:   **To update.** Run `npm pack @fontsource-variable/inter@<version> --pack-destination <temp>`, check the package's integrity (5.3.0: `sha512-OupL48va4JNofb97w6NYeF9S7W/kHNKM0Er8Dem5nqi4jeOLrVJDoE8tZEpnMJmtkvNbB1EIPPwHcdkF6b1oUA==`), copy `package/files/inter-latin-wght-normal.woff2` unchanged, keep the website's copy the same, then rerun the strict build and `node build/scripts/render_user_guide.cjs --offline` in Chromium and Firefox.

`assets/guide/fonts/inter-latin-wght-italic.woff2` (theme: `docs/user-guide/theme/`)
:   **Source.** Fontsource npm package `@fontsource-variable/inter@5.3.0`, `files/inter-latin-wght-italic.woff2` from `https://registry.npmjs.org/@fontsource-variable/inter/-/inter-5.3.0.tgz`; the Inter typeface, variable in weight, Latin subset; the same file as the website's
:   **Licence.** SIL Open Font License 1.1; local license copy at `assets/guide/fonts/LICENSE-inter.txt`
:   **Added** 2026-11-09. **SHA-256** `7291b5970da2237441273c03b424a504b70b18f09791473fab99687dcc314720`.
:   **To update.** Run `npm pack @fontsource-variable/inter@<version> --pack-destination <temp>`, check the package's integrity (5.3.0: `sha512-OupL48va4JNofb97w6NYeF9S7W/kHNKM0Er8Dem5nqi4jeOLrVJDoE8tZEpnMJmtkvNbB1EIPPwHcdkF6b1oUA==`), copy `package/files/inter-latin-wght-italic.woff2` unchanged, keep the website's copy the same, then rerun the strict build and `node build/scripts/render_user_guide.cjs --offline` in Chromium and Firefox.

`assets/guide/fonts/sora-latin-wght-normal.woff2` (theme: `docs/user-guide/theme/`)
:   **Source.** Fontsource npm package `@fontsource-variable/sora@5.3.0`, `files/sora-latin-wght-normal.woff2` from `https://registry.npmjs.org/@fontsource-variable/sora/-/sora-5.3.0.tgz`; the Sora typeface, variable in weight, Latin subset; the same file as the website's
:   **Licence.** SIL Open Font License 1.1; local license copy at `assets/guide/fonts/LICENSE-sora.txt`
:   **Added** 2026-11-09. **SHA-256** `fa26406eeda9a3c6ec3d9ea8813c3045d6dc755e30c716d5c094e8ef43be5a7f`.
:   **To update.** Run `npm pack @fontsource-variable/sora@<version> --pack-destination <temp>`, check the package's integrity (5.3.0: `sha512-h8kHta4Z8SkfUGeI82TUy1n3rbrKpBRGwuk4t4qCwto5oEHxlbOFD2yGP+Df5fbmTXwKGn+o667vYp0E4K8Whw==`), copy `package/files/sora-latin-wght-normal.woff2` unchanged, keep the website's copy the same, then rerun the strict build and `node build/scripts/render_user_guide.cjs --offline` in Chromium and Firefox.

`assets/guide/fonts/jetbrains-mono-latin-wght-normal.woff2` (theme: `docs/user-guide/theme/`)
:   **Source.** Fontsource npm package `@fontsource-variable/jetbrains-mono@5.3.0`, `files/jetbrains-mono-latin-wght-normal.woff2` from `https://registry.npmjs.org/@fontsource-variable/jetbrains-mono/-/jetbrains-mono-5.3.0.tgz`; the JetBrains Mono typeface, variable in weight, Latin subset; the same file as the website's
:   **Licence.** SIL Open Font License 1.1; local license copy at `assets/guide/fonts/LICENSE-jetbrains-mono.txt`
:   **Added** 2026-11-09. **SHA-256** `18be452724bfdc236c074ca94a249a7f41a86752c7d04ab258ce9ed5651f6a7e`.
:   **To update.** Run `npm pack @fontsource-variable/jetbrains-mono@<version> --pack-destination <temp>`, check the package's integrity (5.3.0: `sha512-F32xpS2NsGYoQi2ADSkKTgpJj7ozajsGgDJ8woTnqjmIB+dxDIqImjl4pXZVEExu8UFZ2ndhmX18EBS/hdz3Lw==`), copy `package/files/jetbrains-mono-latin-wght-normal.woff2` unchanged, keep the website's copy the same, then rerun the strict build and `node build/scripts/render_user_guide.cjs --offline` in Chromium and Firefox.

`assets/guide/fonts/jetbrains-mono-latin-wght-italic.woff2` (theme: `docs/user-guide/theme/`)
:   **Source.** Fontsource npm package `@fontsource-variable/jetbrains-mono@5.3.0`, `files/jetbrains-mono-latin-wght-italic.woff2` from `https://registry.npmjs.org/@fontsource-variable/jetbrains-mono/-/jetbrains-mono-5.3.0.tgz`; the JetBrains Mono typeface, variable in weight, Latin subset; the same file as the website's
:   **Licence.** SIL Open Font License 1.1; local license copy at `assets/guide/fonts/LICENSE-jetbrains-mono.txt`
:   **Added** 2026-11-09. **SHA-256** `a8afa085e9ca5e53434e2ee918ba6b65c7dd4dda56509976b36591478c99d62e`.
:   **To update.** Run `npm pack @fontsource-variable/jetbrains-mono@<version> --pack-destination <temp>`, check the package's integrity (5.3.0: `sha512-F32xpS2NsGYoQi2ADSkKTgpJj7ozajsGgDJ8woTnqjmIB+dxDIqImjl4pXZVEExu8UFZ2ndhmX18EBS/hdz3Lw==`), copy `package/files/jetbrains-mono-latin-wght-italic.woff2` unchanged, keep the website's copy the same, then rerun the strict build and `node build/scripts/render_user_guide.cjs --offline` in Chromium and Firefox.

`assets/guide/fonts/LICENSE-inter.txt` (theme: `docs/user-guide/theme/`)
:   **Source.** Fontsource npm package `@fontsource-variable/inter@5.3.0`, `LICENSE`: the Inter Project Authors' copyright and the SIL Open Font License 1.1
:   **Licence.** SIL Open Font License 1.1
:   **Added** 2026-11-09. **SHA-256** `3b0a5fca3d17942cde889069889dedbbbd075e9b599968c82a95f4d944e9b345`.
:   **To update.** Copy from the same package tarball whenever the fonts are updated.

`assets/guide/fonts/LICENSE-sora.txt` (theme: `docs/user-guide/theme/`)
:   **Source.** Fontsource npm package `@fontsource-variable/sora@5.3.0`, `LICENSE`: the Sora Project Authors' copyright and the SIL Open Font License 1.1
:   **Licence.** SIL Open Font License 1.1
:   **Added** 2026-11-09. **SHA-256** `1ec9623d38c445eb4dfe5fcc783e0f0ee728cc97ca157ce1f8cb2b3bf9d9b0d9`.
:   **To update.** Copy from the same package tarball whenever the fonts are updated.

`assets/guide/fonts/LICENSE-jetbrains-mono.txt` (theme: `docs/user-guide/theme/`)
:   **Source.** Fontsource npm package `@fontsource-variable/jetbrains-mono@5.3.0`, `LICENSE`: the JetBrains Mono Project Authors' copyright and the SIL Open Font License 1.1
:   **Licence.** SIL Open Font License 1.1
:   **Added** 2026-11-09. **SHA-256** `403581b69dac5cff4079205e01c6b467e56af449ecbd7247693ddb1baafa005b`.
:   **To update.** Copy from the same package tarball whenever the fonts are updated.

`assets/guide/vendor/lunr-2.3.9.min.js` (theme: `docs/user-guide/theme/`)
:   **Source.** lunr npm package `lunr@2.3.9`, `lunr.min.js` from `https://registry.npmjs.org/lunr/-/lunr-2.3.9.tgz`, by Oliver Nightingale; the search index the theme's search.js builds in the browser
:   **Licence.** MIT; local license copy at `assets/guide/vendor/LICENSE-lunr.txt`
:   **Added** 2026-11-09. **SHA-256** `0c50d9002b85780a842afffb567bb54ede402dae7c6dc5997a018614d8044fc8` (the package's bytes; the repository stores its line endings as CRLF).
:   **To update.** Run `npm pack lunr@<version> --pack-destination <temp>`, check the package's integrity (2.3.9: `sha512-zTU3DaZaF3Rt9rhN3uBMGQD3dD2/vFQqnvZCDv4dl5iOzq2IZQqTxu90r4E5J+nP70J3ilqVCrbho2eWaeW8Ow==`), copy `package/lunr.min.js`, rename it after the version and update `data-search-engine` in `theme/base.html`, then rerun the strict build and the render tool's search queries in Chromium and Firefox.

`assets/guide/vendor/LICENSE-lunr.txt` (theme: `docs/user-guide/theme/`)
:   **Source.** lunr npm package `lunr@2.3.9`, `LICENSE`: "Copyright (C) 2013 by Oliver Nightingale"
:   **Licence.** MIT
:   **Added** 2026-11-09. **SHA-256** `ae33f5a0d538cd93de1136a052ffa734b59d6b28e7cfc19e03de1d8b5d9f40ea`.
:   **To update.** Copy from the same package tarball whenever lunr is updated.

`interface/overview.md#move-between-the-dashboard-and-the-workspace` (inline Mermaid source)
:   **Source.** AfterNight documentation maintainers; current dashboard/dock-workspace lifecycle source/tests
:   **Licence.** Project content, distributed with AfterNight under the repository license
:   **Added** 2026-07-20. **SHA-256** `32fa46a4090bae16f5beaee808c9e7e5cc28e4b4100683924b24986514cbdbcc`.
:   **To update.** Edit the complete Mermaid fence, rerun the strict build and the diagram browser validator, then regenerate the normalized block hash.

`processes/stacking-integration/image-integration-reference.md#the-stages-of-a-run` (inline Mermaid source)
:   **Source.** AfterNight documentation maintainers; current manifest, executor, stage, report, and focused-test sources
:   **Licence.** Project content, distributed with AfterNight under the repository license
:   **Added** 2026-11-10. **SHA-256** `4f777cb29646f32ee07424dc0bf6261c08cb273fb507d03e4bf03d42fc2123d3`.
:   **To update.** Edit the complete Mermaid fence, rerun the strict build and the diagram browser validator, then regenerate the normalized block hash.

## Captured windows

The pictures of the application's windows that
`build/scripts/tools/capture_guide_screenshots.ps1` captures and
`build/scripts/tools/optimize_guide_image.py` prepares are listed by the group
of their pages, one row each: the picture, what it shows, the data in it, the
day it was captured with the application's commit, and the SHA-256 of the file.
They are project assets, captured by the maintainers from their own data and
distributed with AfterNight under the repository license. To update one,
capture the window again as `docs/user-guide/SCREENSHOTS.md` says and replace
its row with the one the second script prints.

### Getting started

| Picture | Shows | Data | Captured | SHA-256 |
| --- | --- | --- | --- | --- |

### Interface

| Picture | Shows | Data | Captured | SHA-256 |
| --- | --- | --- | --- | --- |

### Stretching

| Picture | Shows | Data | Captured | SHA-256 |
| --- | --- | --- | --- | --- |
| `assets/images/processes/stretching/histogram-transformation-before.webp` | The image before Histogram Transformation, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `28a8d3daf9684ff41aba81be5ccc2af36783aaa0a990822e02536306002e7efa` |
| `assets/images/processes/stretching/histogram-transformation-after.webp` | The image after Histogram Transformation with Shadows and Midtones at 0.02, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `4a95dbb0ef59160d510677d9a416d955584c2c77117fdc61c93aafc25c6dab9f` |
| `assets/images/processes/stretching/curves-transformation-before.webp` | The image before Curves Transformation, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `4a95dbb0ef59160d510677d9a416d955584c2c77117fdc61c93aafc25c6dab9f` |
| `assets/images/processes/stretching/curves-transformation-after.webp` | The image after Curves Transformation with one point dragged to input 0.28, output 0.14, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `89c7513519d991969066224490a17219c91264b944b15679676dad7183bfffba` |
| `assets/images/processes/stretching/gh-transformation-before.webp` | The image before Generalized Hyperbolic Transformation, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `28a8d3daf9684ff41aba81be5ccc2af36783aaa0a990822e02536306002e7efa` |
| `assets/images/processes/stretching/gh-transformation-after.webp` | The image after Generalized Hyperbolic Transformation with Stretch factor at 2.5, Local intensity at 5 and Symmetry point at 0.025, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `9eb1d84cc1904498c8bae6295ef06a9bd94edd430d125c067fec6cbf9af9dffd` |
| `assets/images/processes/stretching/arcsinh-stretch-before.webp` | The image before ArcSinh Stretch, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `28a8d3daf9684ff41aba81be5ccc2af36783aaa0a990822e02536306002e7efa` |
| `assets/images/processes/stretching/arcsinh-stretch-after.webp` | The image after ArcSinh Stretch with Stretch Factor at 200 and Black Point at 0.02, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `22cb5879c6d7c06a30cdc4cdbc0845aab6099eae6630735450ee34238c128cd0` |
| `assets/images/processes/stretching/statistical-stretch-before.webp` | The image before Statistical Stretch, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `28a8d3daf9684ff41aba81be5ccc2af36783aaa0a990822e02536306002e7efa` |
| `assets/images/processes/stretching/statistical-stretch-after.webp` | The image after Statistical Stretch with Target Median at 0.15, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `6d09b4a77d09c355f2747c6ef5ae6b16563995ed5cc26289b887715eeb535952` |
| `assets/images/processes/stretching/gamma-correction-before.webp` | The image before Gamma Correction, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `4a95dbb0ef59160d510677d9a416d955584c2c77117fdc61c93aafc25c6dab9f` |
| `assets/images/processes/stretching/gamma-correction-after.webp` | The image after Gamma Correction with Gamma at 0.50, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `2c6e19d4e71f0679d4775666dd4ae269e7fa114cba733b7b6d7c484010d75764` |
| `assets/images/processes/stretching/exposure-correction-before.webp` | The image before Exposure Correction, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `4a95dbb0ef59160d510677d9a416d955584c2c77117fdc61c93aafc25c6dab9f` |
| `assets/images/processes/stretching/exposure-correction-after.webp` | The image after Exposure Correction with Exposure at 1.5, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `a7fb652ed982598ab1a109fd8101f96697905bae3b3de5a8c5e4723779202a46` |

### Sharpening

| Picture | Shows | Data | Captured | SHA-256 |
| --- | --- | --- | --- | --- |
| `assets/images/processes/sharpening/unsharp-mask-before.webp` | The image before Unsharp Mask, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `7bf2d97e33a1230d392981ab7236fcbde90b03db1e03b1b145856de6b506c6f3` |
| `assets/images/processes/sharpening/unsharp-mask-after.webp` | The image after Unsharp Mask with Radius at 2.00 and Amount at 1.00, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `dabb7dba9ebd9dbc91cd9d80bd1cda5b338b37c6afe60b33de68cef36eb11c96` |
| `assets/images/processes/sharpening/sharpen-alpha-before.webp` | The image before Multiscale Sharpen Alpha, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `7bf2d97e33a1230d392981ab7236fcbde90b03db1e03b1b145856de6b506c6f3` |
| `assets/images/processes/sharpening/sharpen-alpha-after.webp` | The image after Multiscale Sharpen Alpha with Amplitude at 1.00, Number of Scales at 5, Anisotropy at 0.00 and Amount at 0.70, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `8d758f16589e04d96be1e41c00065f1ce7cd431632c678754bc000450972356c` |
| `assets/images/processes/sharpening/high-pass-filter-before.webp` | The image before High Pass Filter, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `7bf2d97e33a1230d392981ab7236fcbde90b03db1e03b1b145856de6b506c6f3` |
| `assets/images/processes/sharpening/high-pass-filter-after.webp` | The image after High Pass Filter with Radius at 5.00, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `de357345483f036248f209a47854d61b942109c978052d86ebcd17146d992b6a` |
| `assets/images/processes/sharpening/local-contrast-enhancement-before.webp` | The image before Local Contrast Enhancement, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `7bf2d97e33a1230d392981ab7236fcbde90b03db1e03b1b145856de6b506c6f3` |
| `assets/images/processes/sharpening/local-contrast-enhancement-after.webp` | The image after Local Contrast Enhancement with its default values and the preview area off, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `31d9e2d215872df840d979b160726d3064ca0d8b38bbbbddd0f2e45399d834db` |
| `assets/images/processes/sharpening/clahe-before.webp` | The image before CLAHE, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `7bf2d97e33a1230d392981ab7236fcbde90b03db1e03b1b145856de6b506c6f3` |
| `assets/images/processes/sharpening/clahe-after.webp` | The image after CLAHE with Contrast Limit at 1.50, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `8ebc3ed17ae9c01cf49edf87f20086aa9f673398acd55027a966f9f0bc88c14b` |
| `assets/images/processes/sharpening/local-histogram-equalization-before.webp` | The image before Local Histogram Equalization, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `7bf2d97e33a1230d392981ab7236fcbde90b03db1e03b1b145856de6b506c6f3` |
| `assets/images/processes/sharpening/local-histogram-equalization-after.webp` | The image after Local Histogram Equalization with its default values, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `10bb91b4989a4dc3af522a02423f7f00a1b8a32c0513f2dd7ad19d30fd9ffa70` |

### Star Operations

| Picture | Shows | Data | Captured | SHA-256 |
| --- | --- | --- | --- | --- |
| `assets/images/processes/star-operations/dark-structure-enhance-before.webp` | The image before Dark Structure Enhance, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `7bf2d97e33a1230d392981ab7236fcbde90b03db1e03b1b145856de6b506c6f3` |
| `assets/images/processes/star-operations/dark-structure-enhance-after.webp` | The image after Dark Structure Enhance with its default values, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `edf42333dca094d00044868e7f2eae85bee9512cec8c7d397462cb86fea4ad64` |
| `assets/images/processes/star-operations/star-reduction-before.webp` | The image before Star Reduction, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `7bf2d97e33a1230d392981ab7236fcbde90b03db1e03b1b145856de6b506c6f3` |
| `assets/images/processes/star-operations/star-reduction-after.webp` | The image after Star Reduction with Star Method, Strong and Iterations at 2, against the starless image Starnet++ made, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `8519d2c7eee6ed5f364a78853eeeddb975d6fd209f5537b5afecd1595c648974` |
| `assets/images/processes/star-operations/starmask-recombination-before.webp` | The image before Starmask Recombination, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, then its starless image and star mask made by Starnet++, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `91b7ddf2441cd0270d891b14c6719574f4d0b0adb5ee949e00b8d125535ee044` |
| `assets/images/processes/star-operations/starmask-recombination-after.webp` | The image after Starmask Recombination with the starless image and the star mask Starnet++ made, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, then its starless image and star mask made by Starnet++, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `c6a4c052cfcd6ae82fee1e05ce62413533dc89fb8865300b767da7455c9d03af` |
| `assets/images/processes/star-operations/profiled-starmask-recomb-before.webp` | The image before Profiled Starmask Recombination, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, then its starless image and star mask made by Starnet++, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `91b7ddf2441cd0270d891b14c6719574f4d0b0adb5ee949e00b8d125535ee044` |
| `assets/images/processes/star-operations/profiled-starmask-recomb-after.webp` | The image after Profiled Starmask Recombination with a Star Profiler v2 profile of 2,187 stars, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, then its starless image and star mask made by Starnet++, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `eb55319add10ca3f22cd80f5fb6576f0cd157b3113eb44a90dff33e3b069c771` |
| `assets/images/processes/star-operations/starnet-before.webp` | The image before Starnet++, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `29d0dd7bf699a9d697e7c26683feafb9415a6fcc00b4315e493b5ddec377583e` |
| `assets/images/processes/star-operations/starnet-after.webp` | The starless image Starnet++ made, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `571b52f53a504b8aea987a11bec2ee5ee919ddaaa2cdbf4dd065bf62890954d9` |
| `assets/images/processes/star-operations/starnet-star-mask-before.webp` | The image before Starnet++ Star Mask, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `29d0dd7bf699a9d697e7c26683feafb9415a6fcc00b4315e493b5ddec377583e` |
| `assets/images/processes/star-operations/starnet-star-mask-after.webp` | The star mask Starnet++ Star Mask made, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `5b6320b1d97ecdb2d90fbabb82f9cb9bbc8c6e7d23afc1c386fbd7f165b531b5` |
| `assets/images/processes/star-operations/star-mask-before.webp` | The image Star Mask works on, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, shown as Autostretch in its view | 2026-11-10, AfterNight 5bd712a | `bdfe571c451dc9c3f102c8e195236496afdbd3ff1ff1b018e0d58e8bb5048d6b` |
| `assets/images/processes/star-operations/star-mask-after.webp` | The mask Star Mask made with Percent Increase 1.00, a crop at 100 % of the middle of the image, as its view shows it | A mask made from linear RGB master of the Carina Nebula, the maintainer's own data, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `1d03458ce11789a3e19a331aec53ee88ffcf279fbf6fc717b4e6407f8dc2b19b` |

### Color Composition

| Picture | Shows | Data | Captured | SHA-256 |
| --- | --- | --- | --- | --- |
| `assets/images/processes/color-composition/colorize-before.webp` | The image before Colorize, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `4a95dbb0ef59160d510677d9a416d955584c2c77117fdc61c93aafc25c6dab9f` |
| `assets/images/processes/color-composition/colorize-after.webp` | The image after Colorize with Hue at 200, Saturation at 0.60 and Use lightness on, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `32d8d26146b79a29e8a04ad21c53a4349b0cb9148e4d82b11af74318c6f799ce` |
| `assets/images/processes/color-composition/channel-composition-before.webp` | The red master Channel Composition takes, the whole image, as its view shows it | The linear mono masters (Red, Green, Blue, Luminance) of the same Carina Nebula night, the maintainer's own data, the red master shown as Autostretch in its view | 2026-11-10, AfterNight 5bd712a | `32b2e8e76a54f5444022711979d1c86e737e21f38a74569cb9143c48c7294a93` |
| `assets/images/processes/color-composition/channel-composition-after.webp` | The color image Channel Composition made, the whole image, as its view shows it | The linear mono masters (Red, Green, Blue, Luminance) of the same Carina Nebula night, the maintainer's own data, composed, shown as Autostretch in its view | 2026-11-10, AfterNight 5bd712a | `b7565d49aa12db306ac60ae7484aca3973acdc396b5df0eac88482e0b379c4f0` |
| `assets/images/processes/color-composition/lrgb-combination-before.webp` | The color image before LRGB Combination, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `4a95dbb0ef59160d510677d9a416d955584c2c77117fdc61c93aafc25c6dab9f` |
| `assets/images/processes/color-composition/lrgb-combination-after.webp` | The image after LRGB Combination with the stretched luminance and Saturation 0.40, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view; luminance as above | 2026-11-10, AfterNight 5bd712a | `ec2a402ee2b96a18d10afe527f12826ab5000c1b631bf120a40858756fc7d9d1` |
| `assets/images/processes/color-composition/nbrgb-combination-before.webp` | The color stand-in before NBRGB Combination, the whole image, as its view shows it | Linear narrowband masters of the Eagle Nebula (Ha, OIII, SII) stacked for the guide with Image Integration Lite from the project's sample data; a color stand-in composed from SII and OIII, shown as Autostretch with Linked channels in its view | 2026-11-10, AfterNight 5bd712a | `b91838677ba6998b02a7d0b5867d5a38336af69ea283374f607ae84c4a662d89` |
| `assets/images/processes/color-composition/nbrgb-combination-after.webp` | The image after NBRGB Combination with Ha in Red, the whole image, as its view shows it | Linear narrowband masters of the Eagle Nebula (Ha, OIII, SII) stacked for the guide with Image Integration Lite from the project's sample data, shown as Autostretch with Linked channels in its view | 2026-11-10, AfterNight 5bd712a | `4c597ca3e06f5bdeb9adb7525ccdc4b7e665bc0bc8f2c700373deb9241aea78b` |
| `assets/images/processes/color-composition/nb-color-mapper-before.webp` | The Ha master NB Color Mapper takes, the whole image, as its view shows it | Linear narrowband masters of the Eagle Nebula (Ha, OIII, SII) stacked for the guide with Image Integration Lite from the project's sample data, the Ha master shown as Autostretch in its view | 2026-11-10, AfterNight 5bd712a | `c32f05727806d32842582e48c5e2a1ef73310a7d7c3c8861af2b1d85fa76ec41` |
| `assets/images/processes/color-composition/nb-color-mapper-after.webp` | The image NB Color Mapper made, the whole image, as its view shows it | Linear narrowband masters of the Eagle Nebula (Ha, OIII, SII) stacked for the guide with Image Integration Lite from the project's sample data, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `34ad1fa7ea2b6749142b6cae60dfbc33fedf5c7853fbde683a74396a58d284de` |
| `assets/images/processes/color-composition/narrowband-palette-composer-before.webp` | The Ha master the composer takes, the whole image, as its view shows it | Linear narrowband masters of the Eagle Nebula (Ha, OIII, SII) stacked for the guide with Image Integration Lite from the project's sample data, the Ha master shown as Autostretch in its view | 2026-11-10, AfterNight 5bd712a | `c32f05727806d32842582e48c5e2a1ef73310a7d7c3c8861af2b1d85fa76ec41` |
| `assets/images/processes/color-composition/narrowband-palette-composer-after.webp` | The SHO image the composer made, the whole image, as its view shows it | Linear narrowband masters of the Eagle Nebula (Ha, OIII, SII) stacked for the guide with Image Integration Lite from the project's sample data, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `44d9765f1ef44c21aded2f87cc366b24c771973b005a55081c2ea0a4f0b5e3b0` |

### Color Adjustment

| Picture | Shows | Data | Captured | SHA-256 |
| --- | --- | --- | --- | --- |
| `assets/images/processes/color-adjustment/tighten-edges-before.webp` | The image before Tighten Edges, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `7bf2d97e33a1230d392981ab7236fcbde90b03db1e03b1b145856de6b506c6f3` |
| `assets/images/processes/color-adjustment/tighten-edges-after.webp` | The image after Tighten Edges with Value at 0.50 and Amount at 1.00, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `080dbb27a91c9d2d2f1df73f48622991cba9bb0a6fa1cc9b94ebef14e0a9d504` |
| `assets/images/processes/color-adjustment/adjust-brightness-contrast-before.webp` | The image before Adjust Brightness And Contrast, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `4a95dbb0ef59160d510677d9a416d955584c2c77117fdc61c93aafc25c6dab9f` |
| `assets/images/processes/color-adjustment/adjust-brightness-contrast-after.webp` | The image after Adjust Brightness And Contrast with Brightness at -10 and Contrast at 20, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `33520d09070c8358518be667ec7161a4f236d766dfb81565b5b7ffb6919bd66a` |
| `assets/images/processes/color-adjustment/color-saturation-before.webp` | The image before Color Saturation, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `4a95dbb0ef59160d510677d9a416d955584c2c77117fdc61c93aafc25c6dab9f` |
| `assets/images/processes/color-adjustment/color-saturation-after.webp` | The image after Color Saturation with Amount at 0.50, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `12039d03f9f50a8b1f96faad03e2ff95169007bf250b4dd5a31684c25d720588` |
| `assets/images/processes/color-adjustment/adjust-hsl-before.webp` | The image before Adjust Hue/Saturation/Lightness, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `4a95dbb0ef59160d510677d9a416d955584c2c77117fdc61c93aafc25c6dab9f` |
| `assets/images/processes/color-adjustment/adjust-hsl-after.webp` | The image after Adjust Hue/Saturation/Lightness with Saturation at 25, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `0d8c2264adeba3c34e84569e13572f2b0713e07bdff032fc1b39590ab3619354` |
| `assets/images/processes/color-adjustment/adjust-lch-before.webp` | The image before Adjust Lightness/Chroma/Hue, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `4a95dbb0ef59160d510677d9a416d955584c2c77117fdc61c93aafc25c6dab9f` |
| `assets/images/processes/color-adjustment/adjust-lch-after.webp` | The image after Adjust Lightness/Chroma/Hue with Chroma at 25, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `c37dcbf0c7d10a7b35fc769c7638227ba8f0d8e8339231ba9eccc60b97686ecd` |

### Geometry

| Picture | Shows | Data | Captured | SHA-256 |
| --- | --- | --- | --- | --- |
| `assets/images/processes/geometry/crop-before.webp` | The image before Crop with the crop frame over it, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, shown as Autostretch in its view | 2026-11-10, AfterNight 5bd712a | `624a7391bb97e545ad657557a4c26f4c6f3349742ba42887c7b50c30487937d1` |
| `assets/images/processes/geometry/crop-after.webp` | The image after Crop to 4000 by 3000 pixels, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, shown as Autostretch in its view | 2026-11-10, AfterNight 5bd712a | `acc662dfd3d4e35bd260249dee60a87af66b559869879db778d0a8889b3ce578` |
| `assets/images/processes/geometry/rotate-before.webp` | The image while Rotate is open, the view showing the turn and the crop frame, the whole image | A linear RGB master of the Carina Nebula, the maintainer's own data, shown as Autostretch in its view | 2026-11-10, AfterNight 5bd712a | `34f2faa5608bf5fb66efd48775070cac6148f7333b4c46bf2f90c0d7b89e73c0` |
| `assets/images/processes/geometry/rotate-after.webp` | The image after Rotate by 10 degrees and a crop of 6000 by 4400 pixels, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, shown as Autostretch in its view | 2026-11-10, AfterNight 5bd712a | `ea4c7096e0fca476803260f27a0eaee89cc8ddc4bc4e008e8ecfe35b757db5a7` |

### Analysis

| Picture | Shows | Data | Captured | SHA-256 |
| --- | --- | --- | --- | --- |
| `assets/images/processes/analysis/range-selection-before.webp` | The image before Range Selection, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `4a95dbb0ef59160d510677d9a416d955584c2c77117fdc61c93aafc25c6dab9f` |
| `assets/images/processes/analysis/range-selection-after.webp` | The mask Range Selection made with Lower limit at 0.45 and Smoothness at 4, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `482af9bb9b8523722ce949742b63881852e4cca188f3cf05e1c1209329ee4e6d` |
| `assets/images/processes/analysis/image-blend-before.webp` | The image before Image Blend, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `7bf2d97e33a1230d392981ab7236fcbde90b03db1e03b1b145856de6b506c6f3` |
| `assets/images/processes/analysis/image-blend-after.webp` | The image after Image Blend with a High Pass layer in Overlay, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `08d1303ac854b0d5b97c6c0d1fec012e46865d4acbdee9c368f143c20303da9b` |

### Masking

| Picture | Shows | Data | Captured | SHA-256 |
| --- | --- | --- | --- | --- |
| `assets/images/processes/masking/binarize-before.webp` | The image before Binarize, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `4a95dbb0ef59160d510677d9a416d955584c2c77117fdc61c93aafc25c6dab9f` |
| `assets/images/processes/masking/binarize-after.webp` | The image after Binarize with its default values, RGB/K at 0.5, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `a37fa128997904c077b3aa89d6b10cf79310e913e19213e46f145977146e99bc` |
| `assets/images/processes/masking/morphological-transformation-before.webp` | The image before Morphological Transformation, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `7bf2d97e33a1230d392981ab7236fcbde90b03db1e03b1b145856de6b506c6f3` |
| `assets/images/processes/masking/morphological-transformation-after.webp` | The image after Morphological Transformation with Opening on a 7 x 7 circular element, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `3b50cda58271c49050db2ae8146a7bf15d61ee4528d22ba33379315d789736a9` |
| `assets/images/processes/masking/mask-drawing-before.webp` | The image Mask Drawing works on, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, shown as Autostretch in its view | 2026-11-10, AfterNight 5bd712a | `b7565d49aa12db306ac60ae7484aca3973acdc396b5df0eac88482e0b379c4f0` |
| `assets/images/processes/masking/mask-drawing-after.webp` | The mask Mask Drawing made from one ellipse at Feather 100, the whole image, as its view shows it | A mask made from a linear RGB master of the Carina Nebula, the maintainer's own data, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `ab87e0ae492c5495290ea8f25156715894ad75efcbfea3d9c0c8c4323a7b26c5` |

### Utility

| Picture | Shows | Data | Captured | SHA-256 |
| --- | --- | --- | --- | --- |
| `assets/images/processes/utility/noise-generator-before.webp` | The image before Noise Generator, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `7bf2d97e33a1230d392981ab7236fcbde90b03db1e03b1b145856de6b506c6f3` |
| `assets/images/processes/utility/noise-generator-after.webp` | The image after Noise Generator with Gaussian noise of Var 0.01, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `01601801f9002bcdf2cabfee5e7b62e25702a305b2cf4a3a3fadd66c07ae8f45` |

### Noise Reduction

| Picture | Shows | Data | Captured | SHA-256 |
| --- | --- | --- | --- | --- |
| `assets/images/processes/noise-reduction/gaussian-blur-before.webp` | The image before Gaussian Blur, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `7bf2d97e33a1230d392981ab7236fcbde90b03db1e03b1b145856de6b506c6f3` |
| `assets/images/processes/noise-reduction/gaussian-blur-after.webp` | The image after Gaussian Blur with Sigma at 2.00, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `5e025c5f483f8535b6d90022aa1ea18a2eab9ea4f2fbcac95e202ae20f69ff89` |
| `assets/images/processes/noise-reduction/noise-crop-before.webp` | The crop the noise-reduction pairs start from, not denoised, at 100 %, as its view shows it | A 1,100 x 820 crop of the middle of a linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `07796fb0699ae5e4b7c440950e12931a0a971429234257c7a53a885dd70ccb16` |
| `assets/images/processes/noise-reduction/wavelet-denoise-after.webp` | The crop after Wavelet Denoise at its defaults, at 100 %, as its view shows it | A 1,100 x 820 crop of the middle of a linear RGB master of the Carina Nebula, the maintainer's own data, denoised, then Histogram Transformation with Shadows and Midtones at 0.02 applied, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `6effa2a8bc1fb6b3eb8bd22aabc376565381f1a75ea953d591962277e92abcab` |
| `assets/images/processes/noise-reduction/total-variation-denoise-after.webp` | The crop after Total Variation Denoise, Chambolle, Weight 0.0050, at 100 %, as its view shows it | A 1,100 x 820 crop of the middle of a linear RGB master of the Carina Nebula, the maintainer's own data, denoised, then Histogram Transformation with Shadows and Midtones at 0.02 applied, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `35b2f2f07430e779329223365b9fbc30bf6620e266207fafa85a8f5feacd7cc1` |
| `assets/images/processes/noise-reduction/nl-bayes-after.webp` | The crop after NL-Bayes at its defaults, at 100 %, as its view shows it | A 1,100 x 820 crop of the middle of a linear RGB master of the Carina Nebula, the maintainer's own data, denoised, then Histogram Transformation with Shadows and Midtones at 0.02 applied, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `5fcb22c40c037ce77e0be13db5cbccf6da7f6631673fb69812fe8ce5b45cb76e` |
| `assets/images/processes/noise-reduction/patch-pca-denoise-after.webp` | The crop after Patch PCA Denoise, Patch Size 5, Lookup Size 7, at 100 %, as its view shows it | A 1,100 x 820 crop of the middle of a linear RGB master of the Carina Nebula, the maintainer's own data, denoised, then Histogram Transformation with Shadows and Midtones at 0.02 applied, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `cdd7768b76820063d2fa16c62e4335d28a661c57f4203c3e73e73ece7bceb15d` |
| `assets/images/processes/noise-reduction/bilateral-filter-after.webp` | The crop after Bilateral Filter, Sigma Color 0.01, at 100 %, as its view shows it | A 1,100 x 820 crop of the middle of a linear RGB master of the Carina Nebula, the maintainer's own data, denoised, then Histogram Transformation with Shadows and Midtones at 0.02 applied, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `b106227f29a2ee0020680a72aa18642cb17471a0bab1189c00e8d17ae0b1818f` |
| `assets/images/processes/noise-reduction/median-filter-after.webp` | The crop after Median Filter at its defaults, at 100 %, as its view shows it | A 1,100 x 820 crop of the middle of a linear RGB master of the Carina Nebula, the maintainer's own data, denoised, then Histogram Transformation with Shadows and Midtones at 0.02 applied, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `aea142378a58fd1de46e2364183ee63371fb2ffed631cfc634ad3f64bd422a46` |
| `assets/images/processes/noise-reduction/graycstoration-after.webp` | The crop after GRAYCstoration, Amplitude 5, at 100 %, as its view shows it | A 1,100 x 820 crop of the middle of a linear RGB master of the Carina Nebula, the maintainer's own data, denoised, then Histogram Transformation with Shadows and Midtones at 0.02 applied, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `f8b8cf19a6c50255b82d1a16faa1e35be9510850d2984e1f3f4ff638be644569` |
| `assets/images/processes/noise-reduction/banding-reduction-before.webp` | The banded crop before Banding Reduction, at 100 %, as its view shows it | A 1,100 x 820 crop of the top right of a linear RGB master of the Carina Nebula, the maintainer's own data, with bands added by Pixel Math, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `31ba478ac449a1449b393a2fcb542c4d2e76720ae8bf3059a585f90676fa5200` |
| `assets/images/processes/noise-reduction/banding-reduction-after.webp` | The banded crop after Banding Reduction with Protect From Highlights, at 100 %, as its view shows it | A 1,100 x 820 crop of the top right of a linear RGB master of the Carina Nebula, the maintainer's own data, with bands added by Pixel Math, then Banding Reduction, then Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `80e8459e04dd7a75389ac3a2d55eafaf2178d0939fb07d44cbae2079c4ee8f43` |

### Pixel Math

| Picture | Shows | Data | Captured | SHA-256 |
| --- | --- | --- | --- | --- |

### Multiscale

| Picture | Shows | Data | Captured | SHA-256 |
| --- | --- | --- | --- | --- |
| `assets/images/processes/multiscale/multiscale-linear-after.webp` | The crop after Multiscale Linear Transform with k-Sigma Thresholding at Amount 0.70, at 100 %, as its view shows it | A 1,100 x 820 crop of the middle of a linear RGB master of the Carina Nebula, the maintainer's own data, denoised, then Histogram Transformation with Shadows and Midtones at 0.02 applied, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `3400e89efb817ffd82e50df57d7c78b1ec9e8bb145ec39caecbbf8b3bbd0cec0` |

### Background Extraction

| Picture | Shows | Data | Captured | SHA-256 |
| --- | --- | --- | --- | --- |
| `assets/images/processes/background-extraction/background-extraction-before.webp` | The image before Background Extraction, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, shown as Autostretch in its view | 2026-11-10, AfterNight 5bd712a | `b7565d49aa12db306ac60ae7484aca3973acdc396b5df0eac88482e0b379c4f0` |
| `assets/images/processes/background-extraction/background-extraction-after.webp` | The image after Background Extraction, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, shown as Autostretch in its view | 2026-11-10, AfterNight 5bd712a | `36c29b33c9670ce70db4bc4dfceeebbeb66d73b61bcadf60298fd5730ee360cb` |
| `assets/images/processes/background-extraction/auto-gradient-removal-before.webp` | The image before Auto Gradient Removal, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, shown as Autostretch in its view | 2026-11-10, AfterNight 5bd712a | `b7565d49aa12db306ac60ae7484aca3973acdc396b5df0eac88482e0b379c4f0` |
| `assets/images/processes/background-extraction/auto-gradient-removal-after.webp` | The image after Auto Gradient Removal at its defaults, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, shown as Autostretch in its view | 2026-11-10, AfterNight 5bd712a | `7f358f36f7f91d4a53f5282a2281f72278f5b0b53564197974cd666de0922bfd` |

### Color Calibration

| Picture | Shows | Data | Captured | SHA-256 |
| --- | --- | --- | --- | --- |
| `assets/images/processes/color-calibration/background-neutralization-before.webp` | The composed image before Background Neutralization, the whole image | The linear mono masters (Red, Green, Blue, Luminance) of the same Carina Nebula night, the maintainer's own data; an RGB image composed from the red, green and blue masters without Linear Match, after Histogram Transformation with Shadows 0.06 and Midtones 0.03, shown as Linear | 2026-11-10, AfterNight 5bd712a | `b27b906ae017b281fe22d9379f5932d44612d8d96088f917607812a3a017c957` |
| `assets/images/processes/color-calibration/background-neutralization-after.webp` | The composed image after Background Neutralization with Upper Limit 0.2, the whole image | The linear mono masters (Red, Green, Blue, Luminance) of the same Carina Nebula night, the maintainer's own data; an RGB image composed from the red, green and blue masters without Linear Match, after Histogram Transformation with Shadows 0.06 and Midtones 0.03, shown as Linear | 2026-11-10, AfterNight 5bd712a | `027632ed483a8eaf60be23d42a78a929c7c4ecffae755b4c1ab2a8b5f6246a76` |
| `assets/images/processes/color-calibration/scnr-before.webp` | The image before SCNR, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `29d0dd7bf699a9d697e7c26683feafb9415a6fcc00b4315e493b5ddec377583e` |
| `assets/images/processes/color-calibration/scnr-after.webp` | The image after SCNR at its defaults, a crop at 100 % of the middle of the image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `f9fa3ce2b81fccc5a9553fc3bad4a51103ebf0ba0be6895a2a30dc87e1a4c56d` |
| `assets/images/processes/color-calibration/linear-match-before.webp` | The green master before Linear Match, the whole image | The linear mono masters (Red, Green, Blue, Luminance) of the same Carina Nebula night, the maintainer's own data, the green master, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear | 2026-11-10, AfterNight 5bd712a | `88b8d9cf05ae0ca2d73e0bf6f659d4187109b743d5ed219d010aefceb6a33e11` |
| `assets/images/processes/color-calibration/linear-match-after.webp` | The green master after Linear Match against the red master, the whole image | The linear mono masters (Red, Green, Blue, Luminance) of the same Carina Nebula night, the maintainer's own data, the green master, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear | 2026-11-10, AfterNight 5bd712a | `b691cc0a2c6e51e9db55be94759c86141727c7fb102a2ab6611a509137523fb3` |
| `assets/images/processes/color-calibration/color-calibration-before.webp` | The image before Color Calibration, the whole image | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `4a95dbb0ef59160d510677d9a416d955584c2c77117fdc61c93aafc25c6dab9f` |
| `assets/images/processes/color-calibration/color-calibration-after.webp` | The image after Color Calibration with the top right star field as both references, the whole image | A linear RGB master of the Carina Nebula, the maintainer's own data, after Histogram Transformation with Shadows and Midtones at 0.02, shown as Linear in its view | 2026-11-10, AfterNight 5bd712a | `6abc190f97a9f41bbc61f9628ba0de6a11ec9ee13d6c2fbefaac6bc93d530446` |
| `assets/images/processes/color-calibration/photometric-cc-before.webp` | The image before Photometric Color Calibration, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, shown as Autostretch with Linked channels in its view | 2026-11-10, AfterNight 5bd712a | `8c4d9da2c0633eeb016cf44690333426e0203066951ffe51dd6152305cfdbf9e` |
| `assets/images/processes/color-calibration/photometric-cc-after.webp` | The image after Photometric Color Calibration at its defaults, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, shown as Autostretch with Linked channels in its view | 2026-11-10, AfterNight 5bd712a | `b5b07c99df9edde0dc8e1da3d2eb0cef78d000188de0b08ad7bc9f28772755b5` |
| `assets/images/processes/color-calibration/spectrophotometric-cc-before.webp` | The image before Spectrophotometric Color Calibration, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, shown as Autostretch with Linked channels in its view | 2026-11-10, AfterNight 5bd712a | `8c4d9da2c0633eeb016cf44690333426e0203066951ffe51dd6152305cfdbf9e` |
| `assets/images/processes/color-calibration/spectrophotometric-cc-after.webp` | The image after Spectrophotometric Color Calibration, the whole image, as its view shows it | A linear RGB master of the Carina Nebula, the maintainer's own data, shown as Autostretch with Linked channels in its view | 2026-11-10, AfterNight 5bd712a | `91cf4e418f7323e0666be25cfb5c6562b00bc3684339116c67c7180eb22c22da` |

### Calibration

| Picture | Shows | Data | Captured | SHA-256 |
| --- | --- | --- | --- | --- |

### Registration and Alignment

| Picture | Shows | Data | Captured | SHA-256 |
| --- | --- | --- | --- | --- |

### Stacking and Integration

| Picture | Shows | Data | Captured | SHA-256 |
| --- | --- | --- | --- | --- |
| `assets/images/processes/stacking-integration/image-integration-before.webp` | One registered light, a crop at 100 % of the middle of NGC 253 | One night of NGC 253 from a QHY183C colour camera - 35 lights, 20 darks, 40 flats and 30 biases - from the project's sample data, after Background Neutralization at its defaults and Histogram Transformation with Shadows 0.0200, Midtones 0.25 and Highlights 0.1100, shown as Linear | 2026-11-10, AfterNight 5bd712a | `e682bbe3a97c79a82a36d76d90b6cf53d5d9fc34d0b60b5df1a014673bb4dec6` |
| `assets/images/processes/stacking-integration/image-integration-after.webp` | The master of 18 lights, the same crop at 100 % of the middle of NGC 253 | One night of NGC 253 from a QHY183C colour camera - 35 lights, 20 darks, 40 flats and 30 biases - from the project's sample data, after Background Neutralization at its defaults and Histogram Transformation with Shadows 0.0200, Midtones 0.25 and Highlights 0.1100, shown as Linear | 2026-11-10, AfterNight 5bd712a | `0c2c4945ac1ba4ffa36d1a1ca03ce1640cb55eabf47f3a018f0f4cc6763eebca` |
| `assets/images/processes/stacking-integration/image-stacking-before.webp` | The first bias, a crop at 100 % of the middle of the frame | One night of NGC 253 from a QHY183C colour camera - 35 lights, 20 darks, 40 flats and 30 biases - from the project's sample data, after Histogram Transformation with Shadows 0.0270 and Highlights 0.0320, shown as Linear | 2026-11-10, AfterNight 5bd712a | `e66f79cd3303cb39969e5857a96e88b5100690bec585408746bf85f26e5096a1` |
| `assets/images/processes/stacking-integration/image-stacking-after.webp` | The master bias of 30, the same crop at 100 % | One night of NGC 253 from a QHY183C colour camera - 35 lights, 20 darks, 40 flats and 30 biases - from the project's sample data, after Histogram Transformation with Shadows 0.0270 and Highlights 0.0320, shown as Linear | 2026-11-10, AfterNight 5bd712a | `4e98443b624bb2cccf91a0d2f3c9bf4d21e240b51b76cf9e71e279c92d74c151` |
| `assets/images/processes/stacking-integration/integration-report.webp` | The top of a run's report: the contents, the header with its four figures and the first issues of Needs attention | Narrowband frames of the Eagle Nebula (Ha, OIII, SII) and their flats, from the project's sample data, stacked by Image Integration Lite at the defaults; the report of that run as Chrome shows it at 1,440 px in the dark theme | 2026-11-11, AfterNight 2125a3c | `14855656851c4c3958148a39ed6621206c7f9c6012a37d1f2a31392ece42885b` |
| `assets/images/processes/stacking-integration/integration-report-master.webp` | The picture section of one master's file in a run's report: the stretched preview, the spread of its samples and its statistics | Narrowband frames of the Eagle Nebula (Ha, OIII, SII) and their flats, from the project's sample data, stacked by Image Integration Lite at the defaults; the report of that run as Chrome shows it at 1,440 px in the dark theme | 2026-11-11, AfterNight 2125a3c | `5af31a5376501ee49d1ca1c38868020cc7fafdaefc21dce814dd17c791ea8c89` |
| `assets/images/processes/stacking-integration/integration-report-lights.webp` | The chart of one master's lights in a run's report, on its FWHM measure | Narrowband frames of the Eagle Nebula (Ha, OIII, SII) and their flats, from the project's sample data, stacked by Image Integration Lite at the defaults; the report of that run as Chrome shows it at 1,440 px in the dark theme | 2026-11-11, AfterNight 2125a3c | `4b7462e8f54dcf2c988ab38f42b419f327c9aa2db1898c0d934c222f17d09c99` |

## Offline Runtime Policy

`docs/user-guide/mkdocs.yml` references Mermaid and MathJax through local assets
under `assets/javascripts/`. Mermaid fences use the project `an-mermaid` host,
which the local initializer renders from the trusted, repository-authored
source with strict security. MathJax loads its
fonts and font data from `assets/javascripts/mathjax/fonts/`, where
`mathjax-config.js` points it; its default is jsDelivr. The configuration also
turns semantic enrichment off, because its speech worker is made from a `blob:`
URL that a page opened from disk may not load, and gives screen readers
assistive MathML instead. Do not add CDN fallbacks to the documentation build;
installer-bundled documentation must remain usable when opened from disk
without network access, which `node build/scripts/render_user_guide.cjs
--offline` checks.
