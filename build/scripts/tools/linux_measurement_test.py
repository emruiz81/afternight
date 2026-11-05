# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for linux_measurement.py.

The laptop's thermal protocol is the difference between a measurement and an invented result, so
what is worth testing is that the driver refuses a run the protocol forbids rather than measuring
one and reporting it. Every test drives the checks against literal readings; none of them touches
the real machine, which is the only way to exercise a drooping clock or a battery-powered run on a
machine that is neither.
"""

import io
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import linux_measurement as lm


class PreconditionTest(unittest.TestCase):
    def test_a_run_on_mains_with_a_recorded_profile_is_allowed(self):
        state = lm.HostState(acOnline=True, governor="powersave", turbo="on",
                             preference="balance_performance", loadFraction=0.01)
        self.assertEqual(lm.preconditionProblems(state), [])

    def test_a_run_on_battery_is_refused(self):
        # The one condition that silently halves a laptop's sustained throughput, and the one an
        # operator is most likely to not notice they created by unplugging to move the machine.
        state = lm.HostState(acOnline=False, governor="powersave", turbo="on",
                             preference="balance_performance", loadFraction=0.01)
        problems = lm.preconditionProblems(state)
        self.assertEqual(len(problems), 1)
        self.assertIn("battery", problems[0].lower())

    def test_a_machine_that_will_not_state_its_governor_is_refused(self):
        # Not a detail to shrug at: the profile is recorded rather than changed, so a run that
        # cannot record it has no way to say what it measured under.
        state = lm.HostState(acOnline=True, governor="", turbo="on",
                             preference="balance_performance", loadFraction=0.01)
        problems = lm.preconditionProblems(state)
        self.assertEqual(len(problems), 1)
        self.assertIn("governor", problems[0])

    def test_a_busy_machine_is_refused(self):
        state = lm.HostState(acOnline=True, governor="powersave", turbo="on",
                             preference="balance_performance", loadFraction=0.25)
        problems = lm.preconditionProblems(state)
        self.assertEqual(len(problems), 1)
        self.assertIn("busy", problems[0].lower())

    def test_problems_are_reported_together_rather_than_one_at_a_time(self):
        # An operator who has to fix three things learns about three things, not about the first.
        state = lm.HostState(acOnline=False, governor="", turbo="on", preference="", loadFraction=0.9)
        self.assertEqual(len(lm.preconditionProblems(state)), 3)

    def test_a_scratch_directory_in_memory_is_refused(self):
        # The pipeline writes tens of gigabytes of calibrated and aligned frames to the temporary
        # directory. On a distribution whose /tmp is a tmpfs that is RAM, so the run either fills
        # memory or runs out of room partway and reports a pipeline that dropped most of its
        # frames - which is what happened here before this check existed.
        state = lm.HostState(acOnline=True, governor="powersave", turbo="on",
                             preference="balance_performance", loadFraction=0.01,
                             scratchPath="/tmp", scratchFilesystem="tmpfs",
                             scratchFreeBytes=16 * 1024 ** 3)
        problems = lm.preconditionProblems(state)
        self.assertEqual(len(problems), 1)
        self.assertIn("TMPDIR", problems[0])
        self.assertIn("tmpfs", problems[0])

    def test_a_scratch_directory_on_storage_is_allowed(self):
        state = lm.HostState(acOnline=True, governor="powersave", turbo="on",
                             preference="balance_performance", loadFraction=0.01,
                             scratchPath="/home/bench/scratch", scratchFilesystem="ext4",
                             scratchFreeBytes=500 * 1024 ** 3)
        self.assertEqual(lm.preconditionProblems(state), [])

    def test_an_unknown_scratch_filesystem_is_not_refused(self):
        # Not every host publishes a mount table this can read. Refusing on "cannot tell" would
        # block a machine that is fine; the reading is reported instead.
        state = lm.HostState(acOnline=True, governor="powersave", turbo="on",
                             preference="balance_performance", loadFraction=0.01,
                             scratchPath="/scratch", scratchFilesystem="",
                             scratchFreeBytes=0)
        self.assertEqual(lm.preconditionProblems(state), [])


class ScratchTest(unittest.TestCase):
    def test_the_filesystem_of_the_longest_matching_mount_point_is_the_one_reported(self):
        mounts = [("/", "ext4"), ("/tmp", "tmpfs"), ("/home", "ext4")]
        self.assertEqual(lm.filesystemOf("/tmp/afternight-bench", mounts), "tmpfs")
        self.assertEqual(lm.filesystemOf("/home/bench/scratch", mounts), "ext4")
        self.assertEqual(lm.filesystemOf("/var/spool", mounts), "ext4")

    def test_a_path_under_no_known_mount_point_reports_nothing(self):
        self.assertEqual(lm.filesystemOf("/tmp/x", []), "")

    def test_a_mount_point_is_matched_by_whole_components(self):
        # /tmpfoo is not under /tmp, and a prefix comparison on the raw strings would say it is.
        mounts = [("/", "ext4"), ("/tmp", "tmpfs")]
        self.assertEqual(lm.filesystemOf("/tmpfoo/scratch", mounts), "ext4")

    def test_the_summary_names_the_scratch_directory_and_its_filesystem(self):
        state = lm.HostState(acOnline=True, governor="powersave", turbo="on",
                             preference="balance_performance", loadFraction=0.01,
                             scratchPath="/home/bench/scratch", scratchFilesystem="ext4",
                             scratchFreeBytes=500 * 1024 ** 3)
        text = lm.describe(state, lm.DroopVerdict(True, 0.0))
        self.assertIn("/home/bench/scratch", text)
        self.assertIn("ext4", text)
        self.assertIn("500", text)


class DroopTest(unittest.TestCase):
    def test_a_steady_clock_passes(self):
        samples = [4200.0] * 60
        verdict = lm.droopVerdict(samples)
        self.assertTrue(verdict.acceptable)
        self.assertAlmostEqual(verdict.lateShare, 1.0, places=3)

    def test_a_clock_that_fell_across_the_run_is_rejected(self):
        # The protocol's rule: a run whose clocks droop is discarded and repeated, never averaged
        # in. Averaging a droop reports the machine's thermal limit as the engine's speed.
        samples = [4200.0] * 30 + [3000.0] * 30
        verdict = lm.droopVerdict(samples)
        self.assertFalse(verdict.acceptable)
        self.assertLess(verdict.lateShare, 0.95)

    def test_a_clock_that_rose_is_not_a_droop(self):
        samples = [3000.0] * 30 + [4200.0] * 30
        self.assertTrue(lm.droopVerdict(samples).acceptable)

    def test_each_third_is_judged_by_its_fastest_reading_rather_than_its_average(self):
        """A sweep of short scenarios idles between them, and idling is not drooping.

        Every gap between two processes parks the cores, so a third's average is a mixture of the
        clock and the duty cycle. Its highest reading is the clock. A run that still reached full
        speed at the end has not drooped, however much of the end it spent idle.
        """
        early = [4200.0, 400.0, 4200.0, 400.0] * 5
        late = [400.0, 400.0, 400.0, 4200.0] * 5
        self.assertTrue(lm.droopVerdict(early + late).acceptable)

    def test_a_third_that_never_reaches_full_speed_again_is_still_a_droop(self):
        early = [4200.0, 400.0] * 10
        late = [2000.0, 400.0] * 10
        self.assertFalse(lm.droopVerdict(early + late).acceptable)

    def test_too_few_samples_is_reported_rather_than_judged(self):
        # A verdict from three readings is a guess wearing a verdict's clothes.
        verdict = lm.droopVerdict([4200.0, 4100.0, 4150.0])
        self.assertFalse(verdict.acceptable)
        self.assertIn("samples", verdict.reason)

    def test_a_host_that_reports_no_clock_is_reported_rather_than_judged(self):
        verdict = lm.droopVerdict([])
        self.assertFalse(verdict.acceptable)
        self.assertIn("no clock", verdict.reason)


class ParseTest(unittest.TestCase):
    def test_the_reading_is_the_fastest_processor_rather_than_the_average(self):
        """An average over idle cores measures duty cycle, not the clock.

        This host parks an idle core at 400 MHz. Averaging across all eight therefore reports 569
        MHz on an idle machine, 2430 with one core working and 3400 with all of them - so a sweep
        of short scenarios separated by process launches reads as a clock that collapsed by half,
        and the driver discards a run that never drooped. The fastest core answers the question the
        droop check is actually asking: can this machine still clock high?
        """
        self.assertAlmostEqual(lm.peakMegahertz(["3400000", "400000", "4000000", "400000"]), 4000.0)

    def test_a_processor_that_did_not_answer_is_skipped_rather_than_counted_as_zero(self):
        self.assertAlmostEqual(lm.peakMegahertz(["4200000", "", "4000000"]), 4200.0)

    def test_no_processor_answering_is_no_reading(self):
        self.assertIsNone(lm.peakMegahertz(["", ""]))



class SummaryTest(unittest.TestCase):
    def test_it_states_the_profile_the_run_was_taken_under(self):
        state = lm.HostState(acOnline=True, governor="powersave", turbo="on",
                             preference="balance_performance", loadFraction=0.012)
        text = lm.describe(state, lm.droopVerdict([4200.0] * 60))
        for expected in ("powersave", "balance_performance", "turbo=on", "AC"):
            self.assertIn(expected, text)


class ForwardingTest(unittest.TestCase):
    """What reaches the controller when the driver's own options are separated from it by `--`."""

    def test_the_separator_is_not_forwarded_to_the_controller(self):
        # The documented invocation puts `--` between the driver's options and the controller's.
        # argparse leaves it in the remainder, and the controller then sees `--` as its first
        # argument and rejects every option after it - so the driver was unusable exactly as
        # documented until this was checked.
        forwarded = lm.controllerArguments(["--", "--baseline", "a", "--candidate", "b"])
        self.assertEqual(forwarded, ["--baseline", "a", "--candidate", "b"])

    def test_arguments_without_a_separator_are_forwarded_unchanged(self):
        forwarded = lm.controllerArguments(["--baseline", "a", "--candidate", "b"])
        self.assertEqual(forwarded, ["--baseline", "a", "--candidate", "b"])

    def test_only_the_leading_separator_is_removed(self):
        # A later `--` belongs to whatever the controller makes of it, not to this driver.
        forwarded = lm.controllerArguments(["--", "--baseline", "a", "--", "b"])
        self.assertEqual(forwarded, ["--baseline", "a", "--", "b"])


class MainTest(unittest.TestCase):
    def test_check_only_reports_and_exits_zero_on_a_usable_machine(self):
        stream = io.StringIO()
        state = lm.HostState(acOnline=True, governor="performance", turbo="on",
                             preference="performance", loadFraction=0.0)
        code = lm.main(["--check-only"], stream=stream, readState=lambda: state)
        self.assertEqual(code, 0, stream.getvalue())
        self.assertIn("performance", stream.getvalue())

    def test_check_only_exits_non_zero_and_says_why_on_an_unusable_one(self):
        stream = io.StringIO()
        state = lm.HostState(acOnline=False, governor="powersave", turbo="on",
                             preference="", loadFraction=0.0)
        code = lm.main(["--check-only"], stream=stream, readState=lambda: state)
        self.assertEqual(code, 1)
        self.assertIn("battery", stream.getvalue().lower())


if __name__ == "__main__":
    unittest.main()
