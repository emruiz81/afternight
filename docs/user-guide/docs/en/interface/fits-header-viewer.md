---
kind: interface
summary: >-
  Three windows show what an image's file stated: FITS Header lists its header cards, EXIF Viewer its camera fields,
  and Astrometry Info sums up its plate solution. None of them changes anything.
menu:
  - Image > Information > FITS Header
  - Image > Information > EXIF Viewer
  - Image > Information > Astrometry Info
---
# FITS Header, EXIF, And Astrometry Metadata

Three windows show what an image carries beside its pixels. **FITS Header** lists its
[header cards](../reference/glossary.md#header-card) as the file stated them, **EXIF Viewer** the fields a camera or a
raster file wrote, and **Astrometry Info** sums up its [plate solution](../reference/glossary.md#plate-solution). They
read what AfterNight keeps in memory, which the file's reader stored when it opened the file. None of them edits a
card, a field, the pixels or the [history](../reference/glossary.md#history), and none of them marks the image as
changed. For the image's size and format, open [Image and Sequence Summary](image-summary.md) instead.

```figure
owed: true
image: ../assets/images/interface/fits-header-viewer.webp
alt: >-
  The FITS Header Viewer window. Under the title bar, the heading FITS Header and a line about keyword, value and
  comment records. A card titled Header Keywords holds a table with the columns Key, Value and Comment: SIMPLE, T and C#
  FITS on the first row, then BITPIX 16, NAXIS 2, NAXIS1 5544, NAXIS2 3684, BZERO 32768, EXTEND, IMAGETYP with the
  quoted value LIGHT, EXPOSURE 180.0 with its comment in seconds, and more rows below. Close stands at the bottom
  right.
caption: >-
  The FITS header of a raw NGC 253 light frame, as the file states it.
pins:
  - {n: 1, x: 63.0, y: 3.0}
  - {n: 2, x: 14.0, y: 29.0}
  - {n: 3, x: 33.0, y: 29.0}
  - {n: 4, x: 65.0, y: 29.0}
  - {n: 5, x: 81.0, y: 93.5}
```

## What you see

1. **The title.** It names the view, as `FITS Header Viewer <M42>`.
2. **Key.** Each card's keyword, in the order the file states them.
3. **Value.** The value as the file stores it. A string keeps its quotes, as `'LIGHT'`.
4. **Comment.** The text after the value's slash.
5. **Close.** Closes the window.

## What you can do

### Read the FITS header

1. Make the view active. For a [sequence](../reference/glossary.md#sequence), pause the player on the frame you want.
2. Choose **Image > Information > FITS Header**, choose **FITS Header** on the view's toolbar, or press
   ++ctrl+shift+h++ over the view.
3. Scroll the table, and drag a heading's edge to widen a column. Click a row to mark it while you compare.

On the NGC 253 [light frame](../reference/glossary.md#light-frame), the table opens on `SIMPLE`, `BITPIX` 16 and
`NAXIS` 2, and `DATE-LOC` and `DATE-OBS` lie a few rows down. On a solved image, look for the
[WCS](../reference/glossary.md#wcs) cards - `CTYPE1` and `CTYPE2`, `CRPIX1` and `CRPIX2`, `CRVAL1` and `CRVAL2`, the
`CD` or `PC` and `CDELT` matrix - and any `A` and `B` or `AP` and `BP` cards of [SIP](../reference/glossary.md#sip)
distortion. Then compare them with **Astrometry Info**.

### Read the camera's EXIF fields

Choose **Image > Information > EXIF Viewer**, **EXIF Viewer** on the toolbar, or press ++ctrl+shift+x++. The window
lists the image's [EXIF](../reference/glossary.md#exif) record, one row for each field: its **Tag**, its **Value** and
its full **Key**, such as `Exif.Photo.ExposureTime`. The window is offered only for a JPEG, TIFF, PNG or camera
[RAW](../reference/glossary.md#raw) file that stated a field it shows.

### Read the plate solution

Choose **Image > Information > Astrometry Info**, **Astrometry Info** on the toolbar, or press ++ctrl+shift+p++. A
line says whether the image has a solution, and eleven fields sum it up.

```figure
owed: true
image: ../assets/images/interface/fits-header-viewer-astrometry-info.webp
alt: >-
  The Astrometry Info window. Under its heading and a line of description, the words Astrometric solution available,
  then a card of eleven rows: Field center RA 10:44:30.67, Field center Dec -59:53:31.7, CRVAL RA 10:44:30.67, CRVAL Dec
  -59:53:31.7, Image scale 1.095 arcsec/px, Rotation 107.7° (N through E), Field of view 132.22′ × 99.16′, Projection
  RA---TAN-SIP / DEC--TAN-SIP, Equinox ICRS, SIP distortion A/B 4/4, 14 coeffs, and Solver Unknown. Close stands at the
  bottom right.
caption: >-
  Astrometry Info for the plate-solved Carina master.
```

**Field center RA** and **Field center Dec** say where the middle of the image points, and **CRVAL RA** and **CRVAL
Dec** where the solution's reference pixel points. The two differ unless the reference pixel is the middle, as it is
on the Carina [master](../reference/glossary.md#master). The window follows the
[active image](../reference/glossary.md#active-image): choose another view and it describes that one.

### Keep the metadata when you save

A [FITS](../reference/glossary.md#fits) or [XISF](../reference/glossary.md#xisf) save writes the cards back, and a
TIFF, PNG or JPEG save carries the EXIF fields while **Embed EXIF** is on. Each format keeps a different part, so open
the saved file again and read its header before you delete the original. [File Formats](../reference/file-formats.md)
says what each format keeps.

Inside AfterNight, **Image > Actions > Duplicate** copies every card and field into a new image that has never been a
file. An image combined from several, by Image Blend, LRGB Combination or another combining process, keeps the first
source's pointing and solution cards, the earliest source's observation start, and any other card only when every
source states it alike. Its `EXPTIME` and `TOTEXP` state the sources' distinct exposures added up, in place of theirs.
A change of geometry adjusts the solution or removes it, as its own page says.

## Controls

### FITS Header

```controls
window: FITS Header
controls:
  - name: Title
    kind: readout
    default: ""
    effect: Names the view the table belongs to, as `FITS Header Viewer <M42>`, and follows a rename.
    enabled: The menu entry and the button are enabled while the image carries at least one FITS card, whatever the file's extension. While a window is open, that view's menu entry is disabled until a FITS Header window of it closes. The button is not, so each press opens another window. ++ctrl+shift+h++ opens a window even when the entries are disabled, then empty.
    tradeoff: While an edit holds the image, the view may find no card and disable the entries until the edit is done. With two windows open, closing either enables the menu entry again.
    persistence: Nothing of the window is kept, neither the column widths nor the row marked.
    note: The window stays open beside your work, and closes with its view. None of the three windows reads the pixels, a catalog or the network.
  - name: Key, Value and Comment
    kind: readout
    default: ""
    effect: One row for each card, in the file's order, with no sorting and no separator between header units. A string continued over `CONTINUE` cards is one row with the whole value. `HISTORY` and `COMMENT` cards are rows like the others, and a card with a blank keyword is not shown. A `HISTORY` card is a line of the file's header, not a step of the image's history.
    range: A FITS file gives the cards of the image it opened. An image inside an extension adds the primary header's cards it does not restate, apart from the structural ones, and all its `HISTORY` and `COMMENT` cards. An XISF file gives the FITS keywords it carries, and a process can add or update cards too. An image holds at most 8,192 cards of at most 4,096 bytes each.
    enabled: A row can be marked, never edited. A click on a heading does not sort. The last column stretches with the window.
    tradeoff: It is a table of the cards, not the 80-character records of the file. It has no search, filter, copy or export. The cards are what the file claims, and can be stale, wrong or private, such as an observer's name or the site's latitude and longitude.
    scope: The view the window was opened from, and the frame it shows.
    persistence: Built again when the view's image loads or changes, a new frame among them, and empty when the image carries no FITS card. A long header rebuilds slowly, so pause a playing sequence or close the window when you do not need it. While an edit holds the image, the window keeps what it shows until the edit is done; it never waits for it. There is no refresh button.
    note: The processing-state cards AfterNight writes, such as `ANLIN`, are quoted strings. Writing one replaces the first card of its keyword, in its place and with its comment, and removes the later ones. A long value, such as a color-calibration record's, is saved over `CONTINUE` cards and shows as one row.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window and enables the view's **FITS Header** menu entry again.
    range: ++enter++ and the window's own close button do the same.
```

### EXIF Viewer

```controls
window: EXIF Viewer
controls:
  - name: Title
    kind: readout
    default: ""
    effect: Names the view, as `EXIF Header Viewer <M42>`, and follows a rename. The heading inside reads **EXIF Metadata**.
    enabled: The menu entry and the button are enabled while the image carries an EXIF field the window shows, from a JPEG, TIFF, PNG or camera raw file. A container's own entries, such as a PNG's bit depth or a JPEG comment, do not count. While a window is open, its menu entry is disabled and its button is not, as for FITS Header. ++ctrl+shift+x++ opens a window even when the entries are disabled, then empty.
    range: A camera raw file in a TIFF structure - NEF, DNG, ARW, PEF, SRW, CR2, ORF, RW2 - gives its fields, and a RAF file those of its embedded preview. A CR3, CRW, MRW or X3F file gives none.
    persistence: Nothing of the window is kept.
  - name: Tag, Value and Key
    kind: readout
    default: ""
    effect: One row for each field, in the order the file stated them. **Tag** is the last part of the key, **Value** the value as the Exiv2 library renders it, and **Key** the full key, such as `Exif.Photo.ExposureTime`.
    range: A value longer than one card is shown whole. The window leaves out the maker note, the interoperability fields, the strip offsets, the subfile type, the embedded IPTC and Photoshop blocks, the color profile, the thumbnail and a field without a name.
    tradeoff: It is a table to read, not a copy of the record. It does not show the library's description of a field, and has no sorting, copy or export. A field missing here may still be in the file.
    scope: The view the window was opened from, and the frame it shows.
    persistence: Built again as the FITS Header table is, and empty when the image carries no EXIF field.
    note: An embedded XMP packet is removed before the fields are read, so it is never shown and a later save does not carry it. The other fields are written back by a TIFF, PNG or JPEG save while **Embed EXIF** is on; a field the library cannot write is skipped.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window and enables the view's **EXIF Viewer** menu entry again.
    range: ++enter++ and the window's own close button do the same.
```

### Astrometry Info

```controls
window: Astrometry Info
controls:
  - name: Status
    kind: readout
    default: No astrometric solution
    effect: Reads "Astrometric solution available", "No astrometric solution" or, with no image active, "No active image".
    enabled: The menu entry is enabled for any image, a sequence's frame among them, so it can show an unsolved one. The toolbar button shows for a still image and is enabled when it is solved. ++ctrl+shift+p++ works when the button does and is not in the toolbar's **More** menu.
    tradeoff: '"No astrometric solution" does not say which card is missing or wrong, and "Astrometric solution available" says nothing of how good the solution is.'
    scope: The active image. The window follows the active view, and its title names it, as `Astrometry Info <M42>`.
    persistence: There is one window for each view. Asking again through the view it was opened from brings that window forward; asking through another view opens a second one. It closes with the view it was opened from.
    note: The solution is read from the FITS cards by the one reader the overlays and Plate Solve use, and needs the ICRS and a tangent-plane projection. A header that states the J2000 equinox and no frame is taken as the ICRS. FK5 or any other frame, another equinox without `RADESYS = 'ICRS'`, another projection, a missing, malformed or repeated keyword and a singular matrix all leave no solution.
  - name: Field center RA and Field center Dec
    kind: readout
    default: "-"
    effect: Where the middle of the image points, in hours, minutes and seconds to a hundredth, and in signed degrees, minutes and seconds to a tenth.
    range: The right ascension runs from 0 to 24 hours. Each value is rounded once, so 10h 29m 59.996s shows as `10:30:00.00`, never as 60 seconds.
    note: The middle is half the width and half the height, with the first pixel's center at 0.5, projected through the solution.
  - name: CRVAL RA and CRVAL Dec
    kind: readout
    default: "-"
    effect: Where the solution's reference pixel points, in the same format.
    note: It is the middle of the image only when the reference pixel is.
  - name: Image scale
    kind: readout
    default: "-"
    effect: The side of a square pixel that covers the same sky as the solution's pixel, in arcseconds per pixel to three decimals.
    tradeoff: One number cannot describe a solution whose two axes have different scales.
    note: The scale, the rotation and the field of view come from the solution's matrix - the `CD` cards, or `PC` with `CDELT`, or `CDELT` with `CROTA2` - not from cards of their own.
  - name: Rotation
    kind: readout
    default: "-"
    effect: Which way celestial north points, at the reference pixel, from the direction of increasing y toward decreasing x, to a tenth of a degree, as `107.7° (N through E)`.
    tradeoff: It gives neither an uncertainty nor whether the image is mirrored.
  - name: Field of view
    kind: readout
    default: "-"
    effect: The sky across the image's width and height, in arcminutes to two decimals.
    tradeoff: A linear estimate from each axis's scale. It ignores the projection's curvature and the distortion.
  - name: Projection
    kind: readout
    default: "-"
    effect: Reads `RA---TAN / DEC--TAN`, or `RA---TAN-SIP / DEC--TAN-SIP` when the solution carries a distortion polynomial.
  - name: Equinox
    kind: readout
    default: "-"
    effect: The `EQUINOX` card as a year, such as `J2000.0`, or its text. Without one, the `RADESYS` frame, which reads `ICRS` for a solution AfterNight wrote, or `-` when neither is stated.
    enabled: Shown even when the image has no solution.
    tradeoff: It converts no coordinate to another epoch.
  - name: SIP distortion
    kind: readout
    default: None
    effect: The orders of the forward `A`/`B` and inverse `AP`/`BP` polynomials, and how many coefficients are larger than `1e-12` in size, as `A/B 4/4, 14 coeffs`.
    range: A polynomial is read with the solution. The projection must end in `-SIP` exactly when polynomial cards are present, `A` comes with `B` and `AP` with `BP`, every order lies from 2 to 6 and every coefficient within its order. A missing coefficient is 0. A header that breaks a rule shows no solution at all.
    tradeoff: It says nothing of how far the distortion moves a pixel, nor of the solve's residuals. Higher-order coefficients smaller than the limit are not counted, though they can still move a corner by pixels.
    note: '**None** means no polynomial this reader accepts, not that the header has no distortion cards.'
  - name: Solver
    kind: readout
    default: Unknown
    effect: The `PLTSOLVR` card, which AfterNight's plate solve writes.
    enabled: Shown even when the image has no solution.
    tradeoff: '**Unknown** does not mean unsolved: files solved elsewhere seldom carry the card.'
    note: A solution AfterNight writes states `RADESYS = 'ICRS'` and no `EQUINOX`, each number as the shortest decimal that reads back the same, and none of the solver's comments.
  - name: Close
    kind: button
    default: ""
    effect: Closes the window.
    note: The window is refreshed when the image loads or changes, and when another view becomes active. While an edit holds the image, **Equinox** and **Solver** read as unknown until it is done. Every label and value can be selected with the mouse.
```

## When it goes wrong

**FITS Header** is greyed
:   **Cause.** The image carries no FITS card, or an edit held it while the view refreshed its toolbar.
:   **Fix.** Wait for the edit to finish, or open the image from a FITS or XISF file.

**EXIF Viewer** is greyed
:   **Cause.** The image carries no EXIF field the window shows: none, only hidden ones, or a raw format whose EXIF
    AfterNight does not read.
:   **Fix.** Open the camera's own JPEG, TIFF or raw file, in a format the window reads.

A shortcut opens an empty window
:   **Cause.** ++ctrl+shift+h++ and ++ctrl+shift+x++ open their windows even when the image has nothing to show.
:   **Fix.** Close it, and trust the menu entry's state.

"No astrometric solution" although the header has WCS cards
:   **Cause.** The reader refused the cards: a frame other than the ICRS, a missing, malformed or repeated keyword,
    polynomial cards that disagree with the projection, or a singular matrix.
:   **Fix.** Read `CTYPE`, `CRPIX`, `CRVAL`, `RADESYS`, `EQUINOX`, the matrix and the polynomial cards in FITS Header,
    and solve the image again with Plate Solve.

The Astrometry Info fields changed by themselves
:   **Cause.** The window follows the active view, and another view became active.
:   **Fix.** Click the view you want, and check its name in the window's title.

A saved and reopened file lost some cards or fields
:   **Cause.** The format keeps only part of them, **Embed EXIF** was off, or the library could not write a field.
:   **Fix.** Keep the original, and save in FITS or XISF to keep every card.

## Related

- [Image and Sequence Summary](image-summary.md), for the size, format and acquisition facts of an image or a
  sequence.
- [Plate Solving](../processes/registration-alignment/plate-solving.md), to find or replace an image's solution.
- [Astrometry Overlays](astrometry-overlays.md), to draw the grid, the catalog objects and the stars of a solution.
- [Saving And Exporting](saving-and-exporting.md), for the formats and the **Embed EXIF** setting.
- [File Formats](../reference/file-formats.md), for what each format keeps of the cards and fields.
- [Image Statistics](image-statistics.md), for what the pixels measure rather than what the file claims.

## Credits

- The IAU FITS Working Group's [FITS Standard 4.0](https://fits.gsfc.nasa.gov/fits_standard.html) defines header
  cards, keywords, values and comments.
- NASA/HEASARC's [FITS WCS papers and resources](https://fits.gsfc.nasa.gov/fits_wcs.html) collect the
  world-coordinate conventions the solution is read with.
- Shupe and Hook's registered [SIP convention 1.0](https://fits.gsfc.nasa.gov/registry/sip/SIP_distortion_v1_0.pdf)
  defines the forward `A`/`B` and inverse `AP`/`BP` distortion polynomials.
- CIPA's [Exif standards list](https://www.cipa.jp/e/std/std-sec.html) names the Exif specification the camera
  fields follow.
- The [Exiv2](https://exiv2.org) library, under the GPL-2.0-or-later, reads the EXIF fields this window shows.
