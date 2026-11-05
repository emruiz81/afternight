# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Tests for bounded_process.py: deadlines stop the exact child process tree.

Every test launches real subprocesses. A grandchild that outlives a stopped child is precisely the
failure a benchmark budget cannot tolerate, and only a real process tree can show it.
"""

import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bounded_process as bp


# A child that starts a grandchild, records both process ids, and then waits forever.
TREE = '''
import os, subprocess, sys, time
record = sys.argv[1]
grandchild = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(600)"])
with open(record + ".tmp", "w") as handle:
    handle.write("%d %d" % (os.getpid(), grandchild.pid))
os.replace(record + ".tmp", record)
time.sleep(600)
'''


class FakeClock(object):
    def __init__(self):
        self.monotonic = 100.0
        self.epoch = 1_000_000.0

    def advance(self, seconds):
        self.monotonic += seconds
        self.epoch += seconds


class BudgetTest(unittest.TestCase):
    def test_elapsed_remaining_and_child_deadline_share_one_clock(self):
        clock = FakeClock()
        budget = bp.Budget(600, clock=lambda: clock.monotonic, epoch=lambda: clock.epoch)
        clock.advance(90)
        self.assertEqual(budget.elapsed(), 90)
        self.assertEqual(budget.remaining(), 510)
        self.assertEqual(budget.deadline_epoch(), 1_000_600.0)
        self.assertEqual(budget.deadline_epoch(reserve_seconds=20), 1_000_580.0)
        self.assertEqual(budget.deadline_monotonic(), 700.0)
        clock.advance(600)
        self.assertEqual(budget.remaining(), 0)
        self.assertTrue(budget.expired())

    def test_a_phase_starts_only_when_its_estimate_and_reserve_fit(self):
        clock = FakeClock()
        budget = bp.Budget(300, clock=lambda: clock.monotonic, epoch=lambda: clock.epoch)
        self.assertTrue(budget.fits(200, reserve=1.5))
        self.assertFalse(budget.fits(201, reserve=1.5))
        clock.advance(100)
        self.assertFalse(budget.fits(140, reserve=1.5))
        with self.assertRaises(ValueError):
            bp.Budget(0)

    def test_an_inherited_epoch_deadline_becomes_a_local_monotonic_one(self):
        clock = FakeClock()
        budget = bp.Budget.until_epoch(clock.epoch + 45, clock=lambda: clock.monotonic, epoch=lambda: clock.epoch)
        self.assertEqual(budget.remaining(), 45)
        self.assertEqual(budget.deadline_monotonic(), 145.0)


class RunBoundedTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="bounded")
        self.addCleanup(shutil.rmtree, self.dir, True)
        self.script = os.path.join(self.dir, "tree.py")
        with open(self.script, "w") as handle:
            handle.write(TREE)

    def test_a_child_that_finishes_reports_its_exit_code(self):
        result = bp.run_bounded([sys.executable, "-c", "import sys; sys.exit(7)"],
                                deadline=time.monotonic() + 60)
        self.assertFalse(result.timed_out)
        self.assertEqual(result.returncode, 7)

    def test_captured_output_survives_the_bounded_wait(self):
        result = bp.run_bounded([sys.executable, "-c", "print('x' * 200000)"],
                                deadline=time.monotonic() + 60,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(len(result.stdout.strip()), 200000)

    def test_the_deadline_stops_the_child_and_its_grandchild(self):
        record = os.path.join(self.dir, "pids")
        started = time.monotonic()
        result = bp.run_bounded([sys.executable, self.script, record], deadline=started + 4)
        self.assertTrue(result.timed_out)
        self.assertIsNone(result.returncode)
        self.assertLess(time.monotonic() - started, 30)
        with open(record) as handle:
            child, grandchild = (int(value) for value in handle.read().split())
        for pid in (child, grandchild):
            self.assertTrue(bp.wait_for_exit(pid, 10), "process %d outlived the deadline" % pid)

    def test_progress_is_reported_while_a_child_runs(self):
        ticks = []
        result = bp.run_bounded([sys.executable, "-c", "import time; time.sleep(3)"],
                                deadline=time.monotonic() + 60, tick_seconds=0.5,
                                on_tick=lambda: ticks.append(time.monotonic()))
        self.assertEqual(result.returncode, 0)
        self.assertGreaterEqual(len(ticks), 3)

    def test_an_expired_deadline_launches_nothing(self):
        record = os.path.join(self.dir, "never")
        result = bp.run_bounded([sys.executable, self.script, record], deadline=time.monotonic() - 1)
        self.assertTrue(result.timed_out)
        self.assertFalse(os.path.exists(record))

    def test_a_tick_interval_above_one_minute_is_refused(self):
        with self.assertRaises(ValueError):
            bp.run_bounded([sys.executable, "-c", "pass"], deadline=time.monotonic() + 60, tick_seconds=61)


if __name__ == "__main__":
    unittest.main(verbosity=1)
