---
kind: reference
summary: >-
  What the terms of the guide mean: astrophotography, calibration, stretching, color and file formats, and
  AfterNight's own ideas, such as the history, the preview area and the processing state.
---
# Glossary

Each entry says what a term means and, where it matters, what AfterNight does with it. The terms are in alphabetical
order under their first letter, and an abbreviation that pages use alone has a short entry of its own. A page of the
guide links a term here the first time it uses it. Where two terms are easy to confuse, such as a stretch and a
display stretch, their entries say which is which. A process, a window or a control is described on its own page:
find it in the [Process Catalog](../processes/index.md) or with the guide's search.

## A

### Active image

The image whose view the menu commands, the Process menu, masks and the viewer's tools act on. A process window
opens on the active image, and its **Target Image** box can then choose another open image.

### ADU

Analog-to-digital unit: the number the camera's electronics give a pixel. A camera's raw frames hold such numbers,
and AfterNight opens an integer FITS file or an undebayered RAW frame as [sensor codes](#sensor-code).

### Autostretch

The display mode of an image view that shows a [linear image](#linear-image) stretched, so that its faint signal can
be seen. The stretch is computed from the median and the [median absolute deviation](#median-absolute-deviation) of
every sample. It is a [display stretch](#display-stretch): the image, its history and a saved file stay as they were,
unless you confirm **Apply Stretch** in Autostretch Settings. See [Image Viewer](../interface/image-viewer.md).

### AVX2

A set of x86-64 processor instructions, with FMA, that AfterNight needs: the engine and every part of the
application are built for it, so a processor without it cannot run AfterNight. It is not a setting. On a processor
with the AVX-512 F, BW, DQ and VL subsets, the engine chooses its AVX-512 routines when it runs, where it has them.

## B

### Bayer pattern

The order of the red, green and blue filters in each 2 x 2 cell of a [CFA](#cfa), named by its first two rows: RGGB,
BGGR, GBRG or GRBG. A [debayer](#debayer) needs the right pattern to give the right colors.

### Bias frame

A frame of the shortest exposure the camera allows, taken in the dark, that records the electronic offset of its
readout. Calibration uses its [master](#master) to remove that offset.

### Black point

The input level that becomes black in a [stretch](#stretch): every sample at or below it is set to 0, and what it
held is lost. Histogram Transformation sets it with **Shadows**, ArcSinh Stretch with **Black Point**. See also
[White point](#white-point).

## C

### Calibration frame

A [bias](#bias-frame), [dark](#dark-frame), [flat](#flat-frame) or [dark-flat](#dark-flat) frame, taken to correct
the [light frames](#light-frame) for the camera's offset, its thermal signal and uneven illumination.

### CDF

Cumulative distribution function: for each level of a histogram, the count of samples at that level or below it.
Histogram equalization rescales it into the curve that maps each input level to an output level.

### CFA

Color filter array: the mosaic of red, green and blue filters over the pixels of a one-shot color sensor, so that
each pixel records one color. Its repeating cell is the [Bayer pattern](#bayer-pattern). AfterNight keeps a CFA image
as one channel, the raw mosaic, until a [debayer](#debayer) turns it into RGB.

### CIE Lab

A [color model](#color-model) in which equal steps look about equally large. L* is the lightness, from 0 for black to
100 for white, and a* and b* are two opposite color axes, green to red and blue to yellow. LCh states a* and b* as a
chroma and a hue angle instead. CIE L* is not the lightness of [HSL](#hsl).

### Cold pixel

A pixel that reads far darker than its neighbors in every frame, because it responds little or not at all. Like a
[hot pixel](#hot-pixel), it stays at one place on the sensor, and [rejection](#rejection) of
[dithered](#dithering) frames or Cosmetic Correction removes it.

### Color model

A way of stating a color as three numbers. RGB gives red, green and blue. [HSL](#hsl), [HSV](#hsv) and [HSI](#hsi)
give a hue, a saturation and a lightness, a value or an intensity. [CIE Lab](#cie-lab) gives a lightness and two color
axes, [YCbCr](#ycbcr) a luma and two color differences. Components of one name are not interchangeable: HSL
lightness, HSV value, CIE L* and luma are four different things. See also
[Component encoding](#component-encoding).

### Component encoding

How AfterNight stores a color component that is not a level of light, such as a hue, a chroma or CIE a*: as a value
from 0 to 1. A hue is a fraction of a turn, and a* is \((a^* + 128) / 255\). The component is held in 32-bit floating
point, or 64-bit for a 64-bit floating-point image, so an integer image's component is not rounded. No unit such as
degrees is stored.

### Continuum

The light a source gives across the whole spectrum, as stars and dust do, beneath the emission lines of a nebula. A
narrowband filter passes some continuum with its line. NBRGB Combination uses the passband widths you enter to take
the continuum out of each narrowband image before it combines the line with the RGB image.

### Crash dump

A Crashpad minidump AfterNight writes on the computer when it crashes, on Windows and Linux. AfterNight does not
upload it. It can hold what the application had in memory, file paths among it, and it is read with the symbols of
the exact build that wrote it. See [Troubleshooting](troubleshooting.md#crash-dumps).

## D

### Dark frame

A frame taken with the camera covered, matching the [light frames](#light-frame) in exposure time, gain, offset,
temperature and readout mode. It records the sensor's thermal signal and its [hot pixels](#hot-pixel). Gain is the
camera's amplification, which sets the signal's scale and dynamic range. Offset is the black level the camera adds so
that no sample is cut off at zero. Calibration subtracts the dark [master](#master).

### Dark optimization

Scaling the dark [master](#master) to match the thermal signal of each light. The factor, from 0 to 2, is the one
that leaves the least noise in the central 512 x 512 pixels. Only a one-channel light at least 512 pixels on each
side is optimized, an undebayered color frame among them. A color or smaller light gets the fixed factor, and the log
says so. Image Calibration and Image Integration's light calibration both do it.

### Dark-flat

A [dark frame](#dark-frame) that matches the [flat frames](#flat-frame) in exposure time and settings, instead of the
lights. It corrects the flats when they need a dark of their own.

### Debayer

To demosaic a [CFA](#cfa) image: to estimate, at every pixel, the two colors its filter did not record, keeping the
one it did, so that the one-channel mosaic becomes an RGB image. It needs the right
[Bayer pattern](#bayer-pattern). Calibration works on the mosaic, before the debayer.

### Deconvolution

Sharpening that undoes the blur a [PSF](#psf) describes, as far as the noise allows. It needs a
[linear image](#linear-image), so it comes before the stretch.

### Dilation

A [morphology](#morphology) operation that takes the largest value of each neighborhood: bright structures grow and
dark ones shrink. Its opposite is [erosion](#erosion).

### Display stretch

A stretch the image view applies to show an image, without changing it. The view's display modes are Linear, which
shows the samples as they are, and [Autostretch](#autostretch). Its **View Channel**, which shows one channel or one
color component, is a display choice too. None of them changes the samples, the metadata, the history or whether
the image counts as unsaved, and a save does not write them. Only a process Apply or **Apply Stretch** changes the
image.

### Dithering

Moving the telescope's pointing by a few pixels between [light frames](#light-frame), so that a defect of the sensor
falls on a different part of the sky in each. It keeps fixed patterns from adding up, lets [rejection](#rejection)
remove [hot pixels](#hot-pixel), and gives [drizzle](#drizzle) the offsets it needs.

### Dock workspace

The central area of the main window, which holds the image and sequence views and the Session Console. Creating or
opening a view, or opening the console, shows it in place of the Dashboard.

### Drizzle

Stacking that drops each frame's pixels, shrunk, onto a finer output grid instead of resampling them, so that
[dithered](#dithering), undersampled frames can give back some resolution. Image Integration and Image Integration
Lite offer it.

## E

### Erosion

A [morphology](#morphology) operation that takes the smallest value of each neighborhood: bright structures shrink
and dark ones grow. Its opposite is [dilation](#dilation).

### EXIF

The camera's record in a JPEG, TIFF, PNG or raw file: the camera model, the exposure, the ISO or gain, the date and
many other fields. AfterNight shows it in the EXIF Viewer, and a TIFF, PNG or JPEG save can carry it.

### Exposure stop

A doubling or a halving of the light: a change of \(s\) stops multiplies a linear signal by \(2^s\), so +1 doubles it
and -1 halves it. The value of Exposure Correction is not a number of stops.

### Extension host

A separate AfterNight process that runs an extension's Python code and its windows. A package whose backend is
`runtime` runs in the full host, with direct access to the image engine and to AfterNight's own controls. One whose
backend is `protocol` runs in the lite host, which loads no part of the image engine. See
[Using Extensions](../extensions/using-extensions.md).

## F

### FITS

Flexible Image Transport System, the astronomy image format: pixels with a header of [header cards](#header-card),
which can hold the [WCS](#wcs) and the [SIP](#sip) terms of a [plate solution](#plate-solution). An integer FITS file
opens as [sensor codes](#sensor-code), a floating-point one as [physical quantities](#physical-quantity).

### Flat frame

A frame of an evenly lit field, taken through the same optics and filter as the lights, that records vignetting, dust
shadows and the differences between pixels. Calibration divides the lights by its [master](#master).

### Flat normalization

Dividing the flat [master](#master) by a typical level of its own, so that the flat correction changes only how the
light is spread over the frame, and not the lights' overall level.

### Flux

The light of an object measured as the sum of its samples over the object or an aperture. Resampling a frame can
change that sum, unless the method conserves flux.

### Fraction of full scale

How the viewer and the processes read every sample: an unsigned integer divided by its largest value, a signed one
counted from its smallest value to its largest, a floating-point sample as it is. So 0 is black and 1 is white. A
process that changes an image in place writes its result back in the image's own [pixel type](#pixel-type), rounded
to the nearest code. See [Numeric domain](#numeric-domain).

### FWHM

Full width at half maximum: the width of a star's profile at half its peak, in pixels or arcseconds. It measures how
sharp the stars are: smaller is sharper.

## G

### Gaia

The European Space Agency's survey of the stars, whose catalog gives the position and brightness of more than a
billion of them. AfterNight's internal plate solver and Photometric Color Calibration read a Gaia pack you install in
the settings. Spectrophotometric Color Calibration reads Gaia XP spectra, the externally calibrated low-resolution BP
and RP spectra of Gaia DR3, from a catalog of its own.

### GHS

Generalized Hyperbolic Stretch: a family of stretch curves set by five values, a strength, a local intensity, a
[symmetry point](#symmetry-point) and two [protection points](#protection-point) for the shadows and the highlights.
Generalized Hyperbolic Transformation offers it among its curve families.

## H

### Header card

One record of an image's header: a keyword, a value and a comment, as a [FITS](#fits) file writes it. AfterNight
keeps the cards an image was read with, and a FITS or XISF save writes them back. A TIFF, PNG or JPEG save carries
those its format allows when **Embed EXIF** is on. The image's [processing state](#processing-state) and its
[WCS](#wcs) are header cards too.

### History

The steps of an image or a sequence view: each Apply or other edit adds one, and up to 20 states are kept. In the
Image History panel you can preview, restore or clone a step, remove steps and read their log. A history lasts as
long as its view: no file stores it. See [Image History](../interface/image-history.md).

### Homography

A projective transform between two frames: it maps any four points to four others, so it follows changes of
perspective that rotation, scale and shift alone cannot. It is the transform Image Registration and Image Integration
Lite start with.

### HOO

A palette for two narrowband images: hydrogen-alpha (Ha) to red, and doubly ionized oxygen (OIII) to green and
blue.

### Hot pixel

A pixel that reads far brighter than its neighbors, the more so the longer and warmer the exposure, at one place on
the sensor. [Dark frames](#dark-frame) record it, and calibration, [rejection](#rejection) of
[dithered](#dithering) frames or Cosmetic Correction removes it.

### HSI

A [color model](#color-model) that states a color as a hue, a saturation and an intensity. Its components are not
those of [HSL](#hsl) or [HSV](#hsv).

### HSL

A [color model](#color-model) that states a color as a hue, a saturation and a lightness, computed from the RGB values
as they are stored. HSL lightness is not [CIE L*](#cie-lab).

### HSV

A [color model](#color-model) that states a color as a hue, a saturation and a value, the largest of the three
channels.

## I

### Instrumental magnitude

A star's brightness measured from the image alone, as \(-2.5 \log_{10}\) of its [flux](#flux), before a zero point
turns it into a calibrated magnitude.

### Integration group

The [light frames](#light-frame) that Image Integration stacks into one final [master](#master). Lights of another
size, filter or object always go to another group, and you can split groups further by session, by a tag of your own
or by exposure.

## L

### Light frame

An exposure of the target itself, as opposed to the [calibration frames](#calibration-frame).

### Lineage

Where an image came from, in two forms. Its processing lineage lists every operation it went through with its
parameters, such as a Pixel Math run's evaluation path and random seed, or an external tool's program, version and
arguments. It stays with the image and its history steps. No file format stores it, and **Embed processing history**
writes nothing. In Image History, a view made from another shows the other's steps as inherited lineage, which
cannot be selected or restored.

### Linear image

An image whose samples are still proportional to the light that reached the sensor, as calibration and stacking
leave it. It looks nearly black until it is stretched. AfterNight reads whether an image is linear from the transfer
function the image states, or else from its [processing state](#processing-state), never from its
[pixel type](#pixel-type). Photometric and Spectrophotometric Color Calibration refuse an image recorded as nonlinear,
and you can attest that an imported image is linear.

### Log file

The file AfterNight writes its messages to, kept between sessions and rotated as it grows. Its folder and its level
are set in the settings. It is separate from the Session Console, which shows the messages of this session only. It
can hold file paths and other details of your work.

## M

### MAD

The [median absolute deviation](#median-absolute-deviation): the median distance of the samples from their median.

### Mask

An image linked to another one, its target, so that a process changes the target only where the mask allows: white
takes the processed result, black keeps the original, and gray mixes the two. Inverting the mask swaps them. The mask
is another open image of the target's size. Showing it, and how it is drawn, changes only the display. See
[Masks](../interface/masks.md).

### Master

A frame made by stacking many frames of one kind: a bias, dark or flat master that calibrates the lights, or the
stacked light that a run ends with. Keep a master you will process further in a format that keeps its precision, such
as FITS, XISF or a TIFF of enough bit depth, and not only as a lossy or lower-precision copy.

### Median absolute deviation

The median of the distances of the samples from their median: a measure of spread that a few stars or
[hot pixels](#hot-pixel) hardly change. Multiplied by 1.4826, it estimates the standard deviation of Gaussian noise.
[Autostretch](#autostretch), the Auto-stretch buttons of Histogram Transformation and the STF curve of Generalized
Hyperbolic Transformation measure the spread of the background with it. Its abbreviation is [MAD](#mad).

### Midtones

The tones between the shadows and the highlights. In Histogram Transformation, **Midtones** is the level between the
black and the white point that the [midtones transfer function](#midtones-transfer-function) sends to 0.5: below 0.5
it brightens the midtones, above it darkens them.

### Midtones transfer function

The curve that sends black to black, white to white, and a midtones balance \(m\) to one half:

\[ f(x) = \frac{(m - 1)\,x}{(2m - 1)\,x - m} \]

With \(m\) at 0.5 it is a straight line. Below 0.5 it brightens what lies between black and white, above 0.5 it darkens
it. Histogram Transformation, Statistical Stretch and [Autostretch](#autostretch) stretch with it. Its abbreviation
is [MTF](#mtf).

### Moffat profile

A model of a star's [PSF](#psf) whose wings fall off as a power, set by a value beta. It often fits stars seen
through the atmosphere better than a Gaussian.

### Morphology

Operations on the shapes of an image, made with a [structuring element](#structuring-element): [erosion](#erosion),
[dilation](#dilation), an opening, which is an erosion and then a dilation and removes small bright structures, a
closing, which is a dilation and then an erosion and fills small dark gaps, and a selection of a rank you choose
between the erosion and the dilation. They are often used to clean up a [mask](#mask).

### MTF

The [midtones transfer function](#midtones-transfer-function): the curve through black, white and a midtones balance
that Histogram Transformation and Autostretch stretch with.

## N

### Nonlinear image

An image whose samples are no longer proportional to the light, because a [stretch](#stretch) or another change of
tone was applied. AfterNight records an image as nonlinear when such a process changes it. The record can be missing:
an operation that drops the image's processing-state cards, such as a geometry change, leaves none, and a file from
another program usually states none. See [Processing state](#processing-state).

### Numeric domain

What a sample's number means, which the image states and AfterNight never guesses from its [pixel type](#pixel-type):
a [fraction of full scale](#fraction-of-full-scale), linear or through an encoding such as sRGB, a
[sensor code](#sensor-code) or a [physical quantity](#physical-quantity). TIFF, PNG, JPEG, a full-color RAW frame and
an XISF image of the 0 to 1 range open as fractions of full scale. Changing the pixel type does not change the domain.

## O

### ONNX

Open Neural Network Exchange, a file format for trained neural network models. DeepSNR's models and StarNet2's model
are ONNX files, run on the platforms ONNX Runtime supports.

### OSC

One-shot color: a color camera whose sensor carries a [CFA](#cfa), usually in a [Bayer pattern](#bayer-pattern), so
that each frame is a mosaic to [debayer](#debayer). An OSC image is a frame from such a camera, or the color image
debayered from it.

## P

### PCC

Photometric Color Calibration: color calibration from the brightness that a catalog gives its stars in a few
photometric bands. AfterNight's process of that name reads the stars from its [Gaia](#gaia) pack.

### Pedestal

A constant added to a light before calibration subtracts and divides, so that no sample falls below zero. It is
entered in 16-bit codes, where 65535 is full scale whatever the light's own bit depth, and it is added as entered,
not cut to the light's range.

### Phase correlation

A way to find the shift between two images from their Fourier transforms. Image Registration's phase correlation
finds a translation only, with no rotation or scale.

### Physical quantity

A value in stated units, such as electrons or a flux, held in floating point. A floating-point FITS file, and a
floating-point XISF image that declares a range other than 0 to 1, open as physical quantities and are not rescaled.
The viewer and the processes read them as a [fraction of full scale](#fraction-of-full-scale).

### Pixel scale

The angle of sky one pixel covers, in arcseconds per pixel: 206.265 times the pixel size in micrometers, divided by
the focal length in millimeters. A [plate solution](#plate-solution) states it.

### Pixel type

How each sample of an image is stored. AfterNight has eight: unsigned and signed integers of 8, 16 and 32 bits, and
floating point of 32 and 64 bits. It opens, shows, converts and saves all eight where the format allows them. The
pixel type says how many bytes a sample takes, not what it means: an integer file can hold linear counts or a
stretched picture, and a floating-point file can hold values far above 1. See [Numeric domain](#numeric-domain).

### Plate solution

What plate solving finds by matching the image's stars to a catalog: the sky position of its center, its
[pixel scale](#pixel-scale), its rotation and its projection, and optionally its distortion. AfterNight keeps it as
[WCS](#wcs) [header cards](#header-card), with [SIP](#sip) terms for the distortion. Annotate Image, the astrometry
overlays and the photometric color calibrations read it.

### Preview area

A rectangle you place on the preview of a process window, with the **Preview area** check box of the preview's
toolbar, so that the preview computes only that part. The part is processed as Apply would process it and shown over
the original. Apply still processes the whole image.

### Processing channel

The part of an image a process works on, as its **Channel** box chooses it: the RGB channels, one of them, or one
component of another [color model](#color-model), taken out of the image and put back after the process. It changes
the image, unlike the view's **View Channel**, which changes only the [display](#display-stretch).

### Processing state

What an image states about its data, in five [header cards](#header-card). `ANLIN` says linear, nonlinear or
unknown, `ANBASIS` its channel basis, `ANCCAL` whether and how it was color calibrated, `ANASRC` who stated it, the
loader, a process or a person, and `ANOP` the operation. FITS and XISF files keep them, and TIFF, PNG and JPEG carry
none. A stretch, a tone or color adjustment, CLAHE, SCNR or Pixel Math records its result
nonlinear under its own name.

### Protection point

In Generalized Hyperbolic Transformation, a shadow or highlight level below or above which the curve goes on as a
straight line, tangent to it, so that it keeps contrast there. A protection point does not clip. The two are named LP
and HP.

### PSF

Point spread function: the shape a point of light, such as a star, takes in the image, described by its widths, its
angle, its wings and how well a model fits it. [FWHM](#fwhm) and a [Moffat profile](#moffat-profile) describe it.

## R

### Range mask

A [mask](#mask) made from a range of brightness: the samples inside the range are white, those outside black. Range
Selection includes both ends of the range, can soften its edges or weight it by the image's own values, and can
smooth or invert the result.

### RANSAC

Random sample consensus: a fitting method that tries a transform on many small random sets of matched stars and keeps
the one most matches agree with, so that false matches do not spoil it.

### RAW

A camera's own raw file, holding the sensor's values before the camera processed them. In AfterNight's release builds,
a RawSpeed build of its own decodes the cameras it is qualified for, and LibRaw decodes the rest. An undebayered RAW
frame opens as [sensor codes](#sensor-code), a full-color one as a [fraction of full scale](#fraction-of-full-scale).

### RBF

Radial basis function: a surface built from bumps centered on sample points, each depending only on the distance from
its center. Background Extraction fits a smoothed thin-plate spline, one such surface, through the medians of its
samples and evaluates it at every pixel.

### Real-time preview

The preview of a process window, which shows the result on a copy of the target while you change the controls,
without changing the image. Each preview uses the values the window held when it was asked for. A change made while
one runs asks for one more, with the latest values, and only the newest result is shown. Switching between the
original and the result, the display stretch and the histogram never apply anything.

### Reference frame

The frame, or the output grid, that the other frames are registered and aligned to.

### Registration group

The [light frames](#light-frame) that Image Integration registers to one [reference frame](#reference-frame) with one
set of settings. It separates them by image size and by object.

### Rejection

Leaving out samples that disagree with the rest, such as satellite and airplane trails, cosmic rays and
[hot pixels](#hot-pixel), when frames are stacked or a level is measured. [Sigma clipping](#sigma-clipping) is one
way to do it.

### Response profile

The throughput curves of a camera's sensor, its filters, its window or low-pass filter and the other parts of the
light path, which say how a spectrum turns into red, green and blue counts. Spectrophotometric Color Calibration uses
one, chosen by name and version.

### Ringing

Dark and bright halos that follow a sharp edge, such as the rim of a star, after sharpening or after resampling with
a sharp kernel.

### RMS

Root mean square: the square root of the mean of the squared errors. In plate solving and registration it sums up how
far the matched stars lie from where the fitted transform puts them.

## S

### Sensor code

The number a camera reported, in its own units, before calibration says what it stands for. An integer FITS file and
an undebayered camera RAW frame open as sensor codes. The viewer and the processes read them as a
[fraction of full scale](#fraction-of-full-scale).

### SER

A video format for planetary and lunar captures: a series of uncompressed frames, each with its time. AfterNight
opens it as a [sequence](#sequence).

### Sequence

Frames of one size and type that AfterNight opens in one view and shows one at a time: a SER or video capture, an
image list, which is an `.anseq` file that names its frame files in order, or still images gathered with **New
Sequence**. Most processes work on every frame. Creating, extracting or converting a sequence makes a
[temporary sequence](#temporary-sequence). See [Sequences](../interface/sequences.md).

### SHO

A palette for three narrowband images: ionized sulfur (SII) to red, hydrogen-alpha (Ha) to green, and doubly ionized
oxygen (OIII) to blue.

### Sigma clipping

[Rejection](#rejection) of the samples that lie more than a set number of standard deviations from the center of the
others. It is usually repeated until no more samples are rejected.

### SIP

Simple Imaging Polynomial: the [FITS](#fits) convention that adds polynomial terms to a [WCS](#wcs) for the
distortion of the optics.

### SNR

Signal-to-noise ratio: how strong the real signal is against the noise around it. Stacking more frames raises it.

### SPCC

Spectrophotometric Color Calibration: color calibration that compares the color ratios of the image's stars with the
ratios their [Gaia](#gaia) XP spectra give. Those ratios are computed by integrating each spectrum through a
[response profile](#response-profile), and balanced on a [white reference](#white-reference).

### Spline

A curve through ordered points, made of pieces that join smoothly. Curves Transformation draws its curve with one of
several kinds, and its Monotone Cubic never overshoots a point.

### Starless image

An image with its stars removed, so that a nebula or a galaxy can be processed alone and the stars added back later.

### Starlet transform

A multiscale transform that splits an image into layers of detail at growing scales, plus a coarse residual, by
blurring it again and again with a kernel and keeping the differences. No layer is reduced in size. Multiscale Linear
Transform offers it with dyadic or linear scales and 15 kernels, and Dark Structure Enhance uses it with a Linear or a
B3-spline kernel.

### Stretch

A curve that brightens the faint part of a [linear image](#linear-image) far more than its bright part, so that what
was nearly black becomes visible. It makes the image [nonlinear](#nonlinear-image). A stretch made by a process
changes the image, while a [display stretch](#display-stretch) changes only how it is shown.

### Structuring element

The neighborhood a [morphology](#morphology) operation reads around each pixel, also called its kernel: a small grid
whose active cells name the neighbors that take part.

### Symmetry point

In Generalized Hyperbolic Transformation, the input level where the curve is steepest, around which it is built
symmetrically. It is named SP.

## T

### Temporary sequence

A [sequence](#sequence) that a process, a conversion or an extraction writes to AfterNight's temporary storage on
disk. It can back a new view or replace a view's sequence, but it is not kept until you save it, or convert it, to a
place of your own.

## V

### Validity rule

Which samples of an image mean something, stated with the image and never inferred from its pixels. All valid is
the default of an opened file. Non-finite invalid ignores NaN and infinity, which a zero flat leaves. Exact zero
no-data ignores zeros where something says so. An explicit mask of one bit per sample marks them, such as where a
stack did not reach. A closed range ignores samples outside it. Measurements skip invalid samples and processes leave
them unchanged.

## W

### WCS

World Coordinate System: the [FITS](#fits) [header cards](#header-card) that map each pixel of an image to a position
on the sky. A [plate solution](#plate-solution) writes them.

### White point

The input level that becomes white in a [stretch](#stretch): every sample at or above it is set to 1, and the detail
above it is lost. Histogram Transformation sets it with **Highlights**. See also [Black point](#black-point).

### White reference

What a color calibration takes as white. In Spectrophotometric Color Calibration, a reference spectrum chosen by name
and version, which sets the balance point at which the fit is evaluated. In Color Calibration, an image, or a
rectangle of one, whose channels' means above the background set the gains.

## X

### XISF

Extensible Image Serialization Format, an astrophotography image format that stores pixels with properties of their
own and [FITS](#fits) [header cards](#header-card). AfterNight reads its properties under their own names and writes
them back as the types they were.

## Y

### YCbCr

A [color model](#color-model) of a luma and two color differences, toward blue and toward red. AfterNight stores a
neutral Cb or Cr as 0.5. It is not calibrated luminance and chrominance.
