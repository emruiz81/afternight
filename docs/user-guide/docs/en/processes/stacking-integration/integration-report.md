---
kind: reference
summary: >-
  How to read the report a run of Image Integration writes: where it is, its header and six parts, a master's file,
  the comparison with earlier runs, its two forms, how its figures are printed and what it cannot state.
---
# Image Integration Report

A run of [Image Integration](image-integration.md) or of [Image Integration Lite](image-integration-lite.md) that
makes a [master](../../reference/glossary.md#master) writes a report of itself: one page that opens in your web
browser, with the masters, the lights that went into them, what needs a second look and how the run went. This page
says where the report is and how to move in it, then goes through its header and its six parts in the report's own
order. The two forms of the report, what it cannot state and what to do when it goes wrong come last.

```figure
image: ../../assets/images/processes/stacking-integration/integration-report.webp
alt: >-
  The top of a report in the dark theme. A column on the left lists the contents under six numbered parts, with the
  masters Ha, Oiii and Sii under Master by master. On the right, a header titled M 16 reads Image Integration Lite,
  night of 18 Jun 2025, with the pill Completed with warnings and a Theme button. A sentence sums the run up, and four
  tiles read Masters 3, Lights used 64 of 64, Integration 5 h 20 m and Run time 17 s. Below, Part 1, The night in
  summary, begins with Needs attention and three warnings, each with a See it link.
caption: >-
  The report of a run of Image Integration Lite on narrowband lights of the Eagle Nebula: the contents, the header
  and the first issues of the summary.
```

## Where the report is

The report is `workflow_report.html`, in the run's own folder under `history` in the output directory. The folder
is named by the date and the time the run started, such as `history\20261111_151406_688`. The pictures the report
shows are beside it, under `report_previews`.
[What a run writes](image-integration-reference.md#what-a-run-writes) lists the other files of the folder.

| Where | Choose | What happens |
| --- | --- | --- |
| The progress window, when the run has ended | **Open Report** | The run's report opens in your web browser. |
| **Run History**, on the **Summary** tab of Image Integration | **Report** on the run's row | The report is written again from the record the run kept, then opened. |
| The same row | **Save As...** | You name a file, and the report is written into it as one file that opens on its own. See [The two forms](#the-two-forms). |
| The run's folder | `workflow_report.html` | The report opens as the run left it. |

- Image Integration Lite does not list earlier runs. To reach the row of a Lite run, open Image Integration with
  the same output directory.
- A run writes a report when it made at least one master: a run that completed, and a run that failed or that you
  cancelled after a group had finished. A run that stopped before any master writes none, and neither does a run of
  the registration alone, which **Run Registration** and **Run Registration (All Groups)** start.
- A second run in the same output directory writes a report of its own, in a folder of its own. `history` keeps the
  newest runs only, so copy a report you want to keep, or save it as one file.

## Moving in the report

The report needs no network and no program but a browser. Its charts are drawn when it is written, and every figure
is in the page as text, so it reads the same on another computer and with scripts turned off.

Contents
:   A column on the left lists the header and the six parts, with a count beside an entry where there is one and a
    warning mark beside a master that an issue names. Choose an entry to go there. In a window narrower than 1,181
    pixels the column is hidden and the report takes the whole width.

**Theme**
:   The button at the top right changes between the dark and the light look, and the browser remembers your choice.
    Until you choose, the report is dark, or light when your system asks for light. A printed report is always light.

What a word means
:   A heading or a figure with a dotted underline explains itself when you point at it, and so does every mark of a
    chart. The ++tab++ key reaches them too.

Folded sections
:   The first master's file is open and the others are closed: choose a master's line to open it. A link that leads
    into a closed section opens it. Long lists, such as the log and the table of every light, are closed the same
    way.

Tables of lights
:   Choose a heading of a table of lights to sort by that column, and again to turn the order round.

Printing
:   Print from the browser. Every closed section is printed open, the contents and the buttons are left out, each
    master starts a new page, and a chart with several measures shows its first.

## How figures are printed

| Value | Printed as | Example |
| --- | --- | --- |
| A measured figure | Four significant digits; a power of ten below one thousandth | `0.01743`, `9.175 × 10⁻⁵` |
| A count | Whole, with a thin space between groups of three digits | `9 036 032` |
| A time taken, an exposure | Its two largest units | `987 ms`, `17 s`, `5 h 20 m` |
| A size on disk | The largest unit it fills, in steps of 1,024 | `103 MB`, `2.5 GB` |
| A date and time | To the minute, in the time zone of the computer that ran the run | `11 Nov 2026, 15:14` |

- A level, a noise and every other statistic of an image is a
  [fraction of full scale](../../reference/glossary.md#fraction-of-full-scale), measured on the
  [linear](../../reference/glossary.md#linear-image) master. The pictures are stretched for display, and no figure is
  measured on a picture.
- A dash stands for a value the report cannot state. A missing value is never printed as zero.
- The decimal mark is a point and the months are named in English on every computer, since a report is often read
  away from the one that wrote it.

## The header

The title is the target's name, or the number of targets when the run has several. Under it are the process that
ran and the night or nights the lights were taken on. A night is the evening's date: a light taken at 02:00 belongs
to the night before.

The pill says how the run ended:

| Pill | Means |
| --- | --- |
| **Completed** | Every master was made or was still good, and nothing needs attention but notes. |
| **Completed with warnings** | The run finished, and **Needs attention** holds a warning. |
| **Partly completed** | Some masters were made and kept, and a group failed or the run failed after them. |
| **Cancelled** | You stopped the run. The masters finished by then were kept. |

A sentence then sums the run up, and four tiles give its figures:

| Tile | Reads |
| --- | --- |
| **Masters** | The masters of the run, and how many were made, reused, failed or not reached. |
| **Lights used** | The lights the masters combine, of the lights the run took, and how many were left out. |
| **Integration** | The exposure of the lights used, of the exposure captured. |
| **Run time** | How long the run took, and when it was archived. |

When an earlier run of the folder is there to compare with, each tile adds that run's figure, as **Earlier run:**.
A run of one master shows the master's picture in the header, and its noise in place of the count of masters. The
notes you typed in **Session Notes** follow the tiles, under **Your notes**.

## Part 1: The night in summary

The summary holds what you look at first. The parts after it only break its figures down. A block with nothing to
say is left out, and so is its entry in the contents.

| Block | Shows | Is there |
| --- | --- | --- |
| **What happened** | For a run that stopped: what happened, what was kept, what was not made and what to do | Only when the run failed or was cancelled |
| **Needs attention** | The issues of the run, the most serious first, each with what to do where there is something to do, and a **See it** link to the place that shows it | Always; it says so when there is nothing |
| **What was made** | A card for each master with its stretched picture, its lights and its integration, and the table **How the masters compare** | With more than one master |
| **Lights** | The lights used and left out as one bar, the reasons lights were left out, and a row for each night | Always |
| **The night** | The [FWHM](../../reference/glossary.md#fwhm), the stars and the background noise of every light at the time it was taken | When a light states one of them |
| **Light quality** | The sharpness by master, the roundness against the sharpness, and the weights the stacks used | When a light states its sharpness or a weight |

Needs attention
:   An issue is critical, a warning or a note. The report works its issues out from what the run recorded: a group of
    lights that takes no flat while another does, a flat master made without a bias or a dark, a master saved
    without a [plate solution](../../reference/glossary.md#plate-solution), lights whose registration failed, a low
    share of lights used. Under them, **Messages from the run** repeats the warnings and errors of the run word for
    word, folded when there are more than five. So one fact may stand twice, once in each wording.

How the masters compare
:   One row a master: its [SNR](../../reference/glossary.md#snr), its noise, its
    [effective frames](../../reference/glossary.md#effective-frames), its
    [SNR gain](../../reference/glossary.md#snr-gain) and where its plate solution came from. A bar in a cell is the
    figure's share of the best master's, or of what the number of lights promises.

Astrometry
:   **Inherited** marks an [inherited solution](../../reference/glossary.md#inherited-solution), which the master
    took from the registration's reference. **Solved** marks a master that was plate-solved after the stack. **None**
    marks a master saved without a solution, and its file gives the reason.

Why lights were left out
:   A light has one state, and a reason with no light is not listed.

    | State | The light |
    | --- | --- |
    | **Used** | Went into the master. |
    | **Excluded by you** | Was left out in the lights' list, or from the stack alone. A light left out of the stack alone was still registered. |
    | **Registration failed** | Could not be matched to the reference. Its line gives the stars that were found. |
    | **Left out by the stack** | Was dropped by the stack, which gives the reason: it had no registered file, no transform, or no product a comet or star stack needs. |
    | **Excluded on the restack** | Was flagged by the analysis of [rejection](../../reference/glossary.md#rejection) clusters, and the master was stacked again without it. |
    | **Not stacked** | Belongs to a master that failed, was not reached or was cancelled. |

    On a chart a light that was left out is a cross, and in a table its state is a mark with its name: no state is
    told by color alone.

## Part 2: Master by master

Each master has a file of its own, with four sections. What a master shares with others, such as a flat master or a
registration, is stated once in Part 4, and the file links to it. A run of one master titles this part **The
master**.

The line that opens a file gives the master's name, its lights, its integration, its file name, how many pictures it
has to look at and its astrometry. A master the run reused, one that failed and one that was not reached say so
there.

### The picture

```figure
image: ../../assets/images/processes/stacking-integration/integration-report-master.webp
alt: >-
  The picture section of the Ha master's file. On the left, a stretched gray picture of the Eagle Nebula with the
  buttons Master and Detail under it, and a chart titled How its samples are spread: one narrow peak on a
  logarithmic axis, marked median 0.03783. On the right, a table of statistics as fractions of full scale, from
  Samples 9 036 032 through Minimum, Maximum, Mean, Median and Background noise to Sum of squares, and under it the
  dynamic range.
caption: >-
  The picture of a master, the spread of its samples and its statistics. The statistics are measured on the linear
  master, not on the stretched picture.
```

- **Master** shows the whole frame under a [display stretch](../../reference/glossary.md#display-stretch) of its
  own. **Detail** shows the middle of the frame at full scale, one pixel of the master to one pixel of the picture,
  under the same stretch: it is where the noise and the shape of the stars show.
- A master with maps has a button for each: a rejection map, and the coverage, variance, SNR gain and weight
  maps. One is shown at a time, and a sentence under the picture says how
  to read it. In a rejection map scattered points are noise, a line is a satellite or an aircraft, and a patch is
  something only some lights hold.
- **How its samples are spread** counts the master's samples by value on a logarithmic axis, with the median
  marked. A color master has one panel a channel.
- **Statistics** lists the figures of one read of the master, a column a channel. Point at a name with a dotted
  underline for what it measures. **Samples measured** leaves out the clipped samples.

### The stack

What the stack recorded of itself:

| Figure | Says |
| --- | --- |
| **Coverage** | The [pixel coverage](../../reference/glossary.md#pixel-coverage): the fewest, the median and the most lights that reached a pixel, as one bar. |
| **Effective frames** | The [effective frames](../../reference/glossary.md#effective-frames) of the master, of the lights combined. |
| **SNR gain** | The gain the stack measured, of the gain its number of lights promises. |
| **Rejected**, **Rejected per light** | The share of all samples the stack rejected, then the range and the median over the lights. |
| **Weighting**, **Weighted integration** | How the lights were weighed, and their exposure with each light counted at its weight. |

A stack that writes no quality maps may record no coverage, no effective frames and no measured gain. The
section then leaves those out and Part 6 lists them under **What the report could not state**.

The section also names what brought the lights to one level, the share of the capture's area the master covers,
the astrometry with the center, the scale, the field and the rotation of the solution, the time each phase of
finishing the master took, and the weights of its lights as bars.

### Its lights

```figure
image: ../../assets/images/processes/stacking-integration/integration-report-lights.webp
alt: >-
  A chart titled FWHM in pixels over the 25 lights of the Ha master in the order they were taken, from 18 Jun 2025
  to 04:57. A blue line with a dot at each light runs near 3 pixels, steps up to about 4.5 and ends near 5.5. A
  gray line marks the median, 3.546. Above the chart, a row of buttons reads FWHM, Eccentricity, Stars, SNR,
  Background, Weight, Rejected and Quality, with FWHM chosen.
caption: >-
  The lights of one master on their FWHM, in the order they were taken. The buttons above the chart change the
  measure.
```

- The chart draws one measure over the master's lights in the order they were taken. The buttons above it change
  the measure: the FWHM, the eccentricity, the stars, the SNR, the background noise, the weight, the share of
  rejected samples and the quality score, each where a light states it. Point at a light for all its figures.
- **Where each light sat** plots the shift of each light against the
  [reference frame](../../reference/glossary.md#reference-frame), a mark a light and a shape a night. A spread of
  marks is the [dithering](../../reference/glossary.md#dithering). The shift is that of the middle of the frame,
  so a light taken after a meridian flip sits among the others; the table of lights then has a **Turn** column,
  where such a light reads 180.
- **Left out** lists the lights of the group that did not go into the master, each with its state and its reason.
- The last line opens a table of every light with every measure, its weight, its share of the stack, its state and
  its file.

### How it was made

The settings that belong to this master alone: the calibration of its lights, with the masters they take as links,
then its stacking, its normalization and its [drizzle](../../reference/glossary.md#drizzle). A setting of a mode
is printed only in that mode. The files the run wrote for the master follow, with their sizes.

- The mark `≠` stands on a value that more than half of the other groups of the same stage do not use. Point at it
  for what they use. With two groups, or with every group different, nothing is marked.
- A setting you left on **Auto** prints what it resolved to. Point at it for what was asked and why.

### A master that was not made, or not made again

| The master | Its file |
| --- | --- |
| Was reused, because its frames and settings had not changed | Says so and names the run that made it, as a link when `history` still holds that run. Its figures, its stack and its lights are the ones that run stated. When that run is gone, the picture and the statistics are read from the file and the rest is listed under **What the report could not state**. |
| Failed | Says why first and shows no picture, then the phases of finishing it that ran, its lights and its settings. |
| Was not reached, or was cancelled | Says that the run stopped before it, with its lights and its settings. |
| Is a combined color image | Shows the picture, its channels and what it was made from. It has no lights and no stack of its own. |

## Part 3: Against earlier runs

The report compares the run with the runs archived before it in the same output directory. It reads their stored
reports and no image of theirs, so the figures of an earlier run are the ones that run stated.

| Block | Compares |
| --- | --- |
| **Before and after** | Each master this run made with the latest earlier run that made the same master: the two pictures, then the lights used, the integration, the noise, the signal to noise, the median FWHM, the effective frames and the samples rejected. Where several earlier runs made the master, **Against the run of** picks which of them it stands beside. |
| **What changed** | The run as a whole with the latest earlier run that completed: the totals, the application's version and the fingerprints, the settings that differ, and the frames each group gained and lost. |
| **Over the runs of this folder** | The runs the folder keeps, the oldest first: the integration, the lights used and the noise of the first master as a column for each of the latest eight runs, then a table of every run with a link to its report. |

- The two pictures of a master lie under one divider: the earlier run is left of it and this run right of it. Drag
  the divider, or move it with the arrow keys. **Whole frame** and **Detail** choose the picture, and with several
  masters a button for each chooses the master. Two frames of different shapes stand side by side instead.
- Each picture is its own run's, under its own display stretch, so a difference in brightness between the two says
  nothing. The figures under the pictures are measured on the linear masters.
- A change for the better or for the worse is said in words beside the figure. The share of samples rejected is
  marked neither way: it follows the lights and the settings.
- A master that the run reused is left out of **Before and after**, since it is the earlier run's own master. A
  master no earlier run made is marked as new.
- **Against the run of** offers up to six earlier runs, the latest first. The pictures of the runs before the
  latest are read from those runs' folders in `history`, so a report saved as one file offers the latest run
  alone, and a run that `history` removed keeps its figures and loses its pictures.
- A setting changed the same way in several groups is one row that names the groups. A line under the table says
  how many other settings were compared and are the same.
- The fingerprints tell what differs at a glance: a run with the same settings and the same input files has the
  same run fingerprint.
- With no earlier run, the part is one sentence: "This is the first run of this output folder that keeps a report:
  there is no earlier run to compare it with."

## Part 4: What the masters share

| Block | Shows |
| --- | --- |
| **What made each master** | A row a master, read from left to right: its lights, its bias, dark and flat masters, its registration, then the master. A cell that spans several rows is one group those masters share. |
| **Calibration masters** | Each bias, dark and flat master with a small picture, its frames, its level and noise, what it is checked by, the masters it was calibrated with, the masters that use it and whether it was made, reused or supplied by you. **Check** gives the share of [hot pixels](../../reference/glossary.md#hot-pixel) of a bias and a dark, which are the pixels more than ten deviations above the median, and the level of a flat's corners as a share of the level at its center. **How each was stacked** opens their settings. |
| **Registration** | A card for each [registration group](../../reference/glossary.md#registration-group): the lights registered, the reference and who chose it, the transform, where each light sat, and the group's settings. |

A calibration master's level and noise are measured over the middle of a large frame, and the row says over what
share. A run of one master titles this part **Calibration and registration**, and gives one line in place of the
table of groups.

## Part 5: The run

| Block | Shows |
| --- | --- |
| **Stage by stage** | The stages on one time axis, with the application's own work before and after them in gray, such as the archive and the writing of the report. A stage reads as ran, reused, failed, cancelled, not reached or without work. **The same as a table** opens the rows. |
| **Left to the application** | Every setting left on **Auto**: what was asked for, what it resolved to and why. |
| **Stated once for the run** | The settings that belong to no group: the mode and the scope of the run, the execution, the plate solve and the color combination. |
| **What the run wrote and used** | What was written to disk by kind, and the engine's memory with its peak. |
| **Diagnostics** | The counters of the run when it collected them, and the log as the run wrote it, folded. |

## Part 6: Provenance

| Block | Shows |
| --- | --- |
| **Acquisition** | How each group of lights was taken: its exposures, its share of the exposure, its gain, offset and sensor temperature, and its first and last light. |
| **Equipment and headers** | The camera, the telescope and the site as the masters' headers state them, and how many lights' headers state each field. |
| **Software and fingerprints** | The application's version, the system, the process, the run, when and in which time zone it was archived, the output folder, and the three fingerprints. **In one line** sums the run up in a line to select and copy. |
| **Input files** | Every file the run was given, by group. |
| **What the report could not state** | Each thing the report leaves out, with its reason. |

## The two forms

| Form | Is | Use it |
| --- | --- | --- |
| Linked | `workflow_report.html` in the run's folder, which shows the pictures under `report_previews` beside it | On the computer that ran the run. Its pictures are full size, and its links lead to the reports of the earlier runs. |
| One file | The file **Save As...** writes, named `AfterNight report <run>.html` unless you change it, with every picture inside it | To send or to keep. It opens from any folder, on any computer. |

- The linked report shows no picture when you move or send it without `report_previews`. Send the one-file form
  instead.
- In the one-file form each picture is reduced to 1,280 pixels on its longer side. A file is printed by its name,
  without its folder, and the output folder by its name alone, so the report does not show how your disk is laid
  out. No link leads out of the file.
- The one-file form still names your files, your equipment, your site and your session notes, and holds the log.
  Read it before you send it.
- **Save As...** is offered for a run whose folder keeps its report as data, `workflow_report.json`. A run made by
  an earlier version of AfterNight keeps none.

## What the report cannot state

The report states what the run recorded and what it could measure, and nothing else. What it leaves out is listed
under **What the report could not state**, in Part 6.

- **A run made by an earlier version.** Its folder holds the report that version wrote, which still opens. It
  cannot be written again or saved as one file, it is no earlier run to compare with, and **Over the runs of this
  folder** lists it by its name with the reason.
- **An earlier run that `history` removed.** `history` keeps the newest runs within 20 runs and 5 GiB. A removed
  run stays in the list of runs without a link, and the pictures already copied from it stay in the comparison.
- **A reused master whose run is gone.** Its picture and statistics are read from the file. What its stack
  recorded and what became of each light are not stated.
- **A master that could not be read.** A file that is gone or cannot be opened has no statistics and no spread of
  samples.
- **What a stack did not record.** A stack that writes no quality maps may state no effective frames, no measured
  SNR gain and no pixel coverage.
- **Why a registration failed, once its run is gone.** The run that registers a light records why it failed, and
  the report gives the reason with the stars that were found. A later run that registers nothing again takes the
  reason from that run's report, and gives none when `history` no longer holds it.
- **A light without a time in its header.** It belongs to no night, and the charts by time fall back to the order
  of the lights.
- **Figures a mode does not give.** A local normalization states no scale and offset for a light, and a polynomial
  transform states its kind and no shift.

## When it goes wrong

**Report** answers "The report of this run cannot be written again:", then opens an older report
:   **Cause.** The run's folder keeps no report as data, or one this version does not read. A run made by an
    earlier version reads "The archive keeps no stored report (workflow_report.json)."
:   **Fix.** Read the report that opens: it is the one the run wrote. Run again to get a report of this version.

**Report** answers "The report of this run cannot be written:"
:   **Cause.** The folder keeps no report as data and no report file either.
:   **Fix.** Read the run's log with **Log**, or run again.

A box titled **Save Report As One File** reads "The report was not saved:"
:   **Cause.** The file could not be written, or the run's folder is gone or keeps no report as data. The message
    gives the reason.
:   **Fix.** Choose a folder you can write to. For a run of an earlier version, send its folder instead.

The report shows empty frames in place of its pictures
:   **Cause.** The file was moved or sent without the `report_previews` folder beside it.
:   **Fix.** Open it in the run's folder, or save it as one file with **Save As...**.

A link to an earlier run's report leads nowhere
:   **Cause.** `history` removed that run after this report was written.
:   **Fix.** Choose **Report** on this run's row: the report is written again without the link.

No report was written
:   **Cause.** No master was made, the run only registered, or the archive could not be written.
:   **Fix.** See [Messages](image-integration-reference.md#messages) in the Image Integration Reference.

## Related

- [Image Integration](image-integration.md), for the window, the **Summary** tab and its **Run History**.
- [Image Integration Lite](image-integration-lite.md), for a run with the choices made for you.
- [Image Integration Reference](image-integration-reference.md), for the files a run writes and how a later run
  reuses them.
- [Troubleshooting And Diagnostics](../../reference/troubleshooting.md), for what to send with a report of a
  problem.
- [Glossary](../../reference/glossary.md), for the terms of the report.
