# Reading the XISF writer's units in PixInsight

The engine's XISF codec writes through OpenXISF, and its suites read what it writes back through the same library.
PixInsight defines the format, so it is the reader the writer is checked against: one unit of each form the writer
produces is opened in PixInsight, and what PixInsight reads is held to what was written. The check needs a PixInsight
installation, so it is run by hand on a machine that has one, not by the suites or CI; it is repeated when the XISF
writer, its card handling or OpenXISF changes.

## What is written

`XisfInteropTest.EveryFormTheWriterProducesIsWrittenAsStatedForAnotherReader` in `Test_EngineImage` writes the units.
It runs with the suite, holding each unit to the form stated - OpenXISF's block facts for the codec, byte shuffling,
subblocks and checksum - and to the samples and cards written, read back through the adapter. When
`AFTERNIGHT_XISF_WRITTEN_UNITS` names a directory it keeps the units there with `units.json`, the catalogue of what each
holds: geometry, sample format, colour space, storage, image type, block facts, the FITS keywords and properties
stated, with each property's type and value, the filter array, and for a solved unit where the engine puts a grid of
points on the sky.

| Units | Forms |
|---|---|
| `a-*` | UInt8, UInt16, UInt32, Float32 and Float64, each in one channel and in three, planar and interleaved |
| `b-*` | zlib, LZ4, LZ4-HC and Zstandard, each with and without byte shuffling, in five subblocks of 16 KiB; and Zstandard in the default form, shuffled, in 4 MiB subblocks of a 6.6 MB frame |
| `c-*` | SHA-1, SHA-256, SHA-512, SHA3-256 and SHA3-512, and SHA3-512 beside Zstandard |
| `d-*` | reserved images whose rows were written in place, last band first, with SHA-256, SHA3-512 and no checksum, planar and interleaved |
| `e-*` | a FITS keyword of each kind and a property of each type PixInsight reads, with an identifier, image type, resolution and display function; a filter array with and without a phase, the first plate-solved; a solution with a SIP distortion, with and without an inverse model |
| `r-*` | PixInsight's own units of `src/AfterNightTests/fixtures/common/xisf` read and written again with their cards, as an opened unit is saved: the rich RGB unit (in Zstandard with SHA-256), the filter-array unit, the typed-property unit and the three plate-solved units |

The samples of a unit of the `a` to `e` groups follow a pattern the test and the reading script both compute: integer
codes and binary fractions every format holds exactly, the format's least value at the first pixel and its greatest
at the last, so every sample is compared exactly rather than by a statistic.

## Running the check

```cmd
set AFTERNIGHT_TEST_STORAGE_ROOT=<scratch>\storage
set AFTERNIGHT_XISF_WRITTEN_UNITS=<scratch>\units
bin\msvc\release\Test_EngineImage.exe --gtest_filter=*EveryFormTheWriter*
"C:\Program Files\PixInsight\bin\PixInsight.exe" -n --automation-mode -r="<repository>/src/AfterNightTests/Test_EngineImage/pixinsight/read_written_units.js,<scratch>/units,<repository>/src/AfterNightTests/fixtures/common/xisf,<scratch>/report.txt" --force-exit
```

PixInsight runs the script without a window and exits. `read_written_units.js` opens each unit through PixInsight's
XISF module and writes the report: per unit what was compared and any difference, then how many units failed. It
compares every sample - an integer one to its code, a floating point one exactly - the geometry, sample format, colour
space and image type, every keyword and property the catalogue states with its value and type, the filter array, the
identifier, resolution and display function, and opens a solved unit in an image window to hold PixInsight's solution,
both from image to sky and back, within a hundredth of a pixel of the engine's. A rewritten unit of PixInsight's is
held to PixInsight's own reading of the original instead: every sample, keyword, property and filter array, and the
solution over a grid that reaches past the image.

## The last check

Run on 2026-11-09 with **PixInsight 1.9.6 (build 1713)** on Windows 11, over units written by AfterNightEngine 1.6.0
through OpenXISF 1.1.0: PixInsight opens all 44 units, and reads every sample, keyword, property, filter array and
structural fact as written. The script was also run over a copy with a sample, a keyword, a property, a filter array,
a display function and a solution changed, and reported each one.

What PixInsight reads differently, none of it a defect of the writer:

- **A SIP distortion is left out.** PixInsight builds a solution from the world-coordinate keywords out of their
  linear part alone, with or without the inverse polynomials: its positions are those of the linear solution to the
  last digit, 0.16 px from the engine's whole solution at the corners of the test unit. A solution the plate solver fits
  with a distortion is written as SIP keywords, which PixInsight therefore reads without it; the script reports this
  rather than failing, and fails a solution that matches neither. A solution read from a unit of PixInsight's is
  written back in its own `AstrometricSolution` properties, and PixInsight evaluates it as it does the original.
- **A String held in a data block is listed as an IsoString.** XISF has one String type; PixInsight lists one whose
  text it read from a data block as an IsoString, and the text is the same.
- **The writer states some things the original did not.** An acquisition fact read from a source is also written in
  the writer's own spelling - `EXPOSURE` beside the source's `EXPTIME` - as the FITS writer does; an image that states
  no type is written as a light frame; and every unit carries `Instrument:Software:Name`, the engine's version. PixInsight
  reads each as written.

Distributed units (`.xish`) are not checked: the writer does not produce them, and PixInsight does not read them.
