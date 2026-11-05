# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Run the benchmark controller on the Linux host under its thermal protocol.

The laptop needs a protocol the desktop does not, because laptop sustained throughput is the most
common source of invented results in this arrangement: a machine that quietly drops to its thermal
limit halfway through a sweep reports that limit as the engine's speed, and nothing in the numbers
looks wrong afterwards.

That protocol was executed by hand for the instruction-set spike and written down in prose. Prose is
not reproducible, so this is the same protocol as a script - it refuses the run rather than measuring
one the protocol forbids, and it records the profile a run was taken under rather than changing it.

What it does, in order:

1. **Reads the machine's state and refuses a run the protocol does not allow.** On battery, an
   unreadable governor, or a machine already busy - each is reported, and all of them together
   rather than one at a time.
2. **Settles**, then burns one complete pass whose results are discarded, so the measured trials
   start from a warm machine rather than a cold one.
3. **Samples the processor clock** for the whole measured region, from `scaling_cur_freq`, which on
   this host tracks the real clock. The Windows host's equivalent does not, which is why the droop
   check lives here.
4. **Runs the controller**, which is the only component permitted to claim interleaving.
5. **Judges the droop and refuses to publish a run that drooped.** A drooping run is discarded and
   repeated, never averaged in.

Nothing here changes the governor, the energy-performance preference or the turbo state. They are
root-owned, they are what the machine runs day to day, and pinning the profile a run observed
matters more than raising it.
"""

import argparse
import glob
import os
import subprocess
import sys
import tempfile
import threading
import time

CONTROLLER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "bench_controller.py")

# The share of the machine another process may be using before a run is refused. Above this the
# measurement is partly of whatever else is running.
MAX_BACKGROUND_LOAD = 0.10

# How much of its early-run clock the run has to still hold at the end. Below this it drooped.
MIN_LATE_CLOCK_SHARE = 0.95

# Fewer readings than this is not enough to call a droop either way.
MIN_DROOP_SAMPLES = 10

# Filesystems that are memory rather than storage. The reference pipeline writes its calibrated and
# aligned frames to the temporary directory - tens of gigabytes for the reference dataset - so one
# of these is not scratch space, it is the memory the run is also measuring.
MEMORY_FILESYSTEMS = ("tmpfs", "ramfs")


class HostState(object):
    """What the machine was doing and configured to do when the run was about to start."""

    def __init__(self, acOnline, governor, turbo, preference, loadFraction,
                 scratchPath="", scratchFilesystem="", scratchFreeBytes=0):
        self.acOnline = acOnline
        self.governor = governor
        self.turbo = turbo
        self.preference = preference
        self.loadFraction = loadFraction
        self.scratchPath = scratchPath
        self.scratchFilesystem = scratchFilesystem
        self.scratchFreeBytes = scratchFreeBytes


class DroopVerdict(object):
    def __init__(self, acceptable, lateShare, reason=""):
        self.acceptable = acceptable
        self.lateShare = lateShare
        self.reason = reason


def readFirstLine(path):
    try:
        with open(path) as handle:
            return handle.readline().strip()
    except OSError:
        return ""


def readLoadFraction():
    """The share of the machine's capacity in use, from the one-minute load average."""
    try:
        one_minute = os.getloadavg()[0]
    except (OSError, AttributeError):
        return 0.0
    cores = os.cpu_count() or 1
    return one_minute / float(cores)


def readMounts():
    """(mount point, filesystem type) for every mounted filesystem, or [] when unreadable."""
    mounts = []
    try:
        with open("/proc/mounts") as handle:
            for line in handle:
                fields = line.split()
                if len(fields) >= 3:
                    mounts.append((fields[1], fields[2]))
    except OSError:
        return []
    return mounts


def filesystemOf(path, mounts):
    """The filesystem type of the mount `path` sits on, or "" when no mount point contains it.

    Matched by whole path components: `/tmpfoo` is not under `/tmp`, and comparing the raw strings
    would say it is.
    """
    best = ""
    bestDepth = -1
    target = os.path.normpath(path).split(os.sep)
    for point, filesystem in mounts:
        parts = os.path.normpath(point).split(os.sep)
        if parts == [""] or parts == ["", ""]:
            parts = [""]
        if target[:len(parts)] != parts:
            continue
        if len(parts) > bestDepth:
            best = filesystem
            bestDepth = len(parts)
    return best


def readScratchState():
    """Where the runners will write their scratch frames, and what that place actually is."""
    path = tempfile.gettempdir()
    filesystem = filesystemOf(path, readMounts())
    try:
        statistics_ = os.statvfs(path)
        free = statistics_.f_bavail * statistics_.f_frsize
    except OSError:
        free = 0
    return path, filesystem, free


def readHostState():
    online = ""
    for path in ("/sys/class/power_supply/AC/online", "/sys/class/power_supply/ACAD/online"):
        online = readFirstLine(path)
        if online:
            break
    turbo = readFirstLine("/sys/devices/system/cpu/intel_pstate/no_turbo")
    scratchPath, scratchFilesystem, scratchFree = readScratchState()
    return HostState(
        acOnline=(online == "1"),
        governor=readFirstLine("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"),
        turbo=("on" if turbo == "0" else "off" if turbo == "1" else ""),
        preference=readFirstLine("/sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference"),
        loadFraction=readLoadFraction(),
        scratchPath=scratchPath,
        scratchFilesystem=scratchFilesystem,
        scratchFreeBytes=scratchFree,
    )


def preconditionProblems(state):
    """Every reason this machine must not be measured right now, not only the first."""
    problems = []
    if not state.acOnline:
        problems.append("the machine is on battery; the protocol requires mains power for the whole run")
    if not state.governor:
        problems.append("the scaling governor could not be read, so the run could not state what it "
                        "was measured under")
    if state.loadFraction > MAX_BACKGROUND_LOAD:
        problems.append("the machine is busy: %.0f%% of its capacity is already in use, above the %.0f%% "
                        "the protocol allows" % (state.loadFraction * 100, MAX_BACKGROUND_LOAD * 100))
    if state.scratchFilesystem in MEMORY_FILESYSTEMS:
        problems.append("the scratch directory %s is a %s, which is memory and not storage; set TMPDIR "
                        "to a directory on persistent storage - the reference pipeline writes tens of "
                        "gigabytes there and a run that fills it reports a pipeline that lost most of "
                        "its frames" % (state.scratchPath, state.scratchFilesystem))
    return problems


def peakMegahertz(values):
    """The fastest of the per-processor `scaling_cur_freq` readings, in MHz.

    The fastest core, not the average of all of them. This host parks an idle core at 400 MHz, so
    an average measures how much of the machine was busy rather than how fast it was allowed to
    run: 569 MHz idle, 2430 with one core working, 3400 with all eight. A sweep of short scenarios
    separated by process launches then looks like a clock that halved, and a run that never drooped
    is discarded. The highest core answers what the droop check is for - whether the machine can
    still clock high.

    None when no processor answered.
    """
    kilohertz = []
    for value in values:
        try:
            kilohertz.append(float(value))
        except (TypeError, ValueError):
            continue
    if not kilohertz:
        return None
    return max(kilohertz) / 1000.0


def sampleMegahertz():
    paths = sorted(glob.glob("/sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_cur_freq"))
    return peakMegahertz([readFirstLine(path) for path in paths])


def droopVerdict(samples):
    """Did the clock hold up across the run, judged as late third against first third?

    An average hides a droop by construction - a run that started fast and finished slow averages to
    something plausible - so the shape is what is judged.
    """
    if not samples:
        return DroopVerdict(False, 0.0, "no clock readings: this host reports no per-processor frequency")
    if len(samples) < MIN_DROOP_SAMPLES:
        return DroopVerdict(False, 0.0, "only %d samples: too few to judge a droop either way" % len(samples))

    # The fastest reading in each third, for the same reason each reading is the fastest core: the
    # gaps between scenarios park the clock, and a mean over them measures the gaps.
    third = max(1, len(samples) // 3)
    early = max(samples[:third])
    late = max(samples[-third:])
    share = 0.0 if early == 0 else late / early
    if share < MIN_LATE_CLOCK_SHARE:
        return DroopVerdict(False, share,
                            "the clock fell to %.0f%% of its early-run value; the protocol discards a "
                            "drooping run rather than averaging it in" % (share * 100))
    return DroopVerdict(True, share)


def describe(state, verdict):
    lines = [
        "power        : %s" % ("AC" if state.acOnline else "battery"),
        "governor     : %s" % (state.governor or "unknown"),
        "preference   : %s" % (state.preference or "unknown"),
        "turbo        : turbo=%s" % (state.turbo or "unknown"),
        "background   : %.1f%% of the machine in use" % (state.loadFraction * 100),
        "scratch      : %s (%s), %.0f GiB free" % (state.scratchPath or "unknown",
                                                   state.scratchFilesystem or "unknown filesystem",
                                                   state.scratchFreeBytes / float(1024 ** 3)),
    ]
    if verdict.lateShare:
        lines.append("clock        : late third at %.1f%% of the first third" % (verdict.lateShare * 100))
    if verdict.reason:
        lines.append("clock        : %s" % verdict.reason)
    return "\n".join(lines)


class ClockSampler(object):
    """Reads the processor clock once a second for as long as the measured region lasts."""

    def __init__(self, intervalSeconds=1.0):
        self.interval = intervalSeconds
        self.samples = []
        self._running = False
        self._worker = None

    def start(self):
        self._running = True
        self._worker = threading.Thread(target=self._loop)
        self._worker.daemon = True
        self._worker.start()

    def _loop(self):
        while self._running:
            reading = sampleMegahertz()
            if reading is not None:
                self.samples.append(reading)
            time.sleep(self.interval)

    def stop(self):
        self._running = False
        if self._worker is not None:
            self._worker.join(timeout=self.interval * 3)


def controllerArguments(remainder):
    """Everything after this driver's own options, as the controller should receive it.

    The documented invocation separates the two with `--`. argparse leaves that separator in the
    remainder, and the controller then reads it as its first argument and rejects the options that
    follow, so it is dropped here. Only a leading one: a later `--` is the controller's business.
    """
    arguments = list(remainder)
    if arguments and arguments[0] == "--":
        arguments = arguments[1:]
    return arguments


def runController(arguments, stream):
    command = [sys.executable, CONTROLLER] + list(arguments)
    stream.write("running: %s\n" % " ".join(command))
    return subprocess.call(command)


def main(argv=None, stream=None, readState=None):
    stream = stream or sys.stdout
    readState = readState or readHostState

    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--check-only", dest="check_only", action="store_true")
    parser.add_argument("--settle-seconds", dest="settle", type=int, default=60)
    parser.add_argument("--skip-warmup", dest="skip_warmup", action="store_true")
    known, remainder = parser.parse_known_args(sys.argv[1:] if argv is None else argv)
    forwarded = controllerArguments(remainder)

    state = readState()
    problems = preconditionProblems(state)
    if problems:
        for problem in problems:
            stream.write("refused: %s\n" % problem)
        stream.write(describe(state, DroopVerdict(False, 0.0)) + "\n")
        return 1

    stream.write(describe(state, DroopVerdict(True, 0.0)) + "\n")
    if known.check_only:
        stream.write("the machine is usable for a measured run\n")
        return 0

    if known.settle > 0:
        stream.write("settling for %d s\n" % known.settle)
        time.sleep(known.settle)

    if not known.skip_warmup:
        stream.write("warm-up pass, results discarded\n")
        warmup = list(forwarded) + ["--rounds", "1", "--out-dir", os.path.join(os.getcwd(), "warmup-discarded")]
        if runController(warmup, stream) != 0:
            stream.write("refused: the warm-up pass failed, so nothing was measured\n")
            return 2

    sampler = ClockSampler()
    sampler.start()
    code = runController(forwarded, stream)
    sampler.stop()

    verdict = droopVerdict(sampler.samples)
    stream.write(describe(readState(), verdict) + "\n")

    if code != 0:
        stream.write("the controller failed; no result is published\n")
        return 2
    if not verdict.acceptable:
        stream.write("discard this run and repeat it: %s\n" % verdict.reason)
        return 3
    stream.write("the run held its clock and may be published\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
