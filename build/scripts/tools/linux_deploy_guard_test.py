# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""The Linux deployment guard distinguishes executable identity from compiler arguments."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
import uuid

REPO = Path(__file__).resolve().parents[3]
GUARD = REPO / 'build/scripts/linux/_ensure_afternight_not_running.sh'


@unittest.skipUnless(sys.platform.startswith('linux') and shutil.which('pgrep'), 'Linux /proc and pgrep required')
class LinuxDeployGuardTests(unittest.TestCase):
    def test_matching_include_argument_does_not_block_deployment(self):
        name = 'AfterNightGuard' + uuid.uuid4().hex
        process = subprocess.Popen([sys.executable, '-c',
            'import time; print("ready", flush=True); time.sleep(60)', '-I/tmp/' + name],
            stdout=subprocess.PIPE, text=True)
        try:
            self.assertEqual(process.stdout.readline().strip(), 'ready')
            result = subprocess.run(['bash', str(GUARD), name], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
        finally:
            process.terminate(); process.wait(timeout=5); process.stdout.close()

    def test_running_executable_blocks_deployment(self):
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / ('AfterNightGuard' + uuid.uuid4().hex)
            shutil.copy2(sys.executable, executable)
            # A copied interpreter looks for its library beside itself; an embedded one, like the
            # project's own, finds none there and exits before the guard can see it.
            environment = dict(os.environ, PYTHONHOME='%s:%s' % (sys.base_prefix, sys.base_exec_prefix))
            process = subprocess.Popen([str(executable), '-c', 'import time; time.sleep(60)'], env=environment)
            try:
                for _ in range(100):
                    if Path(f'/proc/{process.pid}/exe').resolve() == executable:
                        break
                    time.sleep(.01)
                else:
                    self.fail('Test process did not start its executable')
                result = subprocess.run(['bash', str(GUARD), executable.name], capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn(str(process.pid), result.stderr)
                # An unlinked executable is still running and must continue to block deployment.
                executable.unlink()
                result = subprocess.run(['bash', str(GUARD), executable.name], capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn(str(process.pid), result.stderr)
            finally:
                process.terminate(); process.wait(timeout=5)


if __name__ == '__main__':
    unittest.main()
