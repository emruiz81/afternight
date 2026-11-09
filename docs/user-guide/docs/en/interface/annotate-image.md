---
kind: interface
summary: >-
  Makes a picture of a plate-solved image with its deep-sky objects, bright stars and coordinates named on it, ready
  to share or print.
---
# Annotate Image

<!--
A page renders no authoring evidence. Record its owner, source-review date, reviewed revision, dirty reviewed
sources, source files and tests, UI reachability, parameter, algorithm and example coverage, content state,
technical and user review, media state and last strict build in its row of
docs/user-guide/DOCUMENTATION_COMPLETION_TRACKER.md.
-->

## Purpose, audience, prerequisites, and expected result

**Annotate Image** makes a picture of a plate-solved image with its deep-sky
objects, bright stars and equatorial coordinates named on it, ready to share or
print. It is for anyone who wants to show what an image contains: the galaxies,
nebulae and clusters in the field, the names of its bright stars, which way is
north and how large the field is.

The image must carry a celestial World Coordinate System (WCS), the same
solution the [Astrometry Overlays](astrometry-overlays.md) need. A
[Plate Solve](../processes/registration-alignment/plate-solving.md) writes one.

The result is a picture - the image as the view shows it, with the annotations
drawn over it - written to a PNG, JPEG or TIFF file, or opened as a new image
in a view of its own. The image you annotate is never changed.

## Reachability and availability

| Item | Current behavior |
| --- | --- |
| Path | The **Annotate Image** button in a solved image view's toolbar, beside **Astrometry Info**; `Ctrl+Shift+L` in a focused view; or **Image > Information > Annotate Image...** for the active view. |
| Enabled/visible when | The toolbar button is visible and the menu entry enabled only for a still image (not a sequence) whose metadata holds a celestial WCS the engine reads. |
| Supported inputs/state | Grey or colour still images of any sample type, linear or nonlinear, with a tangent-plane WCS, with or without SIP distortion. |
| Outputs/side effects | A PNG, JPEG or TIFF file, or a new view named `<image>_annotated`. The annotated image, its metadata and its history are not changed. |
| Persistence | Every choice except the title is kept in the global setting `annotation/settings` for the next window. The folder of the last export is remembered. |
| External dependencies | None. The deep-sky and bright-star catalogues are bundled with AfterNight and read offline. |

The window opens once per view: asking for it again raises the open window. It
closes with its view.

## Before you start

- Plate solve the image, or open an image whose header carries a WCS. The
  window says so when the image has none.
- Stretch the image in the view as you want it shown. With **Image: As the view
  shows it** the picture takes the view's display stretch, colour balance and
  channel linking; nothing about the image itself changes.
- A full-size picture of a large image holds four bytes a pixel while it is
  drawn: a 40-megapixel image needs about 160 MB for the picture alone. A smaller
  **Size** draws a smaller picture.
- The **Credit** text is drawn on the picture. Leave it empty if the picture is
  to be shared without a name.

## Procedure

1. Open the window from the view's **Annotate Image** button. The preview fills
   with the image while the catalogues are read; the line under it counts the
   objects found and how many of them are labelled.
2. Choose a **Level of detail**: **Essential** for the objects everyone knows,
   **Standard** for a balanced picture, **Detailed** or **Complete** for a dense
   chart. The preview is laid out again for every change; a small *Updating...*
   badge shows while a new layout is on its way.
3. Turn layers on or off: the coordinate grid, the deep-sky objects, the stars,
   the compass, the scale bar and the info panel.
4. Under **Style**, choose a **Palette**. Under **Text**, choose the font, its
   weight and the **Label size**; under **Lines**, the **Line width**, whether
   objects are filled and how the stars are marked. The text and the lines are
   set apart: no choice of one changes the other. The preview zooms and moves
   as an image view does - the mouse wheel, a middle-button drag, and its
   **Fit To Window**, **Zoom In**, **Zoom Out** and **Zoom 100%** buttons - and
   holding the left button opens the sample lens. The labels stay sharp at
   every zoom.
5. Under **Info Panel and Placement**, type a **Title** - empty takes the target
   the image header names - and a **Credit**, choose the facts the panel states
   and the corners of the panel, the compass and the scale bar.
6. Under **Output**, choose the picture's **Size**, how the **Image** is shown
   and the **Background**.
7. Click **Export...** and choose a PNG, JPEG or TIFF file, or click **Open as
   Image**. A progress window shows while the picture is drawn; the banner then
   says where it was saved or which view it opened in.

## Complete control and action reference

| UI label/action | Type and unit | Default | Valid values/constraints | Effect | Enabled, hidden, or ignored rules | Trade-off or side effect | Scope | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Level of detail | Four exclusive buttons | Standard | Essential, Standard, Detailed, Complete | Sets the catalogues, kinds, magnitude limits, smallest object and star limit as the table below states. | None is highlighted while the values below are your own (*Custom*). | More detail adds labels; crowded fields leave some out. | Picture | `annotation/settings` |
| Coordinate grid | Checkbox | On | On/Off | Draws lines of right ascension and declination, labelled where they meet the picture's edge. | - | - | Picture | `annotation/settings` |
| Deep-sky objects | Checkbox | On | On/Off | Marks and labels galaxies, nebulae and clusters from the bundled catalogue. | - | - | Picture | `annotation/settings` |
| Stars | Checkbox | On | On/Off | Marks and labels the bright stars **Faintest star**, **Star labels** and **Mark the stars left unnamed** choose. | - | - | Picture | `annotation/settings` |
| Compass | Checkbox | On | On/Off | Draws arrows toward celestial north and east, as the solution places them in that corner. | - | - | Picture | `annotation/settings` |
| Scale bar | Checkbox | On | On/Off | Draws a bar of a round angular length near a sixth of the picture's width, labelled such as `20′`. | - | - | Picture | `annotation/settings` |
| Info panel | Checkbox | On | On/Off | Draws a panel with the title, the credit and the facts chosen. | A panel with nothing to state is not drawn. | - | Picture | `annotation/settings` |
| Label text | Selector | Designation and name | Designation and name, Designation, Name | What a label says: `M 42` with `Orion Nebula` under it, the designation alone, or the name (the designation when there is none). | - | Two-line labels take more room. | Picture | `annotation/settings` |
| Palette | Selector with swatches | Aurora | Aurora, Moonlight, Glacier, Amethyst, Amber, Verdant, Rose, Atlas, Ember, Ink, Custom | Sets every colour. **Custom** is your own colours: chosen, it brings them back and opens **Colours**; the first time it starts from the palette shown, and setting any colour makes the palette custom. Choosing another palette keeps your colours for the next time you choose Custom. Aurora gives each kind of object its own colour; Moonlight draws in white; Glacier, Amethyst, Amber, Verdant and Rose keep to blues, violets, golds, greens and pinks, each kind of object a shade of its own; Atlas takes a printed star atlas's colours - red galaxies, green nebulae, yellow clusters; Ember is red for dark-adapted eyes; Ink draws dark lines on a light halo for bright images or print. The swatch beside each shows its colours for galaxies, nebulae, planetary nebulae and open and globular clusters. | Shows *Custom* once a colour is changed. | - | Picture | `annotation/settings`, your own colours with it |
| Font | Font list | The system's sans-serif face | The installed scalable fonts | The typeface of every text on the picture: the labels, the grid's labels, the compass, the scale bar and the panel. | A typeface missing where the settings are read again is replaced by the system's. | - | Picture | `annotation/settings` |
| Weight | Selector | Regular | Light, Regular, Medium, Bold | How heavy the letters are; a designation and the title are drawn a step heavier than the rest. | - | - | Picture | `annotation/settings` |
| Italic labels | Checkbox | Off | On/Off | Slants the objects' and stars' labels. | The grid's labels and the panel stay upright. | - | Picture | `annotation/settings` |
| Colour | Selector | Each object's colour | Each object's colour, The text colour | Draws each label in its object's colour, or every label in the palette's text colour. | - | - | Picture | `annotation/settings` |
| Backing | Selector | Halo | Halo, Badge, Plain | A soft outline around each letter, a rounded box behind the label, or the letters alone. | Only the letters: the lines keep their own dark edge. | A badge covers more of the image. | Picture | `annotation/settings` |
| Label size | Slider, percent | 100 % | 50-250 % | Scales the labels, the grid's labels and the compass and scale bar. 100 % sets a label's height to 1.4 % of the image's reference length (the square root of its width times its height). | Never changes a line's width. | Larger text leaves fewer labels room in a crowded field. | Picture | `annotation/settings` |
| Panel size | Slider, percent | 100 % | 50-200 % | Scales the info panel's text, as a share of the label size. | - | - | Picture | `annotation/settings` |
| Objects | Selector | Outlines | Outlines, Brackets | Marks a deep-sky object by its outline - its catalogue size at its position angle - or by four corner brackets. | An object that fills the frame is always drawn as a dashed outline. | - | Picture | `annotation/settings` |
| Stars (Lines) | Selector | Rings | Rings, Reticles | Marks a star by a ring around it, or by four short ticks pointing at it that leave the star itself clear. | - | - | Picture | `annotation/settings` |
| Line width | Slider, percent | 100 % | 50-300 % | Scales every line: outlines, star markers, grid, leader lines, compass and scale bar. | Never changes the text. | - | Picture | `annotation/settings` |
| Line opacity | Slider, percent | 100 % | 10-100 % | How opaque the outlines, star markers and leader lines are. | The grid has its own opacity; the compass and scale bar stay opaque. | - | Picture | `annotation/settings` |
| Fill objects with their colour | Checkbox | Off | On/Off | Tints the area of every outlined galaxy, nebula and cluster in its colour, as the view's deep-sky annotations do. | Disabled with **Brackets**. An object whose outline spans more than half the picture is left unfilled, so it does not tint the whole image. | At a high opacity the tint hides some of a small object. | Picture | `annotation/settings` |
| Fill opacity | Slider, percent | 15 % | 0-100 % | How opaque that tint is. | Enabled while **Fill objects with their colour** is on. | - | Picture | `annotation/settings` |
| Grid opacity | Slider, percent | 45 % | 0-100 % | How opaque the grid's lines are; its labels are drawn more opaque. | - | - | Picture | `annotation/settings` |
| Dark edge behind lines | Checkbox | Off | On/Off | Draws a soft edge in the halo colour behind every line, so it reads over a bright part of the image. | Independent of the labels' **Backing**. | - | Picture | `annotation/settings` |
| Leader lines for crowded labels | Checkbox | On | On/Off | Lets a label that finds no room beside its object sit further away, joined to it by a line. | - | Off leaves more labels out of crowded fields. | Picture | `annotation/settings` |
| Mark objects whose labels do not fit | Checkbox | On | On/Off | Keeps the outline or ring of an object whose label was left out. | - | - | Picture | `annotation/settings` |
| Colours | Fourteen colour buttons | The palette's | Any colour with opacity | Sets one colour: grid, each kind of object, stars, text, halo, compass and scale, panel, panel text, accent. | - | - | Picture | `annotation/settings` |
| Title | Text | Empty | Any text | The panel's title. Empty takes the target the header's `OBJECT` card names, else the most prominent named object; the field shows that as its placeholder. | - | - | Picture | Not kept |
| Credit | Text | Empty | Any text | A line under the title, such as your name. | - | Drawn on the picture. | Picture | `annotation/settings` |
| Center, Field and scale, Optics, Camera, Filter, Date, Integration | Checkboxes | On | On/Off | The facts the panel states, each only when the header states it: the field centre; the field's size, pixel scale and rotation; `TELESCOP` with `FOCALLEN` and `FOCRATIO` or `APTDIA`; `INSTRUME`; `FILTER`; the date of `DATE-OBS`; `TOTEXP` or `EXPTIME` with `STACKCNT` or `NCOMBINE` frames. | - | - | Picture | `annotation/settings` |
| Panel, Compass, Scale bar | Selectors | Bottom left, Top right, Bottom right | The four corners | The corner each decoration takes; decorations that share a corner are stacked inward from it. | - | - | Picture | `annotation/settings` |
| Messier, Caldwell, NGC, IC | Checkboxes | On | On/Off | The catalogues objects are taken from. | - | - | Picture | `annotation/settings` |
| Only named NGC and IC objects | Checkbox | Off | On/Off | Keeps an NGC or IC object only when it has a common name. | Set by Essential. | - | Picture | `annotation/settings` |
| Galaxies, Nebulae, Planetary nebulae, Open clusters, Globular clusters, Other entries | Checkboxes | On, except Other entries | On/Off | The kinds of object marked. *Other entries* are the catalogue's stars, asterisms, knots and entries of no known kind. | Other entries is set by Complete. | - | Picture | `annotation/settings` |
| Faintest magnitude, Any | Number and checkbox | 12, Any off | -2 to 20 | Leaves out objects fainter than the limit; **Any** marks every magnitude. | - | - | Picture | `annotation/settings` |
| Objects of no measured magnitude | Checkbox | Off | On/Off | Marks objects no catalogue measured; a named object is always marked. | - | - | Picture | `annotation/settings` |
| Smallest object | Number, percent | 0.30 % | 0-5 % | Leaves out objects whose major axis is smaller than this share of the image's reference length. Messier, Caldwell and named objects are kept whatever their size. | Objects of no stated size are judged by magnitude alone. | - | Picture | `annotation/settings` |
| Faintest star | Number, magnitude | 4.5 | -2 to 9 | The faintest star marked. | The bundled star catalogue stops at magnitude 9. | A faint limit adds many stars to a wide field. | Picture | `annotation/settings` |
| Star labels | Selector | Names and designations | Proper names; Names and designations; Every star, by number when unnamed | Which stars are named and by what: a proper name such as `Sirius`; also a Bayer or Flamsteed designation such as `η Car` or `61 Cyg`; or every star, one with neither named by its catalogue number such as `HIP 52468`, as the view's star labels are. | - | Every star adds many labels to a wide field. | Picture | `annotation/settings` |
| Mark the stars left unnamed | Checkbox | Off | On/Off | Marks the stars **Star labels** gives no name, without a label. | Set by Detailed. | - | Picture | `annotation/settings` |
| Grid spacing | Selector | Normal | Coarse, Normal, Fine | About four, six or ten lines across the field, at round steps of time and arc. | - | - | Picture | `annotation/settings` |
| Label the grid at the edges | Checkbox | On | On/Off | Labels each grid line where it meets the picture's edge. | - | - | Picture | `annotation/settings` |
| Size | Selector | Full size | Full size, or a long edge of 4096, 3840, 2560, 2048, 1600, 1280 or 1024 pixels below the image's own | The picture's size; it keeps the image's aspect ratio and is never larger than the image. | - | A smaller picture is drawn faster and holds less memory. | Picture | `annotation/settings` |
| Image | Selector | As the view shows it | As the view shows it, As stored, unstretched | Presents the image under the annotations with the view's display stretch, or every channel at full scale with nothing stretched. | A linear image *as stored* looks dark. | - | Picture | `annotation/settings` |
| Background | Selector | The image | The image, Transparent: annotations only | Draws the annotations over the image, or alone over transparent pixels to lay over the image in another program. | JPEG cannot hold a transparent picture. | - | Picture | `annotation/settings` |
| Reset | Button | - | - | Returns every choice to its default and keeps the credit and your own colours. | - | - | Window | Saved as changed |
| Open as Image | Button | - | - | Draws the picture and opens it in a new view. | Disabled until the preview is ready and while a picture is drawn. | - | New view | - |
| Export... | Button | - | PNG, JPEG, TIFF | Draws the picture and writes it to the file chosen, as the export settings write that format. | Disabled until the preview is ready and while a picture is drawn. | Overwrites the file chosen. | File | Remembers the folder |
| Close | Button | - | - | Closes the window and keeps the choices. | - | - | Window | - |

The levels of detail set these values:

| Level | Deep-sky objects | Faintest magnitude | Smallest object | Other entries | Stars |
| --- | --- | --- | --- | --- | --- |
| Essential | Messier, Caldwell, and named NGC and IC objects | Any | Any size | Off | Proper names to magnitude 2.5 |
| Standard | Every catalogue; objects of no measured magnitude only when named | 12 | 0.30 % | Off | Names and designations to magnitude 4.5 |
| Detailed | Every catalogue | 14 | 0.12 % | Off | Names and designations to magnitude 6, the unnamed stars marked |
| Complete | Every catalogue | Any | Any size | On | Every star to magnitude 9, by catalogue number when unnamed |

## Behavior and state model

**One drawing at every size.** Every length - a label's height, a line's width,
a marker's radius, the margins - is a fraction of the image's reference length,
the geometric mean of its width and height. The picture at full size, a smaller
picture and the preview are therefore the same drawing at different scales:
what the preview shows is where the picture puts it.

**What is marked.** The catalogues are read once for the solution, for the
field's radius and two degrees more, so a large object centred outside the frame
is found. An entry is kept when its catalogue and kind are chosen, it passes the
magnitude and size limits, and some of it lies in the frame. Entries the
catalogues list twice for one object - `M 42` and `NGC 1976`, `NGC 3372` and
`C 92` - become one mark under the designation people use most: Messier, then
NGC, then IC, then Caldwell. A star is kept when it is bright enough, lies in the
frame and **Star labels** names it - by its proper name, its Bayer or Flamsteed
designation, or its catalogue number - or **Mark the stars left unnamed** asks
for it.

**Where labels go.** Labels are placed one by one, the most prominent object
first: Messier, Caldwell and named objects before others, larger and brighter
ones before smaller and fainter ones. Each label tries the places beside its
object in a fixed order and takes the first one that stays inside the picture
and covers no other label and no decoration. A place that covers a smaller
object or crosses another object's outline is taken only when no leader line
reaches a free one: a label with no room beside its object sits further away on
a leader line, and one that still finds no room is left out, and the line under
the preview counts it. An object
larger than much of the frame is labelled inside itself and its outline is
drawn dashed, since it is a region rather than an object.

**The grid.** The spacing is a round step - seconds, minutes and hours of right
ascension, arcseconds, arcminutes and degrees of declination - chosen to draw
the number of lines **Grid spacing** asks for across the field. A field that
holds a celestial pole draws every hour of right ascension. Lines are labelled
just inside the picture's edge: declinations at the left edge and right
ascensions at the bottom when the lines meet them, another edge otherwise.

**The compass and the scale bar** are measured where they are drawn, so they
stay right near a pole and in wide fields.

**The preview** is laid out again on a worker for each change of what is shown
or how labels are placed; a change of colour only draws again. The image under
it is the view's own image, shown - not copied - in a compact image view, so it
is presented and drawn exactly as the [Image Viewer](image-viewer.md) shows an
image at every zoom. Its toolbar offers the zoom alone: the stretch is the
window's **Image** choice - as the view shows it, or as stored - and the
picture is stretched as the preview shows it. A transparent picture is
previewed over a checkerboard.

## Inputs, outputs, and metadata

- **Input:** the view's image and its WCS. The image is read, never written.
- **Exported file:** PNG, JPEG or TIFF, 8-bit RGB - RGBA for a transparent
  background - written as **Settings > Export** writes that format: its JPEG
  quality, TIFF compression and whether a raster file carries the image's
  metadata.
- **Opened image:** a new view named `<image>_annotated`, holding 8-bit display
  RGB recorded as nonlinear. It keeps what still describes it: the telescope,
  camera, site, pointing, date and integration cards of the image, and its WCS
  moved to the picture's size, so it can be plate solved, measured and annotated
  again. It starts its own history.
- **Neither** writes anything into the annotated image, its history or its
  file.

## Performance and responsiveness

The catalogue query, the layout, the preview's image and the picture each run
on a worker; the window never waits for them. The layout of a field of a few
hundred objects takes tens of milliseconds. A picture takes about as long as
presenting the image at that size plus a fraction of a second to draw: on a
16-thread desktop a full-size picture of a 7246 x 5429 colour image is drawn
and written to an uncompressed TIFF file in about half a second; the
compression the export settings choose adds its own time. Its pixels are
counted against the session's display-buffer memory while it is drawn. A
smaller **Size** is faster and lighter.

## Worked example

- Evidence mode: Source-and-test-derived, with a project-owned solved image.
- Starting state: a 7246 x 5429 RGB composite of the Carina Nebula, plate solved
  at 1.10″ a pixel, shown with the view's automatic stretch.
- Actions: open **Annotate Image**, keep **Standard**, type a credit, choose
  **Complete**.
- Checkpoints: at Standard the preview marks NGC 3372, the Carina Nebula, as a
  dashed region labelled inside itself, which **Fill objects with their colour**
  leaves unfilled; at Complete it names every star to magnitude 9: Eta Carinae
  as `η Car`, at the catalogue's magnitude 6.2, and the others by their HIP
  numbers, the labels that find no room left out and counted. The panel states the centre `10h 44m 31s
  -59° 53′ 32″`, the field `2.20° × 1.65°`, the scale and the rotation. A
  full-size uncompressed TIFF export completes in about half a second.
- Limitations: Trumpler 14 and 16, the Keyhole and other objects of catalogues
  other than NGC, IC, Messier and Caldwell are not in the bundled catalogue and
  are not named.

## Warnings and limitations

- The picture is display-referred 8-bit data with the annotations burned in.
  Keep the original image for further processing.
- Objects come from the bundled NGC, IC, Messier and Caldwell catalogue; other
  catalogues (Sharpless, Barnard, Collinder, Trumpler...) are not included.
  Catalogue sizes and position angles are approximations, and some Caldwell
  rows have the provenance limitation recorded in
  [Astrometry Overlays](astrometry-overlays.md#references-and-provenance).
- Star positions are J2000 without proper motion; they are drawn where the
  catalogue puts them.
- In a crowded field some labels are left out rather than overlapped. Lower the
  **Label size**, turn **Leader lines** on, or choose a lower level of detail.
- An export overwrites the file chosen without asking again once the file dialog
  confirmed it.
- The credit and title are drawn into the picture.

## Troubleshooting

| Symptom or exact error | Verified cause/enablement path | Recovery | Evidence source |
| --- | --- | --- | --- |
| The **Annotate Image** button is missing, or the menu entry is disabled. | The view is a sequence, or its image has no WCS the engine reads. | Plate solve the image, or open a solved file. | Window and menu tests |
| *The image states no astrometric solution. Plate solve it first.* | The image lost its WCS while the window was open. | Plate solve it again; the window starts over. | Window tests |
| *The catalogues could not be read: ...* | The bundled catalogue resource could not be read. | Reinstall AfterNight. | Window source |
| *A JPEG file cannot keep transparency: choose a PNG or TIFF file.* | **Background** is **Transparent** and the file chosen is JPEG. | Choose a PNG or TIFF file, or the image background. | Window tests |
| *Choose a PNG, JPEG or TIFF file.* | The file chosen has another extension. | Choose a `.png`, `.jpg` or `.tif` file. | Window source |
| *The image is being changed; try again once it is shown.* | A process was writing the image when the picture was asked for. | Wait for the view to update and export again. | Window source |
| *The picture could not be made: ...* | The engine refused a step - memory, the file, the format. The message names it. | Choose a smaller **Size** or another folder, and try again. | Window source |
| Some objects are marked but not labelled. | Their labels found no room. | Lower **Label size**, turn on leader lines, or choose less detail. | Layout tests |
| Stars are ringed but not named. | **Mark the stars left unnamed** marks the stars **Star labels** does not name. | Choose **Every star, by number when unnamed**, or turn the marking off. | Scene tests |

## Related workflow steps

- Before: [Plate Solving](../processes/registration-alignment/plate-solving.md)
  writes the WCS this window needs; [Astrometry Overlays](astrometry-overlays.md)
  check it in the view, naming the sky at this window's levels of detail, label
  texts and palettes, chosen in the view's own menu.
- After: [Saving & Exporting](saving-and-exporting.md) saves an opened picture in
  any format, and the [Image Viewer](image-viewer.md) shows it.

## References and provenance

- NASA/HEASARC, [FITS World Coordinate System papers and resources](https://fits.gsfc.nasa.gov/fits_wcs.html),
  for the tangent-plane projection through which every position is drawn.
- CDS/VizieR, [NGC 2000.0 catalog VII/118](https://cdsarc.cds.unistra.fr/viz-bin/cat/VII/118),
  the primary input of AfterNight's bundled deep-sky catalogue; Messier aliases,
  common names and selected extents are project-maintained additions.
- Astronexus, [HYG Database 4.2](https://www.astronexus.com/projects/hyg),
  source of the bundled bright-star subset - the 83,479 stars to visual
  magnitude 9 - under CC BY-SA 4.0.
- The drawing, label placement and palettes are AfterNight's own.
