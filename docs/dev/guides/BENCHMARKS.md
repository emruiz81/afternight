# Benchmarks

How the engine's speed and memory are measured, and how a measurement becomes a verdict. The protocol is strict
because a workstation lies easily: tens of gigabytes of page cache, clock boost and run order can each manufacture a
result larger than the effect being measured.

## The runner and the scenario contract

| Project | Links | Role |
|---|---|---|
| `EngineBenchCommon` | no engine | The scenario catalogue, fixtures, oracles, harness and result schema |
| `AfterNightEngineBench` | `EngineBenchCommon` and the engine | Runs each scenario through the engine; also the raw-dump exporter |
| `Test_EngineBenchCommon` | `EngineBenchCommon` | The common layer's suite |
| `Test_EngineFixtureBridge` | the bridge, `EngineBenchCommon` and the engine | The bridge's suite |

`src/AfterNightBench/common` names no engine, because an oracle that judges the engine's answer must not be computed
by the engine. Its projects are class `none` in `consumer_targets.txt`, and `check_build_graph.py` holds them to
reaching no engine and the runner to linking nothing else of AfterNight's. A scenario is described once there - its
input, its seed, its correctness class, its tolerance and the oracle its answer is judged by - and the runner translates
it into the engine's calls.

The translation is `src/AfterNightBench/engine/FixtureBridge.h`, the only place a benchmark type meets an engine type.
It lives in the runner and never in the engine, so the engine never reaches the benchmark layer and the benchmark
vocabulary never ships; `check_installer_payload.py` proves no runner reaches a package. It translates and refuses, and
never repairs: a 64-bit integer fixture is refused rather than narrowed, a payload in the other byte order rather than
swapped, a semi-planar fixture rather than placed, and a stated stride is kept rather than tightened. A bridge that
repaired what it was handed would measure something the scenario did not describe and report it under the scenario's
name. The one thing it supplies is meaning: a benchmark descriptor states shape and representation and no opinion of
what the samples mean, so the scenario states its `FixtureSemantics`, with one documented default for every
representation - reading a numeric domain out of a sample type is the inference the engine's descriptors exist to end.

**Correctness comes first.** Every row's output is checked against its oracle before its time is read, and a row that
is wrong is a correctness failure, not a regression. The exact classes - `BitExact`, `ExactAfterMapping` and
`MetadataExact` - hold the output to its digest; the tolerance classes - `MaxUlp`, `AbsoluteRelative`,
`LsbDistribution`, `RmsPsnr`, `GeometricResidual` and `EstimatorTolerance` - state a positive tolerance, and the
row's error against its oracle must be at or below it.

**The harness is the repository's own.** What a publishable run needs is its manifest - the build, the dependency
lock, the host, the instruction set, the power state, the backend, the thread count, the fixture digest, the
correctness class and the per-trial samples - and a third-party harness supplies none of it; the part one would
replace, timing a body a number of times and keeping the samples, is a few dozen lines. The interleaving that matters
is between processes, which only the controller can do, and a harness linked into the timed path would be one more
thing whose version moves the numbers. The cost is that the harness runs the repetitions it is told to, with no
adaptive iteration count, so a scenario is sized against the host's clocks (see the duration floor below).

`AfterNightEngineBench` prints its options when it is given one it does not understand. The ones a measurement uses
are `--scenarios` and `--dump-scenarios` (the catalogue a run reads, and the one it would run), `--corpus-root`
(or `AFTERNIGHT_BENCH_CORPUS_ROOT`) and `--corpus-manifest`, `--repetitions`, `--warmup`, `--threads`,
`--backend` (a forced CPU backend), `--cancel-at` (the cancellation probe `cancellation_latency.py` reads),
`--keep-pipeline-scratch` and `--dry-run`, which writes a run that measures nothing. `--export <source>
--export-to <descriptor.json>` turns any file the engine decodes into a raw dump: a descriptor and the raw sample
bytes, the path by which real image data reaches a layer that has no codec of its own. Every run's manifest records
the digest of `docs/dev/dependency-lock.json`, which pins the dependency inputs a build is made from: the
submodule commits, the Qt version and both embedded-Python artifacts. It is updated in the change that moves one of
them.

## Running a measurement

The runner lives in the test solution and is built with the suites. A measurement that says one build is faster than
another is never two runs typed one after the other:

```text
python build/scripts/tools/bench_controller.py --baseline <runner> --candidate <runner> --out-dir <new-directory> --scenarios <scenarios.json> --rounds 12 --warmup 2 --seed 7 --threads <width> --host-id <host> --power-profile <recorded-profile> --clean --otherwise-idle --baseline-engine-commit <sha> --baseline-engine-clean --candidate-engine-commit <sha> --candidate-engine-clean
```

`bench_controller.py` measures two builds of the engine - a change against the commit it starts from, or against a
runtime frozen by `stretch_bench.py freeze`. It launches each runner one repetition at a time, in a seeded order that
alternates between the two, supplies identical options to both and keeps every per-trial record, so drift during the
run lands on both sides equally. It is the only thing permitted to mark a run as randomly interleaved; a runner on its
own always says it was not, and `check_interleaving_claim.py --run-dir <dir> --expect true` checks the claim from
outside. It writes one merged manifest a side, `<out-dir>/baseline.json` and `<out-dir>/candidate.json`, and the
order it ran in, `<out-dir>/schedule.json`. Timing sample `i` on the two sides came from the same pair, and a half
pair is never merged, so a paired analysis can rely on it. `--deadline-epoch` bounds an
invocation: a pair that cannot finish is not started, and only complete pairs are kept, in `*-incomplete.json`, with
exit code 4.

Rounds merge by correctness class. An exact class whose digest changes between rounds fails the run; a tolerance class
merges, and every distinct digest is recorded in the schedule under `digestDivergences`. A floating-point reduction
whose partition the scheduler chooses lands on different last bits from run to run, and an estimator that clips
iteratively can then do different amounts of work, so failing the sweep would leave a host unable to publish any
number and dropping the check would hide the finding.

**A difference is decided by the pairs, not by two medians.** A series whose dispersion is at or under the noise
threshold on both sides is decided by its median ratio, and a regression is a ratio past the host's resolution; a noisy
series is decided by an exact one-sided paired sign test over the per-pair ratios, and a series the test cannot decide
is inconclusive, never a pass. `product_measurement.py` applies that rule to the product's paired runs (its `decide`).

**The duration floor.** A row's timing decides nothing until its clocked region runs long enough for both clocks to
mean something. `prepare_hard_floor_scenarios.py` reads an unbatched pilot of each side, keeps the scenarios every
pilot executed and writes the `timedBatch` that puts the faster side's region above the floor, 150 ms by default:

```text
python build/scripts/tools/prepare_hard_floor_scenarios.py --scenarios <unbatched.json> --baseline <baseline-pilot.json> --candidate <candidate-pilot.json> --out <batched.json> --duration-floor-ms 150 --safety-factor <measured-margin>
```

The runner divides the clocked batch by that count, so results stay per operation. When one side is far slower, a
common batch repeats it many times per region: `prepare_independent_catalogues` sizes one catalogue per side from its
own pilot, differing only in `timedBatch`, for the controller's `--baseline-scenarios` and `--candidate-scenarios`.
`--include-scenario <id>` prepares a focused catalogue for rows that need more duration; it is a supplemental run, never
a way to discard a conclusive regression.

A stated `--threads` width binds the runner process-wide. Read `cpuMs` against `wallMs` per repetition before
trusting a row: the process clock also counts the harness's sampler threads, and its resolution can move a short
region. The runners take `--engine-lto` or `--engine-no-lto` from the linked engine's build recipe, never from the
compiler that built the runner - the MSVC Release engine is linked with LTO, the Linux engine without - and the
controller forwards `--baseline-engine-lto`, `--candidate-engine-no-lto` and the like to each side.

## The performance and cancellation campaigns

`build/scripts/tools/performance_matrix.txt` registers the representative rows of every scenario group the engine is
measured on. Each concrete CPU backend is forced in a run of its own with the runner's `--backend`; a row records
exactly the requested backend, and an implementation that is unavailable or inapplicable skips by name instead of
falling back. The judge takes every one of them, from one host and one clean commit:

```text
python build/scripts/tools/run_performance_campaign.py --forced Scalar=<run.json> --forced Sse2=<run.json> ... --output <bundle>/PERFORMANCE.json
```

A substituted backend, a row a run did not cover, or runs from different hosts or builds are refused rather than
averaged. The `pipelines` group runs over the reference dataset on a measurement host only and is not part of the
forced runs. The optional GPU proof has no throughput row: regenerate with `AFTERNIGHT_OPENCL=1`, require the physical
device, keep the complete `Test_EngineOpenClBackend` result, then restore the default generated tree.

`cancellation_latency.py` runs the runner once per `--cancel-at` fraction and judges each probed operation by a
budget: it passes when it returned within the budget of the stop request and either stopped with a `Cancelled` failure
or finished its work.

## The measurement hosts

Two bare-metal machines, one per platform; no result comes from a virtual machine. They are named `bench-linux` and
`bench-windows` wherever a per-host number is filed: the runner's and the controller's `--host-id`, and the
compile-time records in [budgets.json](../budgets.json).

**No absolute figure is compared across the two hosts.** Every absolute figure names its host, and a cross-platform
statement reduces to a ratio measured inside each host, or to each platform meeting its own target. Thresholds and
ceilings are recorded per host. An output digest is a reproducibility check within a host and never an equality check
across them: two toolchains, two instruction-set selections and two floating-point contraction policies give the same
commit different digests.

| | Linux host | Windows host |
|---|---|---|
| Machine | Dell Latitude 3520 laptop | desktop |
| Processor | Intel Core i7-1165G7 (Tiger Lake), 4 cores / 8 threads | AMD Ryzen 9 9950X (Zen 5), 16 cores / 32 threads |
| Memory | 31 GiB, one NUMA node; L2 1.25 MiB a core | 94 GB |
| GPU | none | NVIDIA GeForce RTX 5080, 16 GB |
| Instruction set | AVX-512; Highway selects its AVX3_DL target | AVX-512; Highway selects its AVX3_ZEN4 target |
| Process CPU clock | nanosecond resolution | 15.625 ms steps |
| Power | on mains; `powersave` governor, `balance_performance`, turbo on | Balanced plan |

**The power profile is recorded, never changed**, on both: a profile a run observed is what makes it comparable with
the next, and changing it would move every later number for a reason unrelated to the engine. The runner reads the
Windows power plan from the operating system rather than from the operator.

### The Linux host

A laptop's sustained throughput is the most common source of invented results: a machine that drops to its thermal
limit halfway through a sweep reports that limit as the engine's speed. `build/scripts/tools/linux_measurement.py` is
the protocol as a driver around the controller. It refuses a run on battery, with an unreadable governor, on a machine
already more than 10% busy, or with its scratch directory in memory, and reports every reason at once. It settles for
60 s, runs one complete pass whose results are discarded, samples the processor clock once a second over the measured
region, and judges the clock's late third against its first third: a run below 95% exits 3, to be discarded and
repeated rather than averaged in. Each reading is the fastest core's, because this host parks an idle core at 400 MHz,
and an average across cores measures how busy the machine was rather than how fast it was allowed to run.

```bash
python3 build/scripts/tools/linux_measurement.py --check-only
export TMPDIR=<a directory on persistent storage>
python3 build/scripts/tools/linux_measurement.py -- --baseline <runner> --candidate <runner> --out-dir <new-directory> --scenarios <scenarios.json> --rounds 12 --warmup 2 --seed 7 --host-id bench-linux --clean --otherwise-idle
```

**Scratch is on storage.** `/tmp` on this host is a 16 GB tmpfs, which is memory, and the reference pipeline writes
about 27 GB of calibrated and aligned frames to the temporary directory; a run that fills it keeps a fraction of its
lights. The driver refuses a tmpfs or ramfs scratch directory, and the pipeline row refuses to report a timing unless
every light survives calibration, registration and alignment.

**Noise does not fall with duration here**, so thresholds are set per scenario from that scenario's own measured spread
rather than per duration band. A multi-modal scenario - one whose CPU time moves with its wall time across trials,
because it did different amounts of work - is not gateable until its modes are explained. On a single-threaded kernel,
pinning to one physical core halves the cache-resident spread and is marginally worse at frame scale, where one core's
bandwidth is the limit; a multi-threaded benchmark does not inherit it unexamined.

### The Windows host

With 94 GB of memory the page cache is large enough that run order alone can invent a result, so interleaved,
repeated trials are mandatory here rather than advisory. Noise falls steeply with duration on this host, and the
regression thresholds follow the duration of the clocked region:

| Clocked region | Threshold |
|---|---|
| 150 ms or more | 5% |
| 10 ms to 150 ms | 15%, and the scenario is a candidate for resizing |
| under 10 ms | not gateable: report the number, never fail a run on it |

Three independent facts set the 150 ms floor. The process CPU clock advances in 15.625 ms steps, so CPU time below ten
of them is quantization; the wall-clock spread falls under 5% only somewhere above 5 ms; and a short kernel's number
moves with its repetition protocol - an operation that returns freshly allocated memory pays the first touch of its
pages on every call, so the same row reads differently launched one repetition a process and repeated inside one.
Kernel numbers are comparable only under an identical repetition protocol.

The processor clock the run manifest samples cannot show a droop here: Windows reports the nominal 4300 MHz as the
current clock, under load too, so the samples are constant by construction; the droop check matters on the laptop. The
background load beside a run is measured, not asserted: this machine idles at about 6 to 11% of its capacity, and a
sweep taken just after a build reads more, most plausibly the virus scanner reading the new binaries. At the 150 ms
floor a CPU-to-wall ratio above the worker count can be the 15.625 ms accounting step - eight busy threads have read
8.33 over 150 ms - so it needs investigating before it shows oversubscription; a confirmed concurrency-budget violation
by the engine invalidates the run.

## The real-data corpus and the reference dataset

The real-data corpus is the maintainer's own captures and calibration library. It is local-only - never committed and
never uploaded - and a result that rests on it names the dataset, so a reader can tell which results are reproducible
anywhere and which only on the measurement hosts. The runner resolves it through the corpus manifest,
`<root>/RawDumps/corpus.json` under the corpus root, whose entries carry each file's relative path, size, SHA-256 and
expected descriptor; a single-file dataset is a raw dump the runner exported, and a multi-frame one is a frame set of
the original files.

`carina-asi294mm-red`, the reference ImageIntegration dataset, is the Red channel of an ASI294MM Pro capture of the
Carina Nebula with matched calibration: 8288 x 5644 UInt16 FITS at gain 120, offset 30 and -10 C, 232 frames and
21,706,513,920 bytes.

| Role | Frames |
|---|---:|
| lights, 120 s, dithered | 72 |
| flats | 40 |
| darks, 120 s | 60 |
| bias | 60 |

It was chosen so that a measurement fails for engine reasons and never for data reasons. A 47-megapixel frame is
94 MB in UInt16 and 187 MB in Float32, so peak memory is measured where keeping 16-bit data 16-bit matters most, and
the lights stay UInt16. Every calibration stage runs, because the bias, darks and flats match the lights on gain,
offset, set point and dimensions. Seventy-two lights are enough to measure sustained scaling across the Windows host's
sixteen physical cores; dithering gives registration real transforms and drizzle a meaning; and it is one filter, one
session and one set point, with a meridian flip inside it - three of the lights are on the other side of the pier, so
registration carries a 180-degree field reversal.

A frame set resolves without reading pixels - every frame present at its recorded size - and is refused whole rather
than resolved to 231 frames, because half a calibration set is a different measurement. Its identity is a digest over
every member's recorded SHA-256 in role and frame order, so it changes when a frame is replaced, added, removed or
reordered; each frame is verified when it is read. The set names the original FITS files rather than dumps: the
pipeline takes file paths and streams its frames, and that streaming is part of what it costs. The pinned identities are
in `build/scripts/tools/product_measurement.json`: the manifest's SHA-256
`7ae7b9218e137bad19eceff55cc666deade9e85fa0c7124c6489bfffbed771cb` and the set's identity
`77ddd17bf3b34a6200f18cbf7930862ef30ca93088f27a0d12161f1421cc83c0`. Check the corpus before a long measurement - a
missing frame costs a second here and much longer inside a benchmark:

```text
AfterNightEngineBench --verify-corpus --corpus-root <root> --corpus-manifest <root>/RawDumps/corpus.json --out <scratch>/verify.json
```

It exits 1 and names the file that does not resolve. The derived inputs some rows read are prepared once, outside
timing, each pinned by SHA-256 so both hosts time the same bytes: `prepare_stacking_corpus.py` (the red flats as stack
fixtures, decoded through the runner's exporter), `prepare_final_stack_inputs.py` (the aligned lights of one pipeline
run, the frame set `carina-asi294mm-red-aligned`), `prepare_integration_corpus.py` (registered lights for the
normalization pairs and the stacks of 8, 32 and 72), `prepare_color_inputs.py` (a colour frame as encoded RGB),
`prepare_patch_denoise_inputs.py` (two whole frames and a crop of each) and `stretch_bench.py prepare` (the stretch
captures).

The product's own measurement runs the ImageIntegration workflow, full and Lite, over the same dataset in two
deployments: `product_measurement.py` archives a build of a clean commit, runs the paired protocol in its cold, warm,
incremental and cancellation cases and compares the pairs; `build/scripts/tools/product_measurement.json` pins its
dataset, workflows, cases, protocol and output contract, and `product_measurement.py check` holds the file to them.

Raw run bundles - manifests, per-trial samples and verdicts - are machine artifacts and are not committed.

## The real-time preview drag (`bench-rt-preview`)

The process dialogs' real-time preview is measured from the product's side, through a real dialog, by opt-in cases
of `Test_ImageRTPreviewControl`: `RealtimePreviewDragBenchmark.BrightnessAndContrastSliderDrag` opens the brightness
and contrast dialog over a frame, turns its preview on, presses its brightness slider, drags it a step at a time at a
steady rate, releases it, and waits for the dialog to go idle - a process whose interactive frames run over what the
preview view paints; `RealtimePreviewDragBenchmark.GammaCorrectionSliderDrag` drags the gamma correction's slider,
another, as `AdjustLChLightnessDrag` drags the LCh adjustment's lightness and `GaussianBlurSigmaDrag` the Gaussian blur's
sigma; `MedianFilterModulationDrag` drags the median filter's modulation over its five by five window, a body that
reads the pixel grid, whose frames run at full scale over the part of the frame the preview view shows - over the whole
frame, settled, while the view shows all of it; `HighPassFilterRadiusDrag` drags the high pass's radius from the one
`AN_BENCH_RADIUS` states, a blur taken away whose reach and cost grow with the radius;
`RealtimePreviewDragBenchmark.HistogramTransformationMidtonesDrag` does
the same with the histogram
transformation's midtones, a value typed a step at a time, so every frame is settled and runs over the whole frame, as
a stretch's frames do; `RealtimePreviewDragBenchmark.CurvesTransformationPointDrag` takes the middle point of the
curves transformation's curve with the left button, drags it and lets it go, as a person does, and the curve editor
tells the dialog the point is held as a slider does. They are skipped unless `AN_BENCH_IMAGE` names the
frame: a file the viewer opens, read as the viewer reads it - a camera RAW frame demosaiced by RCD - or `synthetic`, a
6000 by 4000 sixteen-bit colour frame of a starry sky the case makes. `synthetic:<representation>` makes that sky in
`uint16`, `uint32`, `int32`, `float32` or `float64` - a 32-bit integer one at the same fraction of its own full scale,
a floating one unrounded, as a stack holds it - and `synthetic:<representation>:<width>x<height>` at that size too.

| Knob | What it sets | Default |
|---|---|---|
| `AN_BENCH_IMAGE` | the frame's path, `synthetic`, or `synthetic:<representation>[:<width>x<height>]` | none: the case is skipped |
| `AN_BENCH_AUTOSTRETCH` | `1` shows the preview in autostretch | `0`, linear |
| `AN_BENCH_ZOOM100` | `1` zooms the preview view to 1:1 before the drag | `0`, fit to the window |
| `AN_BENCH_HISTOGRAM` | `0` hides the dialog's histogram before the drag | `1`, shown |
| `AN_BENCH_MASK` | `1` assigns a Float32 grey gradient mask to the frame, `2` a UInt16 one | `0`, none |
| `AN_BENCH_CHANNEL` | the dialog's channel selector entry it runs on, by its label, such as `CIE L* (Lightness)` | the whole image |
| `AN_BENCH_AREA` | `1` turns the preview area on, at the size the dialog opens it with | `0`, off, for a dialog that opens with one too |
| `AN_BENCH_APPLY` | `1` applies the dialog once the drag has settled and compares the settled preview's samples with the Apply's - within the preview area when one is on | `0`, no Apply |
| `AN_BENCH_STEPS` | the drag's steps: the brightness one value each from 1 - the slider ends at 150, and a drag of more steps holds still there and is settled before its release - the gamma a hundredth each up from 1, the lightness one each up from 0, the Gaussian's sigma a tenth each up from 2, the median's modulation a hundredth each up from 0.4, the high pass's radius a tenth each up from `AN_BENCH_RADIUS`, the midtones a thousandth each down from 0.5, and the curve's middle point a pixel each up from where it is drawn at 0.5 | `60` |
| `AN_BENCH_RADIUS` | the radius, in whole pixels, the high pass's drag starts from | `10`, the dialog's own |
| `AN_BENCH_STEP_MS` | the milliseconds between steps | `16` |
| `AN_BENCH_FPS` | the previews a second the Processing settings state, from 5 to 60 | the product's default, `30` |
| `AN_BENCH_WIDTH` | the session's width | the host's |
| `AN_BENCH_TRACE` | a file the case appends its results to, one tab-separated row per run, the header first | none |

```cmd
set AFTERNIGHT_TEST_STORAGE_ROOT=%TEMP%\at_drag
set AN_BENCH_IMAGE=synthetic
set AN_BENCH_WIDTH=8
set AN_BENCH_TRACE=%TEMP%\rt-preview.tsv
bin\msvc\release\Test_ImageRTPreviewControl.exe --gtest_filter=RealtimePreviewDragBenchmark.BrightnessAndContrastSliderDrag
```
The case prints its results and records them as its properties; it asserts only that the drag showed previews and the
dialog went idle. Each wait for a frame, a settled dialog or an Apply allows thirty of the support's ten-second
patiences, as a body whose frames run over the whole of a large frame can need. What they mean:

- `previews_per_second` is the previews shown during the drag over its length. `previews_shown_in_all` counts them
  until the dialog is idle again - for the one-second drag, previews a second - and `page_faults_in_drag` over it is
  the page faults per preview.
- `settled_frames_in_drag` and `interactive_frames_in_drag` count the previews shown by kind of frame - a held slider's
  are interactive - and `frame_mean_ms` and `frame_longest_ms` time them from a preview's start until its result is
  handed to the preview view (`ProcessRunTelemetry`). `frame_rate` is the rate the drag was paced at.
- `frame_work_mean_ms` and `frame_work_longest_ms` time a frame's work inside its admitted operation - the body, the
  histogram and the presentation - and `admission_wait_mean_ms` the wait for that admission, over the drag's frames;
  `admission_wait_longest_ms` is the longest wait since the dialog opened, its first preview's included.
- `view_frames_in_drag` counts the frames that ran over what the preview view paints - the target reduced, or a part
  of it at full scale - and `view_frame_factor` is the reduction of the last one, 1 for a part;
  `view_frames_layered_in_drag` the frames over the part the view shows painted as a layer over its presentation, and
  `preview_shown` the size of that part before the drag, in the image's pixels;
  `presentations_adopted_in_drag` the frames whose presentation the view painted as it was handed over, and
  `presentations_run_in_drag` the presentations the view still made itself. `histograms_counted_in_drag`,
  `histograms_remapped_in_drag` and `histograms_sampled_in_drag` say how the frames made the dialog's histogram: the
  count of a whole result, the target's codes remapped through the body, or the count of the body run over the target
  sampled one pixel per block. `editor_histograms_counted_in_drag` and `editor_histograms_counted_in_all` count the
  histograms the curve editor counted itself of the preview's result, during the drag and until the dialog was idle
  again - `none` for a dialog whose editor draws none. `analyses_measured` counts the target's analyses the frames
  measured for a stretched preview - none when the preview view had measured it showing the target.
- `scopes_prepared` and `scopes_reused` count the conversions of the frame's source into the encoding of a channel
  scope - CIE L* and the other components of another encoding - that frames made and kept for the frames after them,
  and the frames that ran over one kept from before; `planar_conversions_made` and `planar_conversions_reused` do the
  same for a mask or a source laid out a plane per channel in the result's representation for the mask's blend, and
  `crops_made` and `crops_reused` for the crops of the target and its mask under the preview area or the region the
  view shows, and `composite_bases_built` counts the bases of the preview area's composite the frames built - none
  when the patches are the target's own kind of image, pasted into a copy of the target. All are counted until the
  dialog is idle again.
- `patch_frames_in_drag` counts the drag's frames over the preview area shown as their patch painted over the preview
  view's own presentation, no composite of the whole image made, `histograms_derived_in_drag` their histograms derived
  from the counts of the target, of its part under the area and of the patch, and `layers_adopted_in_drag` the patches
  the view painted.
- With `AN_BENCH_APPLY`, `settled_digest` and `applied_digest` are digests of the settled preview's samples and of the
  Apply's, within the preview area when one is on, and `settled_is_applied` says whether they are the same.
  `apply_ms` is the time from the Apply until its run was committed to the target, and `apply_engine_allocated_mib`
  the bytes the engine handed out from the Apply until the dialog and the target were idle after it, every allocation
  counted once by the session's memory resource (`MemoryCounters::totalBytes`), whatever was let go meanwhile.
- `settled_frame_after_release_ms` is the time from the slider's release until the settled frame it asked for was
  handed to the preview view, and `settled_presented_after_release_ms` until the preview view had presented it too.
  The midtones have no slider to release: theirs is the time until the frame of the last step was handed over, `none`
  when it was handed over before the drag ended.
- `cores_busy_in_drag` is the process's processor time over the drag's length. The harness's GUI thread sleeps a
  millisecond between its turns of the event loop, so its own polling is a small part of it; a harness polling without
  sleeping adds about one core to every row. On the Linux host, a laptop, the sleep also slows the frames themselves:
  with the harness polling, a Brightness and Contrast frame took 6.8 ms and its work 7.8, and with it sleeping 11.8
  and 12.3, at the same rate - most likely the processor's clock and idle states, which a thread that never sleeps
  holds up - so a frame's time is compared only between runs of the same harness.
- `gui_pumps_over_4ms` to `gui_pumps_over_33ms` count the GUI thread's pumps - every slider step and every turn of the
  event loop between steps - by length, and `gui_blocked_ms` is the time spent in pumps over 16 ms.
- `page_faults_in_drag`, `page_faults_per_preview` and the working set and private bytes before and after.
  `page_faults_per_preview_after_warm_up` counts the faults from the third preview shown in the drag to its end, per
  preview shown in that time: the steady state, without the memory the drag's first previews take and the later ones
  reuse.
- `present_mean_ms` is the engine's presentation on a worker, display proxy and mask layer included,
  `pixmap_mean_ms` a pixmap made on the GUI thread, `adopt_mean_ms` the GUI thread's adoption of a presentation, and
  `presented_factor` how far the presentation painted last was reduced (`ImageViewPresentationTelemetry`).
- `proxies_reduced_in_drag`, `reduce_mean_ms` and `reduce_longest_ms` are the display proxies the preview view reduced
  on its workers and what each took, `whole_image_measured` the presentations that measured the whole image to
  stretch a proxy or a region of it, and `whole_measure_mean_ms` what one such measurement took; both are part of
  `present_mean_ms`.

A second case, `RealtimePreviewDragBenchmark.StandardViewPresentations`, takes the same frame and knobs but opens no
dialog: it presents the frame in a standard view of 1600 by 1000, fitted and at 1:1, linear and automatically
stretched, bare and under a multiply mask - each a presentation of a new revision of the frame, so nothing measured
before is reused - and prints for each the presentation's time on a worker, of which the reduction to the display
proxy and the measurement of the whole image's statistics a stretched proxy or region is presented through, then the
GUI thread's adoption and one paint. Its stretched rows are a view's first presentation of a frame, read against a
budget of 150 ms over `synthetic:float32:9504x6336` at `AN_BENCH_WIDTH=8`. A pan across the frame at 1:1 ends it.

A run is one sample of a one-second drag: compare configurations on one quiet host with no build running, and repeat a
run before reading a small difference into it.

The extension host's native window has a drag of its own in `Test_ExtensionHostNative`:
`NativeWindowPreviewBenchmark.ASliderDragThroughTheFrameSeam` opens the window over a frame, presses its slider, drags
it and releases it as the dialogs' drags do, and lends every frame's source through the shared-buffer seam - the
window's frame buffer pool, as the host lends them - to a stand-in for the extension's preview - a scale and an offset
of every sample, in C++, on the frame's worker - whose output is taken back as an extension's is. It runs when
`AN_BENCH_IMAGE` is `synthetic`, a 6016 by 4016 sixteen-bit grey frame, or `synthetic:<width>x<height>`;
`AN_BENCH_CHANNELS=3` makes the frame colour and `AN_BENCH_FLOAT=1` its samples Float32; `AN_BENCH_SCALING` is the
manifest's `full_only`, `decimated` or `native_region`, `full_only` by default; `AN_BENCH_POOL_LARGEST_MIB` (a number
or `unbounded`) and `AN_BENCH_POOL_IDLE_MIB` state the pool's bounds, the product's by default; `AN_BENCH_ZOOM100`,
`AN_BENCH_STEPS`, `AN_BENCH_STEP_MS` and `AN_BENCH_FPS` are the dialogs' knobs. It prints `previews_per_s_in_drag`,
the frames and the megapixels each lent, the view's factor, `settled_after_release_ms`, the mean time a frame spent
lending its buffers (`lend_mean_ms`), in the stand-in (`stand_in_mean_ms`) and taking the result back and giving the
buffers back (`take_mean_ms`), each step of the seam's way (`span_lend_source_ms`, `span_lend_output_ms`,
`span_lend_mask_ms`, `span_take_ms`, `span_release_ms`) and the pool's bounds; what a package's Python adds is the
stand-in's place.

What a real host adds - the extension's word on the parameters, the bridge's binding of the buffers, the Python run and
the flush of what it set on its result - is measured in `Test_ProcessFramework`:
`ExtensionHostManagerTest.TheFramesOfANativeWindowInARealHostAreTraced` runs when `AN_BENCH_HOST_FRAMES` names a number
of frames, and needs a windowing platform on which the host makes a native window: Windows' own
(`QT_QPA_PLATFORM=windows`), or X11's (`QT_QPA_PLATFORM=xcb`), under `xvfb-run` on a host with no display. It launches a
host over an extension whose `execute_preview` applies `afternight.core.apply_linear_transform` and counts its calls,
replaces the window's image that many times with a new gradient of `AN_BENCH_HOST_SIZE` (`<width>x<height>`, 640x480 by
default, small enough that what every frame pays whatever its size dominates), each replacement reaching the host as the
application sends it and previewed in a settled frame, and prints the mean of each span over the replacements' frames
with `seam_mean_ms`, every span but the extension's own `run`. It reads the host's frame trace: a host started with
`AFTERNIGHT_PREVIEW_FRAME_TRACE` naming a file writes there a header and a line a frame - the frame's width, height,
channels, representation and whether it showed a drag, then each span in microseconds, `prepare_us`, `lend_source_us`,
`lend_output_us`, `lend_mask_us`, `bind_us`, `run_us`, `flush_us`, `take_us` and `release_us` - in the application too,
which starts its hosts with its own environment, so a drag in a real extension window can be read back frame by frame.

Beside the benchmark, the same suite holds what the preview shows: `Dialogs/RealtimePreviewReferences.*` compares the
settled preview of every stretch-based dialog and of brightness and contrast - its samples, the statistics the preview
view presented them with and the dialog's histograms - exactly with the references under
`src/AfterNightTests/fixtures/references/realtime-preview/`, whose README says how they are captured.
