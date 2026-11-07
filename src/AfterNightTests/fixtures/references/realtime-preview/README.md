# Real-time preview references

What the real-time preview of a process dialog showed when these records were captured, held exactly by
`Test_ImageRTPreviewControl`'s `Dialogs/RealtimePreviewReferences.*` cases
(`src/AfterNightTests/Test_ImageRTPreviewControl/RealtimePreviewReferencesTest.cpp`). A change that alters any of them
changes what a person sees in a preview; it says why, and the references are captured again with it. Anything else
that alters them is a regression.

## What is recorded

The cases are those the recorded behaviour expectations (`../../common/expectations/behavior/`) hold for the histogram
transformation and its automatic stretches, the generalized hyperbolic transformation and its screen transfer, the
arcsinh and statistical stretches, the curves, the gamma correction and brightness and contrast - each mode over each
fixture scene it was recorded on - and brightness and contrast through three masks. Each case opens its dialog over the
scene, sets the mode, turns the preview on, puts the preview view in autostretch and waits until everything has landed.
Then it records, as one file per dialog:

- `<dialog>.<mode>.preview/<scene>`: the settled preview's samples, in the preview's own representation.
- `<dialog>.<mode>.statistics/<scene>`: the analysis the preview view presented them with, one row per colour channel
  in Float64: the median, the median absolute deviation, the samples measured, the neutralization offset, the
  calibration gain and offset, whether the statistics were measured, neutralized and calibrated, the promotion, and
  whether they were taken from subsampled samples. The promotion is 0, `Gradient`, in every record: every view carries
  each sample at its full precision through its stretch.
- `<dialog>.<mode>.histogram-<n>/<scene>`: the counts of each histogram the dialog draws, in its layout's order, one row
  per channel in Float64: every bin, then the samples clipped low, clipped high, not a number and admitted in all.

A masked case's mode ends in `-masked-by-<mask scene>`, and its description carries the mask scene's digests.

The records are the fixture layers' expectation records (`../../common/FixtureExpectation.h`), classified `exact`. Each
states the scene by name and by the digests of its description and samples, and the case by a description the
comparison also holds, so a case or a scene that has changed is refused rather than passed. A record's `commit` names
the tree the capture ran on - the parent of the commit that carries the change it records, since a capture precedes its
commit - and `captured` the day; the comparison reads neither. The statistics are read through
`ImageViewWidget::presentedAnalysis()`, the analysis the view keeps, which changes nothing it presents.

## What the records show

The preview of a process whose interactive frames may run over a reduction of its target - brightness and contrast,
the gamma correction and the curves transformation - is stretched through the target's own analysis, measured once
for its revision, so the stretch does not follow the adjustment: every mode's statistics record states the scene's own
medians and median absolute deviations. The curves transformation's records were captured again when its drags came
to run over what the preview view paints; before, each of its previews was stretched by its own statistics.

The gamma correction's preview is the Apply's power law in every frame. The unbounded floating scene holds 37 samples
below zero - 10 in the first channel and 27 in the third - and the exact power answers a negative base a not-a-number,
as the Apply does, so its `gamma-2.2` preview's samples there are not-a-number, and its histogram counts them as such
rather than as clipped low (its not-a-number column reads 10, 0 and 27). The preview is the Apply's bytes, which is what
these references hold; whether a gamma should answer a sample below zero otherwise is a decision about the Apply
(`PointwiseWindowTest`, `GammaCorrectionTest.AFloatingSampleBelowZeroIsANotANumberInThePreviewAsInTheApply`).

Every sample of a compiled pointwise run takes the vector power, which gives the same bits on AVX2 and on AVX-512
(`PointwiseExecutionTest.AFloat32PowerAnswersASampleAlikeWhereverItSitsInItsRowAndOnEveryVectorTarget`), wherever the
sample sits in its row; so the gamma correction's records hold on both kinds of host, and a preview area, whose rows
start and end elsewhere, agrees with its Apply.

## The builds that reproduce them

Release builds with MSVC on Windows and with Clang 21 on Linux reproduce every record exactly. A build that does not
reproduce a record exactly keeps its own beside them, under `<compiler>-<platform>/` as the provenance names the build
(`clang-linux-x64/`, say), and reads it in place of the common one; none is checked in.

## Capturing them again

```cmd
set AFTERNIGHT_TEST_STORAGE_ROOT=%TEMP%\at_references
set AFTERNIGHT_FIXTURE_CAPTURE=%TEMP%\references-capture
set AFTERNIGHT_FIXTURE_CAPTURE_COMMIT=<the commit the libraries were built from>
bin\msvc\release\Test_ImageRTPreviewControl.exe --gtest_filter=Dialogs/RealtimePreviewReferences.*
```

The cases then write fresh records under `%AFTERNIGHT_FIXTURE_CAPTURE%\references\realtime-preview\` and are reported
as failed, by design: a capture run compares nothing, so a shell that leaves the variable set cannot run the suite
green. Review the records against these, record by record, and copy them here. On a build other than the one these came
from, the capture also writes that build's own records, for the cases it does not reproduce, under
`<compiler>-<platform>\`; that directory is all such a build checks in.
