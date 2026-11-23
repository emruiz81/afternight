---
kind: interface
summary: >-
  The Integration place: the ways to stack your frames, the single steps of calibration, registration and stacking,
  and the list of integration jobs with their last runs, the one running first.
---
# Integration Hub

The Integration hub is the place the [rail](../reference/glossary.md#rail)'s **Integration** shows, and **View > Integration** too. It gathers what
turns frames into a stacked [master](../reference/glossary.md#master): the ways to stack, the single steps that each
do one stage on a list of frames, and every [integration job](../reference/glossary.md#integration-job), with how its last run ended. No image needs to be open.

```figure
owed: true
image: ../assets/images/interface/integration-hub.webp
alt: >-
  The main window showing the Integration place. Under the heading Integration and the line Calibrate, register and
  stack, three large cards under Three ways to stack: Guided integration with the tag Planned, a greyed Start and the
  words Not in this version; Image Integration with a blue New integration button; Image Integration Lite with a New
  quick integration button. Under Single steps, four smaller cards: Image Calibration, Image Registration, Image
  Stacking and Image List Analyzer, each with a line and an arrow. Under Integration jobs, with the count 0, a search
  field and Show set to All projects, the column headings Job, Project, Frames, Masters, Last run, Took and Status,
  the line No integration job yet, and a line about the project a job without one creates.
caption: >-
  The Integration place of a new profile: its ways to stack and its single steps, and no job yet.
pins:
  - {n: 1, x: 4.4, y: 12.9}
  - {n: 2, x: 6.4, y: 27.0}
  - {n: 3, x: 37.6, y: 27.0}
  - {n: 4, x: 69.0, y: 27.0}
  - {n: 5, x: 4.4, y: 50.8}
  - {n: 6, x: 4.4, y: 64.7}
  - {n: 7, x: 66.4, y: 64.7}
  - {n: 8, x: 4.4, y: 68.6}
```

## What you see

1. **The heading.** "Integration", and "Calibrate, register and stack."
2. **Guided integration.** A way to stack that asks a few questions and sets up the rest. It is shown as planned: its
   **Start** is greyed, and the card says "Not in this version".
3. **Image Integration.** The full workflow, every setting in reach. **New integration** opens it.
4. **Image Integration Lite.** Lights and optional masters on one page. **New quick integration** opens it.
5. **Single steps.** Image Calibration, Image Registration, Image Stacking and Image List Analyzer, each a card that
   opens its process.
6. **Integration jobs.** The heading, with how many jobs there are and how many are running.
7. **Search and Show.** Find a job by its name or its [project](../reference/glossary.md#project), and choose the project whose jobs are shown.
8. **The jobs.** A row for each job: its name, its project, its frames, its masters, its last run, how long that took,
   its status and **Open**. A line under the list says what a job run with no project does.

## What you can do

### Start an integration

Choose **New integration** to stack with
[Image Integration](../processes/stacking-integration/image-integration.md): bias, darks, flats and lights, each stage
set as you need it. Choose **New quick integration** for
[Image Integration Lite](../processes/stacking-integration/image-integration-lite.md): the lights and the masters you
have, on one page, with one button. Each opens as a [task](../reference/glossary.md#task), a page of the centre with its item in the rail. The
**Process > Preprocess** menu opens the same processes.

### Run one stage

Choose a card under **Single steps** to work on a list of frames with one process:
[Image Calibration](../processes/calibration/image-calibration.md) applies bias, dark and flat masters,
[Image Registration](../processes/registration-alignment/image-registration.md) detects stars and aligns the frames,
[Image Stacking](../processes/stacking-integration/image-stacking.md) combines registered frames with
[rejection](../reference/glossary.md#rejection), and
[Image List Analyzer](../processes/stacking-integration/image-list-analyzer.md) inspects, blinks and exports them.

### Find and reopen a job

A job is a session of Image Integration: its frames, its settings and its output folder. The list holds the jobs kept
outside any project and those of the project that is open; a closed project's jobs show again when you open it.
Running jobs come first, then the others by their last run, the most recent first, then those never run, by name.

1. Type part of a job's name, or of its project's, in **Search jobs by name or project...**, or choose a project in
   **Show**.
2. Choose **Open** on the job's row. Image Integration shows that job's session, in the task already open or in a new
   one.

### Follow a run

While a job runs, its row comes first. Its status reads **Running** with the stage and how far it is, such as
"Running · Registration 58 %", over a thin bar, and the count adds "1 running". **Show** brings its task forward. The
rail's item of the task shows the same progress while another place is shown.

### Read how a run ended

Each run leaves a record of how it ended in its output folder, under `.afternight\image_integration`. The list reads
its row from there: when the run ended, how long it took, the masters it left and its status. The status is
**Completed**, **Completed with warnings**, **Partly completed** when a master it planned was not made, **Failed** or
**Cancelled**, with the stage it stopped at when it knows it, such as "Failed at Stacking". A job no run has left a
record for reads **Not run**. The [Integration Report](../processes/stacking-integration/integration-report.md) says
what the run did in full.

The list is read again each time the place is shown, and when a project opens or closes.

### Keep each run with its project

While **Create a project for every integration job** is on, in [Settings](settings-dialog.md), a run started with no
project open gets one in its output folder, so that its masters, its report and its
[history](../reference/glossary.md#history) stay together, and the line under the list says so. With a project open,
the run joins it. [Projects](projects.md) says what a project keeps.

## Controls

```controls
window: Integration hub
controls:
  - name: Start
    kind: button
    default: ""
    effect: The guided integration's button. It does nothing in this version.
    enabled: Never. The card carries the tag **Planned**.
  - name: New integration
    kind: button
    default: ""
    effect: Opens Image Integration as a task, as **Process > Preprocess > Image Integration** does.
  - name: New quick integration
    kind: button
    default: ""
    effect: Opens Image Integration Lite as a task, as **Process > Preprocess > Image Integration Lite** does.
  - name: Image Calibration
    kind: button
    default: ""
    effect: Opens Image Calibration as a task. Its line reads "Apply bias, dark and flat masters to a list of frames".
  - name: Image Registration
    kind: button
    default: ""
    effect: Opens Image Registration as a task. Its line reads "Detect stars and align a list of frames".
  - name: Image Stacking
    kind: button
    default: ""
    effect: Opens Image Stacking as a task. Its line reads "Combine registered frames with rejection".
  - name: Image List Analyzer
    kind: button
    default: ""
    effect: Opens Image List Analyzer as a task. Its line reads "Inspect, blink and export a list of frames".
  - name: Count
    kind: readout
    default: "0"
    effect: How many jobs the list holds, with how many run now, such as "5 · 1 running".
    range: It counts every job, the ones the search and **Show** hide as well.
  - name: Search jobs by name or project...
    kind: field
    default: ""
    effect: Keeps the jobs whose name or project holds what you type, in any case.
  - name: Show
    object: integrationJobProjects
    kind: combo
    default: All projects
    options: [All projects, No project, The open project]
    effect: >-
      Keeps every job, the jobs kept outside any project, or the jobs of the project named. The list offers each
      project its jobs name.
    persistence: Kept while AfterNight runs. A project that no longer has a job in the list leaves it, and **All projects** is chosen again.
  - name: Status
    kind: readout
    default: ""
    effect: >-
      A tag on each row: **Not run**, **Running** with the stage and its percentage, **Completed**, **Completed with
      warnings**, **Partly completed**, **Failed** or **Cancelled**, the last two with the stage they stopped at when
      the record names it.
    note: >-
      **Frames** counts the frames of every kind the session holds. **Masters**, **Last run** and **Took** read a dash
      before the first run.
  - name: Open
    object: integrationJobOpen
    kind: button
    default: ""
    effect: Opens Image Integration on the job's session. On a running job it reads **Show**, and brings its task forward.
    enabled: Greyed for a session that could not be read, which the list shows by its name alone.
  - name: No integration job yet
    kind: readout
    default: "No integration job yet: a session of Image Integration is one."
    effect: >-
      Shown while the list is empty. When the search or **Show** hides every job, it reads "No job is named so, or is of
      that project."
  - name: Project line
    kind: readout
    default: >-
      A job run without a project creates one in its output folder (Settings › Projects), so its masters, its report
      and its history stay together.
    effect: Says what a run with no project open does.
    enabled: Shown while **Create a project for every integration job** is on.
```

## When it goes wrong

A job is missing from the list
:   **Cause.** It belongs to a project that is not open, or the search or **Show** hides it.
:   **Fix.** Open its project, clear the search and choose **All projects**.

"No job is named so, or is of that project."
:   **Cause.** The search or **Show** leaves no job.
:   **Fix.** Clear the search, or choose **All projects** in **Show**.

A job's **Open** is greyed and its row shows only a name
:   **Cause.** Its session could not be read.
:   **Fix.** Choose **New integration**, and read what Image Integration says about its stored sessions.

A job that ran reads **Not run**, with dashes
:   **Cause.** Its output folder was moved or emptied, so its run's record is not where the session says.
:   **Fix.** Put the output folder back where the job writes, or run the job again.

**Start** of the guided integration is greyed
:   **Cause.** The guided integration is planned and not in this version.
:   **Fix.** Use **New quick integration** for a short way, or **New integration** for every setting.

## Related

- [Image Integration](../processes/stacking-integration/image-integration.md), the workflow **New integration** opens.
- [Image Integration Lite](../processes/stacking-integration/image-integration-lite.md), the one-page way to stack.
- [Projects](projects.md), for the project a run is kept in.
- [Integration Report](../processes/stacking-integration/integration-report.md), for what a run did.
- [Tasks](tasks.md), for the page each process opens as.
- [The Rail and Its Places](rail-and-places.md), for the places and a running task's item.
