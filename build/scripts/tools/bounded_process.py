# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Run benchmark child processes inside a wall-clock budget that stops their whole process tree.

A benchmark phase that outlives its budget is stopped rather than waited for, and stopping only the
process that was launched is not enough: a controller that has started a runner, or a launcher that
has started the real interpreter, leaves those children competing with whatever is measured next.

On Windows each child is created suspended, placed in a job object before it runs an instruction,
and resumed; every process it starts inherits the job. The deadline terminates the job, and the job
is also closed with this process, so an interrupted parent takes its tree with it. On POSIX the child
leads a new session and the deadline signals its process group.

`Budget` is the one clock an invocation spends: phases ask whether an estimate plus its reserve fits,
and children inherit the deadline as an epoch time so nested controllers stop at the same moment.
"""

import os
import signal
import subprocess
import sys
import time


#: Longest interval between progress reports. The execution policy requires one at least a minute.
MAXIMUM_TICK_SECONDS = 60.0


class Budget(object):
    """A wall-clock allowance for one invocation, measured on a monotonic clock."""

    def __init__(self, seconds, clock=time.monotonic, epoch=time.time):
        if not seconds > 0:
            raise ValueError("a budget must be positive")
        self._clock = clock
        self._epoch = epoch
        self.seconds = float(seconds)
        self.started = clock()
        self.started_epoch = epoch()

    @classmethod
    def until_epoch(cls, deadline_epoch, clock=time.monotonic, epoch=time.time):
        """The budget a child inherits from a parent's epoch deadline."""
        remaining = float(deadline_epoch) - epoch()
        budget = cls(max(remaining, 1e-9), clock=clock, epoch=epoch)
        budget.seconds = remaining
        return budget

    def elapsed(self):
        return self._clock() - self.started

    def remaining(self):
        return max(0.0, self.seconds - self.elapsed())

    def expired(self):
        return self.remaining() <= 0.0

    def deadline_monotonic(self):
        return self.started + self.seconds

    def deadline_epoch(self, reserve_seconds=0.0):
        """The deadline for a child, earlier by `reserve_seconds` so the parent can still record."""
        return self.started_epoch + self.seconds - reserve_seconds

    def fits(self, estimate_seconds, reserve=1.5):
        """Whether a phase estimated at `estimate_seconds` may start with its reserve."""
        return estimate_seconds * reserve <= self.remaining()


class BoundedResult(object):
    """How a bounded child ended. `returncode` is None when the deadline stopped it."""

    def __init__(self, returncode, timed_out, elapsed, stdout=None, stderr=None):
        self.returncode = returncode
        self.timed_out = timed_out
        self.elapsed = elapsed
        self.stdout = stdout
        self.stderr = stderr


if os.name == "nt":
    import ctypes
    from ctypes import wintypes

    _kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    _ntdll = ctypes.WinDLL("ntdll")

    _CREATE_SUSPENDED = 0x00000004
    _JOB_OBJECT_EXTENDED_LIMIT_INFORMATION = 9
    _JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x00002000
    _PROCESS_TERMINATE = 0x0001
    _PROCESS_SET_QUOTA = 0x0100
    _PROCESS_SUSPEND_RESUME = 0x0800
    _PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    _SYNCHRONIZE = 0x00100000
    _WAIT_OBJECT_0 = 0

    class _IoCounters(ctypes.Structure):
        _fields_ = [(name, ctypes.c_ulonglong) for name in
                    ("reads", "writes", "others", "readBytes", "writeBytes", "otherBytes")]

    class _BasicLimits(ctypes.Structure):
        _fields_ = [("perProcessUserTimeLimit", ctypes.c_int64), ("perJobUserTimeLimit", ctypes.c_int64),
                    ("limitFlags", wintypes.DWORD), ("minimumWorkingSetSize", ctypes.c_size_t),
                    ("maximumWorkingSetSize", ctypes.c_size_t), ("activeProcessLimit", wintypes.DWORD),
                    ("affinity", ctypes.c_size_t), ("priorityClass", wintypes.DWORD),
                    ("schedulingClass", wintypes.DWORD)]

    class _ExtendedLimits(ctypes.Structure):
        _fields_ = [("basic", _BasicLimits), ("io", _IoCounters), ("processMemoryLimit", ctypes.c_size_t),
                    ("jobMemoryLimit", ctypes.c_size_t), ("peakProcessMemoryUsed", ctypes.c_size_t),
                    ("peakJobMemoryUsed", ctypes.c_size_t)]

    _kernel32.CreateJobObjectW.restype = wintypes.HANDLE
    _kernel32.CreateJobObjectW.argtypes = (wintypes.LPVOID, wintypes.LPCWSTR)
    _kernel32.SetInformationJobObject.restype = wintypes.BOOL
    _kernel32.SetInformationJobObject.argtypes = (wintypes.HANDLE, ctypes.c_int, wintypes.LPVOID, wintypes.DWORD)
    _kernel32.OpenProcess.restype = wintypes.HANDLE
    _kernel32.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
    _kernel32.AssignProcessToJobObject.restype = wintypes.BOOL
    _kernel32.AssignProcessToJobObject.argtypes = (wintypes.HANDLE, wintypes.HANDLE)
    _kernel32.TerminateJobObject.restype = wintypes.BOOL
    _kernel32.TerminateJobObject.argtypes = (wintypes.HANDLE, wintypes.UINT)
    _kernel32.WaitForSingleObject.restype = wintypes.DWORD
    _kernel32.WaitForSingleObject.argtypes = (wintypes.HANDLE, wintypes.DWORD)
    _kernel32.CloseHandle.restype = wintypes.BOOL
    _kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
    _ntdll.NtResumeProcess.restype = ctypes.c_long
    _ntdll.NtResumeProcess.argtypes = (wintypes.HANDLE,)

    def _check(ok, what):
        if not ok:
            raise OSError(ctypes.get_last_error(), what + " failed")

    class _ProcessTree(object):
        """A job object holding one launched child and everything it starts."""

        def __init__(self, command, **popen):
            self._job = _kernel32.CreateJobObjectW(None, None)
            _check(self._job, "CreateJobObjectW")
            try:
                limits = _ExtendedLimits()
                limits.basic.limitFlags = _JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
                _check(_kernel32.SetInformationJobObject(self._job, _JOB_OBJECT_EXTENDED_LIMIT_INFORMATION,
                                                         ctypes.byref(limits), ctypes.sizeof(limits)),
                       "SetInformationJobObject")
                popen["creationflags"] = popen.get("creationflags", 0) | _CREATE_SUSPENDED
                self.process = subprocess.Popen(command, **popen)
                handle = _kernel32.OpenProcess(_PROCESS_SET_QUOTA | _PROCESS_TERMINATE | _PROCESS_SUSPEND_RESUME,
                                               False, self.process.pid)
                try:
                    _check(handle, "OpenProcess")
                    _check(_kernel32.AssignProcessToJobObject(self._job, handle), "AssignProcessToJobObject")
                    if _ntdll.NtResumeProcess(handle) != 0:
                        raise OSError(0, "NtResumeProcess failed")
                finally:
                    if handle:
                        _kernel32.CloseHandle(handle)
            except BaseException:
                self.terminate()
                self.close()
                raise

        def terminate(self):
            if self._job:
                _kernel32.TerminateJobObject(self._job, 1)

        def close(self):
            # Closing the last handle kills whatever is still in the job.
            if self._job:
                _kernel32.CloseHandle(self._job)
                self._job = None

    def wait_for_exit(pid, timeout_seconds):
        """Whether process `pid` has ended, waiting up to `timeout_seconds` for it."""
        handle = _kernel32.OpenProcess(_SYNCHRONIZE | _PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
        if not handle:
            return True
        try:
            return _kernel32.WaitForSingleObject(handle, int(timeout_seconds * 1000)) == _WAIT_OBJECT_0
        finally:
            _kernel32.CloseHandle(handle)
else:
    class _ProcessTree(object):
        """A session led by one launched child; its process group is the tree."""

        def __init__(self, command, **popen):
            popen["start_new_session"] = True
            self.process = subprocess.Popen(command, **popen)

        def terminate(self):
            try:
                os.killpg(self.process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass

        def close(self):
            self.terminate()

    def wait_for_exit(pid, timeout_seconds):
        """Whether process `pid` has ended, waiting up to `timeout_seconds` for it."""
        end = time.monotonic() + timeout_seconds
        while True:
            try:
                finished, _ = os.waitpid(pid, os.WNOHANG)
                if finished == pid:
                    return True
            except ChildProcessError:
                pass
            try:
                os.kill(pid, 0)
            except ProcessLookupError:
                return True
            if time.monotonic() >= end:
                return False
            time.sleep(0.05)


def run_bounded(command, deadline, env=None, cwd=None, stdout=None, stderr=None, stdin=subprocess.DEVNULL,
                tick_seconds=30.0, on_tick=None, clock=time.monotonic):
    """Run `command` until it exits or the monotonic `deadline`, whichever is first.

    At the deadline the whole tree is terminated and the result says so; nothing is launched when
    the deadline has already passed. `on_tick` is called at most `tick_seconds` apart while the
    child runs, which is where a caller reports elapsed time and progress.
    """
    if not 0 < tick_seconds <= MAXIMUM_TICK_SECONDS:
        raise ValueError("progress must be reported at least once a minute")
    started = clock()
    if started >= deadline:
        return BoundedResult(None, True, 0.0)
    tree = _ProcessTree(command, env=env, cwd=cwd, stdout=stdout, stderr=stderr, stdin=stdin,
                        text=stdout is subprocess.PIPE or stderr is subprocess.PIPE)
    timed_out = False
    output, errors = None, None
    try:
        while True:
            remaining = deadline - clock()
            if remaining <= 0:
                timed_out = True
                tree.terminate()
                output, errors = tree.process.communicate()
                break
            try:
                output, errors = tree.process.communicate(timeout=min(tick_seconds, remaining))
                break
            except subprocess.TimeoutExpired:
                if on_tick is not None and clock() < deadline:
                    on_tick()
    except BaseException:
        tree.terminate()
        tree.process.wait()
        raise
    finally:
        tree.close()
    return BoundedResult(None if timed_out else tree.process.returncode, timed_out, clock() - started,
                         output, errors)


def main(argv=None):
    """Run a command under `--deadline-epoch`; exit 124 when the deadline stopped it."""
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--deadline-epoch", type=float, required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    budget = Budget.until_epoch(args.deadline_epoch)
    result = run_bounded(command, budget.deadline_monotonic(),
                         on_tick=lambda: sys.stderr.write("elapsed %.0f s, remaining %.0f s\n" %
                                                          (budget.elapsed(), budget.remaining())))
    return 124 if result.timed_out else result.returncode


if __name__ == "__main__":
    sys.exit(main())
