#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""The product measurement's application driver on Linux: one ImageIntegration run through the application's window.

The product measurement runs the product's workflow two ways: through AfterNightWorkflowDriver, which runs the
workflow's orchestration with no window, and through the application itself - its menus, its process windows, its
progress dialog - which is the path a person takes. This script is the second on Linux, as
product_application_driver.ps1 is on Windows: it takes the same arguments, takes the same steps and writes the same
result, so product_measurement.py pairs, compares and decides both alike.

Each run starts a display session of its own - an X server with no screen, a session bus and the accessibility bus,
and no window manager, which neither Qt's own dialogs nor its modality need - and the deployment's application in it,
with a storage root of its own and only the deployment and the system directories on its loader path. It opens Image
Integration or Image Integration Lite from the Process menu, adds each role's frames from a folder through the
application's own folder dialog, names the output directory, asks for the run, and watches for the window that says the
run ended, while a sampler reads the process's resident set every few milliseconds. It then stops the application,
tears the session down and writes the result.

It works the same against the baseline and the candidate deployment, because it drives only the windows, object names
and dialogs both keep, and it modifies neither: an archived deployment is run as it was built.

The application's widgets are driven through AT-SPI, which the application's Qt exposes, to set the run up. Inside the
measured region nothing asks the application anything except the one press that starts the run and, for the
cancellation case, the one press of Cancel: the windows the run opens are watched through the X server, which answers
for the application without involving it and is cheap enough to watch every five milliseconds, as the Windows driver
watches the window manager.

The measured region is the one a person sees: from the request - Run > Execute Integration in the full window, Run
Integration in Lite - to the message that ends the progress dialog. In the full window the preparation ends when the
execution plan is shown and the execution starts when its Run is pressed; the time the plan waits for that press is the
driver's, not the product's, and is left out. The resident peak is the largest resident set sampled inside the region,
or the process's high-water mark when it rose inside the region and is the larger. The engine's own accounting is read
from the run's log, where the workflow executor writes it.

Usage (the system's Python 3, which carries PyGObject and the AT-SPI client library; it needs Xvfb, dbus-daemon and
at-spi2-core):
    /usr/bin/python3 product_application_driver_linux.py --application <deployment>/bin/AfterNight --workflow full|lite
        --frames <dir with BIAS DARK FLAT LIGHTS> --output <workflow output dir> --storage <new dir>
        --result <result.json> [--variant 0|1] [--cancel-after-ms N] [--sampling-interval-ms N]
        [--dataset-identity <sha256>] [--timeout-seconds N]

product_measurement.py --path application launches it.
"""

from __future__ import annotations

import argparse
import ctypes
import datetime
import hashlib
import json
import os
import pathlib
import re
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
import warnings

REPOSITORY = pathlib.Path(__file__).resolve().parents[3]

FRAME_ROLES = (('Bias', 'BIAS'), ('Darks', 'DARK'), ('Flats', 'FLAT'), ('Lights', 'LIGHTS'))
COMPLETION_TITLES = ('Complete', 'Completed with Errors', 'Cancelled', 'Failed')
REFUSAL_TITLES = ('Execution Error', 'Nothing to Process', 'Registration Records', 'Import Failed', 'No Files Found')
PROGRESS_TITLE = 'Image Integration - Processing'
STACKING_SETTINGS_TITLE = 'LIGHT Integration Stacking Settings'
# What a process of the run inherits of the shell's environment: the account, the search path, the locale and the
# user's runtime directory, and nothing else. A plugin or Python path of the shell's would reach past the deployment,
# the desktop session's identity - XDG_CURRENT_DESKTOP, DESKTOP_SESSION, KDE_FULL_SESSION and their kin - would choose
# the desktop's theme for Qt, and the display, bus and session manager are the private session's.
INHERITED_VARIABLES = ('HOME', 'USER', 'LOGNAME', 'SHELL', 'PATH', 'LANG', 'LANGUAGE', 'TZ', 'TMPDIR',
                       'XDG_RUNTIME_DIR')
# Where the system's shared objects are; a mapped object anywhere else but the deployment is reported.
SYSTEM_LIBRARY_DIRECTORIES = ('/lib/', '/lib64/', '/usr/lib/', '/usr/lib64/', '/usr/local/lib/')
SHARED_OBJECT = re.compile(r'\.so(\.\d+)*$')


class DriverError(Exception):
    """The run cannot be driven or measured as asked."""


def now() -> float:
    return time.perf_counter()


def milliseconds(start: float, end: float) -> float:
    return (end - start) * 1000.0


# ---- The display session: an X server with no screen, a session bus, the accessibility bus.

def _read_line(stream_fd: int, timeout: float, what: str) -> str:
    """Reads one line a starting process writes to a pipe, refusing one that says nothing in time."""
    deadline = time.monotonic() + timeout
    data = b''
    while b'\n' not in data:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise DriverError(f'{what} did not start within {timeout:.0f} s')
        ready, _, _ = select.select([stream_fd], [], [], remaining)
        if ready:
            chunk = os.read(stream_fd, 4096)
            if not chunk:
                raise DriverError(f'{what} exited before it started')
            data += chunk
    return data.split(b'\n', 1)[0].decode().strip()


class DisplaySession:
    """One run's own X server, session bus and accessibility bus, started outside the region and torn down after it.

    Nothing of the user's desktop session reaches it: the X server listens on no network port and takes a display
    number no other server holds, the session bus is a private daemon that activates no service, and the accessibility
    bus is launched on that bus. The application asks Qt for the desktop portal's file dialogs when no platform theme is
    named; on a bus that starts no portal Qt shows its own, which the driver answers, and no wallet or desktop service
    starts on demand inside a run either."""

    BUS_CONFIGURATION = """<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <keep_umask/>
  <listen>unix:tmpdir=/tmp</listen>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
"""

    def __init__(self, directory: pathlib.Path):
        self.directory = directory
        self.processes: list[tuple[str, subprocess.Popen]] = []
        self.display = None
        self.bus = None
        self.accessibility_bus = None

    def environment(self) -> dict:
        env = {key: value for key, value in os.environ.items()
               if key in INHERITED_VARIABLES or (key.startswith('LC_') and key != 'LC_ALL')}
        if self.display:
            env['DISPLAY'] = self.display
        if self.bus:
            env['DBUS_SESSION_BUS_ADDRESS'] = self.bus
        return env

    def _spawn(self, name: str, command: list[str], **kwargs) -> subprocess.Popen:
        with open(self.directory / f'{name}.log', 'wb') as log:
            process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=kwargs.pop('stdout', log), stderr=log,
                                       env=self.environment(), start_new_session=True, **kwargs)
        self.processes.append((name, process))
        return process

    def start(self) -> None:
        self.directory.mkdir(parents=True, exist_ok=True)
        read_end, write_end = os.pipe()
        try:
            self._spawn('xvfb', ['Xvfb', '-displayfd', str(write_end), '-screen', '0', '1920x1080x24',
                                 '-nolisten', 'tcp'], pass_fds=(write_end,))
            os.close(write_end)
            write_end = -1
            self.display = ':' + _read_line(read_end, 30, 'the X server')
        finally:
            os.close(read_end)
            if write_end >= 0:
                os.close(write_end)
        configuration = self.directory / 'session-bus.conf'
        configuration.write_text(self.BUS_CONFIGURATION, encoding='ascii')
        bus = self._spawn('dbus', ['dbus-daemon', f'--config-file={configuration}', '--print-address', '--nofork'],
                          stdout=subprocess.PIPE)
        self.bus = _read_line(bus.stdout.fileno(), 30, 'the session bus')
        bus.stdout.close()
        self._spawn('atspi', ['/usr/libexec/at-spi-bus-launcher', '--launch-immediately'])
        self.accessibility_bus = self._wait_accessibility_bus(30)

    def _wait_accessibility_bus(self, timeout: float) -> str:
        """The accessibility bus's address, as its launcher answers it on the session bus once that bus is up."""
        deadline = time.monotonic() + timeout
        last = None
        while time.monotonic() < deadline:
            answer = subprocess.run(['dbus-send', '--session', '--print-reply=literal', '--dest=org.a11y.Bus',
                                     '/org/a11y/bus', 'org.a11y.Bus.GetAddress'], env=self.environment(),
                                    capture_output=True, text=True, timeout=10)
            address = answer.stdout.strip()
            if answer.returncode == 0 and address.startswith('unix:'):
                return address
            last = answer.stderr.strip()
            time.sleep(0.1)
        raise DriverError(f'the accessibility bus did not start: {last}')

    def application_environment(self, deployment: pathlib.Path, storage: pathlib.Path) -> dict:
        """What the application starts with: the session's display and bus, accessibility on, a storage root of its
        own, the C numeric locale, and only the deployment's directories before the system's on the loader path. The
        user's configuration and data directories are the storage root's too: Qt keeps its file dialog's history in
        the one, and anything that writes to the standard data location rather than the storage root writes to the
        other, so no run leaves anything in the user's own directories."""
        env = self.environment()
        env.update({'QT_QPA_PLATFORM': 'xcb', 'QT_ACCESSIBILITY': '1', 'QT_LINUX_ACCESSIBILITY_ALWAYS_ON': '1',
                    'AFTERNIGHT_TEST_STORAGE_ROOT': str(storage), 'XDG_CONFIG_HOME': str(storage / 'config'),
                    'XDG_DATA_HOME': str(storage / 'data'), 'LC_NUMERIC': 'C',
                    'LD_LIBRARY_PATH': os.pathsep.join([str(deployment), str(deployment / 'lib')])})
        return env

    def stop(self) -> None:
        """Stops every process the session started and whatever each of them started: their process groups, then any
        descendant that left its group."""
        descendants = set()
        for _, process in self.processes:
            descendants |= descendants_of(process.pid)
        for _, process in reversed(self.processes):
            stop_process_group(process)
        for pid in descendants:
            try:
                os.kill(pid, signal.SIGKILL)
            except (ProcessLookupError, PermissionError):
                pass
        self.processes = []


def descendants_of(pid: int) -> set[int]:
    """Every living process below a process, read from each process's parent in /proc."""
    parents = {}
    for entry in os.listdir('/proc'):
        if entry.isdigit():
            try:
                with open(f'/proc/{entry}/stat', encoding='ascii', errors='replace') as handle:
                    parents[int(entry)] = int(handle.read().rsplit(')', 1)[1].split()[1])
            except (OSError, ValueError, IndexError):
                continue
    found, frontier = set(), {pid}
    while frontier:
        frontier = {child for child, parent in parents.items() if parent in frontier and child not in found}
        found |= frontier
    return found


def stop_process_group(process: subprocess.Popen, grace: float = 5.0) -> None:
    """Asks a process group to end, then kills what is left of it."""
    for sig, wait in ((signal.SIGTERM, grace), (signal.SIGKILL, grace)):
        try:
            os.killpg(process.pid, sig)
        except ProcessLookupError:
            break
        try:
            process.wait(wait)
            break
        except subprocess.TimeoutExpired:
            continue
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass


# ---- The X server's windows, read through Xlib: cheap enough to watch inside the measured region.

class _XWindowAttributes(ctypes.Structure):
    _fields_ = [('x', ctypes.c_int), ('y', ctypes.c_int), ('width', ctypes.c_int), ('height', ctypes.c_int),
                ('border_width', ctypes.c_int), ('depth', ctypes.c_int), ('visual', ctypes.c_void_p),
                ('root', ctypes.c_ulong), ('class_', ctypes.c_int), ('bit_gravity', ctypes.c_int),
                ('win_gravity', ctypes.c_int), ('backing_store', ctypes.c_int), ('backing_planes', ctypes.c_ulong),
                ('backing_pixel', ctypes.c_ulong), ('save_under', ctypes.c_int), ('colormap', ctypes.c_ulong),
                ('map_installed', ctypes.c_int), ('map_state', ctypes.c_int), ('all_event_masks', ctypes.c_long),
                ('your_event_mask', ctypes.c_long), ('do_not_propagate_mask', ctypes.c_long),
                ('override_redirect', ctypes.c_int), ('screen', ctypes.c_void_p)]


class _XStructureEvent(ctypes.Structure):
    """The head every structure event shares: CreateNotify names the parent where the others name the event window,
    and the window the event is about follows."""
    _fields_ = [('type', ctypes.c_int), ('serial', ctypes.c_ulong), ('send_event', ctypes.c_int),
                ('display', ctypes.c_void_p), ('event', ctypes.c_ulong), ('window', ctypes.c_ulong)]


class _XPropertyEvent(ctypes.Structure):
    _fields_ = [('type', ctypes.c_int), ('serial', ctypes.c_ulong), ('send_event', ctypes.c_int),
                ('display', ctypes.c_void_p), ('window', ctypes.c_ulong), ('atom', ctypes.c_ulong),
                ('time', ctypes.c_ulong), ('state', ctypes.c_int)]


_CREATE_NOTIFY, _DESTROY_NOTIFY, _UNMAP_NOTIFY, _MAP_NOTIFY, _PROPERTY_NOTIFY = 16, 17, 18, 19, 28
_STRUCTURE_NOTIFY_MASK, _SUBSTRUCTURE_NOTIFY_MASK, _PROPERTY_CHANGE_MASK = 1 << 17, 1 << 19, 1 << 22
_IS_UNMAPPED = 0
_ANY_PROPERTY_TYPE = 0
_XA_CARDINAL, _XA_WM_NAME = 6, 39


class XWindows:
    """The application's top-level windows as the X server knows them: each one's title and whether it is mapped.

    The server reports every window created on the root, and every map, unmap, destruction and title change of each
    window followed since, as events; a look at the windows reads the events already queued on the connection and asks
    the server only about a window it has not seen before. The application is never involved."""

    def __init__(self, display: str, pid: int):
        self.pid = pid
        self.x = ctypes.CDLL('libX11.so.6')
        x = self.x
        x.XOpenDisplay.restype = ctypes.c_void_p
        x.XOpenDisplay.argtypes = [ctypes.c_char_p]
        x.XDefaultRootWindow.restype = ctypes.c_ulong
        x.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
        x.XInternAtom.restype = ctypes.c_ulong
        x.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        x.XSelectInput.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_long]
        x.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong),
                                 ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)),
                                 ctypes.POINTER(ctypes.c_uint)]
        x.XGetWindowAttributes.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(_XWindowAttributes)]
        x.XGetWindowProperty.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_long,
                                         ctypes.c_long, ctypes.c_int, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong),
                                         ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_ulong),
                                         ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_void_p)]
        x.XFree.argtypes = [ctypes.c_void_p]
        x.XPending.argtypes = [ctypes.c_void_p]
        x.XNextEvent.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        x.XCloseDisplay.argtypes = [ctypes.c_void_p]
        # A window can go between the event that names it and the question about it; the server's answer that it is
        # gone is expected, not fatal.
        self._error_handler = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p)(lambda _d, _e: 0)
        x.XSetErrorHandler(self._error_handler)
        self.dpy = x.XOpenDisplay(display.encode())
        if not self.dpy:
            raise DriverError(f'the X server {display} cannot be opened')
        self.root = x.XDefaultRootWindow(self.dpy)
        self.net_wm_pid = x.XInternAtom(self.dpy, b'_NET_WM_PID', 0)
        self.net_wm_name = x.XInternAtom(self.dpy, b'_NET_WM_NAME', 0)
        self.utf8_string = x.XInternAtom(self.dpy, b'UTF8_STRING', 0)
        self.windows: dict[int, dict] = {}
        self._event = (ctypes.c_long * 24)()
        x.XSelectInput(self.dpy, self.root, _SUBSTRUCTURE_NOTIFY_MASK)
        self._follow_tree(self.root)

    def close(self) -> None:
        if self.dpy:
            self.x.XCloseDisplay(self.dpy)
            self.dpy = None

    def _children(self, window: int) -> list[int]:
        root, parent = ctypes.c_ulong(), ctypes.c_ulong()
        children = ctypes.POINTER(ctypes.c_ulong)()
        count = ctypes.c_uint()
        if not self.x.XQueryTree(self.dpy, window, ctypes.byref(root), ctypes.byref(parent), ctypes.byref(children),
                                 ctypes.byref(count)):
            return []
        found = [children[i] for i in range(count.value)]
        if children:
            self.x.XFree(children)
        return found

    def _follow_tree(self, window: int) -> None:
        """Follows every window below the root two levels down: the application's own top-level windows, and its
        windows inside a window manager's frames when one reparents them."""
        for child in self._children(window):
            self._follow(child)
            if window == self.root:
                for grandchild in self._children(child):
                    self._follow(grandchild)

    def _property(self, window: int, atom: int, kind: int) -> tuple[int, int, bytes] | None:
        actual_type, actual_format = ctypes.c_ulong(), ctypes.c_int()
        items, after = ctypes.c_ulong(), ctypes.c_ulong()
        data = ctypes.c_void_p()
        status = self.x.XGetWindowProperty(self.dpy, window, atom, 0, 1024, 0, kind, ctypes.byref(actual_type),
                                           ctypes.byref(actual_format), ctypes.byref(items), ctypes.byref(after),
                                           ctypes.byref(data))
        if status != 0 or not data:
            return None
        try:
            if actual_format.value == 32:
                values = (ctypes.c_long * items.value).from_address(data.value)
                return actual_type.value, 32, b''.join(int(v).to_bytes(8, 'little', signed=True) for v in values)
            size = items.value * (actual_format.value // 8)
            return actual_type.value, actual_format.value, ctypes.string_at(data.value, size)
        finally:
            self.x.XFree(data)

    def _pid_of(self, window: int) -> int | None:
        value = self._property(window, self.net_wm_pid, _XA_CARDINAL)
        if not value or value[1] != 32 or len(value[2]) < 8:
            return None
        return int.from_bytes(value[2][:8], 'little', signed=True)

    def _title_of(self, window: int) -> str:
        value = self._property(window, self.net_wm_name, self.utf8_string)
        if value and value[2]:
            return value[2].decode('utf-8', 'replace')
        value = self._property(window, _XA_WM_NAME, _ANY_PROPERTY_TYPE)
        return value[2].decode('latin-1', 'replace') if value and value[2] else ''

    def _follow(self, window: int) -> None:
        """Asks for the window's own structure and property events first, then reads its state, so no change after
        the reading is missed."""
        if window in self.windows:
            return
        self.x.XSelectInput(self.dpy, window, _STRUCTURE_NOTIFY_MASK | _PROPERTY_CHANGE_MASK)
        attributes = _XWindowAttributes()
        if not self.x.XGetWindowAttributes(self.dpy, window, ctypes.byref(attributes)):
            return
        self.windows[window] = {'pid': self._pid_of(window), 'title': self._title_of(window),
                                'mapped': attributes.map_state != _IS_UNMAPPED}

    def _drain(self) -> None:
        while self.x.XPending(self.dpy) > 0:
            self.x.XNextEvent(self.dpy, self._event)
            kind = self._event[0] & 0xffffffff
            if kind == _PROPERTY_NOTIFY:
                event = _XPropertyEvent.from_buffer(self._event)
                known = self.windows.get(event.window)
                if known is not None and event.atom in (self.net_wm_name, _XA_WM_NAME):
                    known['title'] = self._title_of(event.window)
                elif known is not None and event.atom == self.net_wm_pid:
                    known['pid'] = self._pid_of(event.window)
                continue
            event = _XStructureEvent.from_buffer(self._event)
            if kind == _CREATE_NOTIFY:
                self._follow(event.window)
            elif kind == _DESTROY_NOTIFY:
                self.windows.pop(event.window, None)
            elif kind in (_MAP_NOTIFY, _UNMAP_NOTIFY):
                self._follow(event.window)
                known = self.windows.get(event.window)
                if known is not None:
                    known['mapped'] = kind == _MAP_NOTIFY
                    if known['mapped']:
                        known['title'] = self._title_of(event.window)
                        if known['pid'] is None:
                            known['pid'] = self._pid_of(event.window)

    def shown(self) -> list[tuple[int, str]]:
        """The application's mapped windows and their titles."""
        self._drain()
        return [(window, state['title']) for window, state in self.windows.items()
                if state['mapped'] and state['pid'] == self.pid]

    def find(self, titles) -> tuple[int, str] | None:
        for window, title in self.shown():
            if title in titles:
                return window, title
        return None


# ---- What the process has used, read from the kernel's account of it.

def _status_fields(text: str) -> dict[str, int]:
    """The memory lines of /proc/<pid>/status, in bytes."""
    fields = {}
    for line in text.splitlines():
        name, _, value = line.partition(':')
        parts = value.split()
        if len(parts) == 2 and parts[1] == 'kB':
            fields[name] = int(parts[0]) * 1024
    return fields


class ProcessAccount:
    """The application's resident set, high-water mark, anonymous memory, processor time and mapped objects."""

    def __init__(self, pid: int):
        self.pid = pid
        self.ticks = os.sysconf('SC_CLK_TCK')

    def memory(self) -> tuple[int, int, int]:
        """The resident set (VmRSS), its high-water mark (VmHWM) and the private bytes, as resident anonymous memory
        and its swapped-out pages (RssAnon + VmSwap)."""
        with open(f'/proc/{self.pid}/status', encoding='ascii') as handle:
            fields = _status_fields(handle.read())
        return fields.get('VmRSS', 0), fields.get('VmHWM', 0), fields.get('RssAnon', 0) + fields.get('VmSwap', 0)

    def cpu_seconds(self) -> float:
        """User and system time of every thread of the process, from /proc/<pid>/stat."""
        with open(f'/proc/{self.pid}/stat', encoding='ascii') as handle:
            fields = handle.read().rsplit(')', 1)[1].split()
        return (int(fields[11]) + int(fields[12])) / self.ticks

    def shared_objects(self) -> set[str]:
        with open(f'/proc/{self.pid}/maps', encoding='utf-8', errors='replace') as handle:
            paths = {line.split(None, 5)[5].strip() for line in handle if len(line.split(None, 5)) == 6}
        return {path for path in paths if path.startswith('/') and SHARED_OBJECT.search(path.replace(' (deleted)', ''))}


def classify_shared_objects(paths, deployment: pathlib.Path, checkout: pathlib.Path) -> dict:
    """Sorts mapped shared objects into the deployment's, the system's and the rest; the ones inside the checkout - a
    development library the loader found through a binary's RUNPATH - are named apart."""
    # Compared with forward slashes, so a directory is its prefix on any host the driver's reading is tested on.
    def spelled(path) -> str:
        return pathlib.PurePath(path).as_posix()

    deployment_prefix = spelled(deployment).rstrip('/') + '/'
    checkout_prefix = spelled(checkout).rstrip('/') + '/'
    deployed, system, outside = [], [], []
    for path in sorted(paths):
        if spelled(path).startswith(deployment_prefix):
            deployed.append(path)
        elif spelled(path).startswith(SYSTEM_LIBRARY_DIRECTORIES):
            system.append(path)
        else:
            outside.append(path)
    return {'deployment': deployed, 'system': system, 'outside': outside,
            'checkout': [path for path in outside if spelled(path).startswith(checkout_prefix)]}


class Sampler:
    """The resident set of the process, read every interval on a thread of its own."""

    def __init__(self, pid: int, interval_ms: int):
        self.pid = pid
        self.interval = max(1, interval_ms) / 1000.0
        self.times: list[float] = []
        self.bytes: list[int] = []
        self.lock = threading.Lock()
        self.stopping = threading.Event()
        self.thread = threading.Thread(target=self._run, name='sampler', daemon=True)

    def start(self) -> None:
        self.thread.start()

    def stop(self) -> None:
        self.stopping.set()
        self.thread.join()

    def _run(self) -> None:
        try:
            descriptor = os.open(f'/proc/{self.pid}/status', os.O_RDONLY)
        except OSError:
            return
        try:
            while not self.stopping.is_set():
                try:
                    text = os.pread(descriptor, 16384, 0).decode('ascii', 'replace')
                except OSError:
                    return
                match = re.search(r'^VmRSS:\s+(\d+) kB', text, re.MULTILINE)
                if match:
                    with self.lock:
                        self.times.append(now())
                        self.bytes.append(int(match.group(1)) * 1024)
                self.stopping.wait(self.interval)
        finally:
            os.close(descriptor)

    def peak_between(self, start: float, end: float) -> tuple[int, int]:
        """The largest reading between two moments, and how many readings fell between them."""
        peak = count = 0
        with self.lock:
            for moment, value in zip(self.times, self.bytes):
                if start <= moment <= end:
                    count += 1
                    peak = max(peak, value)
        return peak, count


# ---- The application's widgets, read through AT-SPI: used to set the run up, and once to start or stop it.

# Branches a search never enters unless it looks for them: their items are the frames, the files of a folder or the
# runs of a history, many hundreds of objects each answered by the application over the bus.
_LEAF_ROLES = frozenset(('table', 'tree table', 'tree', 'list', 'list box', 'table cell', 'table row'))


class Accessibility:
    """The application's accessible objects on the session's accessibility bus."""

    def __init__(self, pid: int, bus_address: str, check_alive):
        import gi
        gi.require_version('Atspi', '2.0')
        from gi.repository import Atspi, Gio, GLib
        self.Atspi, self.Gio, self.GLib = Atspi, Gio, GLib
        self.pid = pid
        self.check_alive = check_alive
        Atspi.init()
        # A question the application answers on its GUI thread can wait behind a moment of work there - an import
        # applied, a tab built - longer than the client library's default 0.8 s.
        Atspi.set_timeout(5000, 15000)
        self.application = None
        # Actions go out on a connection of their own and wait for no answer: an action that opens a modal dialog is
        # answered only when the dialog closes, and a call waiting on the client library's connection holds every other
        # question behind it until it times out.
        self.actions = Gio.DBusConnection.new_for_address_sync(
            bus_address, Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
            None, None)

    def wait(self, find, what: str, timeout: float, interval: float = 0.2):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                found = find()
            except Exception:  # an object can go while it is read; the next look finds the tree as it is then
                found = None
            if found:
                return found
            self.check_alive()
            time.sleep(interval)
        raise DriverError(f'{what} did not appear')

    def attach(self, timeout: float) -> None:
        def find():
            desktop = self.Atspi.get_desktop(0)
            for index in range(desktop.get_child_count()):
                candidate = desktop.get_child_at_index(index)
                if candidate is not None and candidate.get_process_id() == self.pid:
                    return candidate
            return None
        self.application = self.wait(find, 'the application on the accessibility bus', timeout, 0.5)

    @staticmethod
    def role(node) -> str:
        try:
            return node.get_role_name()
        except Exception:
            return ''

    @staticmethod
    def name(node) -> str:
        try:
            return node.get_name() or ''
        except Exception:
            return ''

    @staticmethod
    def identifier(node) -> str:
        try:
            return node.get_accessible_id() or ''
        except Exception:
            return ''

    def has_state(self, node, state: str) -> bool:
        try:
            return node.get_state_set().contains(getattr(self.Atspi.StateType, state.upper()))
        except Exception:
            return False

    @staticmethod
    def children(node) -> list:
        try:
            count = node.get_child_count()
        except Exception:
            return []
        found = []
        for index in range(count):
            try:
                child = node.get_child_at_index(index)
            except Exception:
                child = None
            if child is not None:
                found.append(child)
        return found

    def windows(self) -> list:
        """The application's top-level windows."""
        return self.children(self.application)

    def find(self, scope, role: str | None = None, name: str | None = None, identifier: str | None = None,
             showing: bool = True, enter=_LEAF_ROLES):
        """The first object below a scope of a role whose name is a name or whose accessible id - the object names of
        it and its parents, as UI Automation's automation id - ends with an identifier. Only the branches a person sees
        are searched when `showing`."""
        stack = list(reversed(self.children(scope)))
        while stack:
            node = stack.pop()
            if showing and not self.has_state(node, 'showing'):
                continue
            node_role = self.role(node)
            if (role is None or node_role == role) and (name is None or self.name(node) == name) \
                    and (identifier is None or self.identifier(node).split('.')[-1] == identifier):
                return node
            if node_role in enter and node_role != role:
                continue
            stack.extend(reversed(self.children(node)))
        return None

    def window(self, identifier: str | None = None, name: str | None = None):
        for node in self.windows():
            if identifier is not None and self.identifier(node).split('.')[-1] == identifier:
                return node
            if name is not None and self.name(node) == name:
                return node
        return None

    def action_names(self, node) -> list[str]:
        try:
            with warnings.catch_warnings():
                # The name call this client library offers is deprecated in favour of one its bindings do not carry.
                warnings.simplefilter('ignore', DeprecationWarning)
                return [self.Atspi.Action.get_action_name(node, index)
                        for index in range(self.Atspi.Action.get_n_actions(node))]
        except Exception:
            return []

    def action(self, node, action: str = 'Press') -> tuple[str, str, int]:
        """What performing an action sends, read once, so a press inside the region asks the application nothing."""
        names = self.action_names(node)
        if action not in names:
            raise DriverError(f'{self.role(node)} {self.name(node)!r} has no {action} action, only {names}')
        return node.app.bus_name, node.path, names.index(action)

    def send(self, prepared: tuple[str, str, int]) -> None:
        """Performs a prepared action and returns once the request is on its way; its answer is not waited for."""
        bus_name, path, index = prepared
        self.actions.call(bus_name, path, 'org.a11y.atspi.Action', 'DoAction', self.GLib.Variant('(i)', (index,)), None,
                          self.Gio.DBusCallFlags.NONE, -1, None, None)
        self.actions.flush_sync(None)

    def act(self, node, action: str = 'Press') -> None:
        self.send(self.action(node, action))

    def menu_item(self, scope, path: list[str]):
        """The last item of a menu path below a window's menu bar, found without opening the menus: a person's choice
        of the item is its action, which the item's press triggers."""
        bar = self.find(scope, role='menu bar')
        if bar is None:
            raise DriverError('the window has no menu bar')
        node = bar
        for index, label in enumerate(path):
            found = None
            for candidate in self.children(node):
                if self.role(candidate) == 'popup menu':
                    candidate = next((item for item in self.children(candidate)
                                      if self.role(item) == 'menu item' and self.name(item) == label), None)
                    if candidate is not None:
                        found = candidate
                        break
                elif self.role(candidate) == 'menu item' and self.name(candidate) == label:
                    found = candidate
                    break
            if found is None:
                raise DriverError(f'no menu item {" > ".join(path[:index + 1])}')
            node = found
        return node

    def text(self, node) -> str:
        try:
            return self.Atspi.Text.get_text(node, 0, self.Atspi.Text.get_character_count(node))
        except Exception:
            return self.name(node)

    def set_text(self, node, text: str) -> None:
        if not self.Atspi.EditableText.set_text_contents(node, text):
            raise DriverError(f'{self.role(node)} {self.identifier(node)!r} refused its text')


# ---- One run through the application's window.

def _same_path(shown: str, expected: pathlib.Path) -> bool:
    try:
        return os.path.normpath(os.path.abspath(shown.strip())) == os.path.normpath(str(expected))
    except (TypeError, ValueError):
        return False


class ApplicationDriver:
    """Drives one run through the application's window and measures it."""

    def __init__(self, arguments):
        self.arguments = arguments
        self.origin = now()
        self.events: list[str] = []
        self.process = None
        self.x = None
        self.ax = None

    def note(self, text: str) -> None:
        line = f'{milliseconds(self.origin, now()):9.1f} ms {text}'
        self.events.append(line)
        print(line, flush=True)

    def check_alive(self) -> None:
        if self.process is not None and self.process.poll() is not None:
            raise DriverError(f'the application exited with code {self.process.returncode}')

    # The windows, as the X server shows them.

    def wait_window(self, titles, timeout: float, poll: float):
        deadline = now() + timeout
        while now() < deadline:
            found = self.x.find(titles)
            if found:
                return found
            self.check_alive()
            time.sleep(poll)
        return None

    def wait_window_gone(self, titles, timeout: float) -> bool:
        deadline = now() + timeout
        while now() < deadline:
            if not self.x.find(titles):
                return True
            self.check_alive()
            time.sleep(0.05)
        return False

    # The widgets, through AT-SPI.

    def window(self, identifier: str, timeout: float):
        """A process window, top-level or docked in the main window."""
        def find():
            found = self.ax.window(identifier=identifier)
            if found is not None:
                return found
            main = self.ax.window(identifier='afterNightMainWindow')
            return None if main is None else self.ax.find(main, identifier=identifier)
        return self.ax.wait(find, identifier, timeout)

    def element(self, scope, what: str, timeout: float = 10.0, **criteria):
        return self.ax.wait(lambda: self.ax.find(scope, **criteria), what, timeout)

    def select_tab(self, scope, name: str) -> None:
        tab = self.element(scope, f'the tab {name}', role='page tab', name=name)
        self.ax.act(tab)
        self.ax.wait(lambda: self.ax.has_state(tab, 'selected'), f'the tab {name} selected', 10)
        time.sleep(0.3)

    def answer_folder_dialog(self, path: pathlib.Path, timeout: float = 20.0) -> None:
        """Answers Qt's own folder dialog with a path: its name box is given the path, which it is read back to hold,
        and the dialog is accepted; it is done when its window is gone."""
        def find():
            for node in self.ax.windows():
                if self.ax.identifier(node).split('.')[-1] == 'QFileDialog' and self.ax.has_state(node, 'showing'):
                    return node
            return None
        dialog = self.ax.wait(find, 'a folder dialog', timeout)
        title = self.ax.name(dialog)
        self.note(f"dialog '{title}' for {path}")
        time.sleep(0.3)
        edit = self.element(dialog, "the dialog's name box", identifier='fileNameEdit')
        self.ax.set_text(edit, str(path))
        shown = self.ax.text(edit)
        if not _same_path(shown, path):
            raise DriverError(f"the dialog's name box reads '{shown}', not '{path}'")
        accept = self.element(dialog, "the dialog's Choose", role='button', name='Choose')
        self.ax.act(accept)
        if not self.wait_window_gone([title], 10):
            raise DriverError(f'the folder dialog did not accept {path}')

    def dismiss_box(self, title: str) -> str:
        """Reads and closes a message box the application shows, by its title, with its OK."""
        box = self.ax.wait(lambda: self.ax.window(name=title), f"the box '{title}'", 10)
        text = ' '.join(self.ax.name(node) for node in self.ax.children(box) if self.ax.role(node) == 'label')
        button = self.ax.find(box, role='button', name='OK')
        if button is None:
            raise DriverError(f"the box '{title}' has no OK")
        self.ax.act(button)
        self.wait_window_gone([title], 10)
        self.note(f"dismissed '{title}'")
        return text

    def import_role(self, window, tab: str, folder: pathlib.Path) -> None:
        """Adds a role's folder as a person adds it to the role's tab: the window's Frames > Add Folder... (This Folder
        Only) > Add <role>..., which runs what the tab's own Add Folder button runs."""
        self.select_tab(window, tab)
        self.ax.act(self.ax.menu_item(window, ['Frames', 'Add Folder... (This Folder Only)', f'Add {tab}...']))
        self.answer_folder_dialog(folder)
        # The import runs behind a progress dialog and ends in a box that says what it added.
        deadline = now() + 600
        last_progress = now()
        box = None
        while now() < deadline:
            if self.x.find(['Importing files...']):
                last_progress = now()
            else:
                box = self.x.find(['Import Complete', 'Import Issues'])
                if box or now() - last_progress > 3.0:
                    break
            refusal = self.x.find(REFUSAL_TITLES)
            if refusal:
                raise DriverError(f"the import said '{refusal[1]}'")
            self.check_alive()
            time.sleep(0.1)
        else:
            raise DriverError(f'the import of {folder} did not end')
        if box:
            summary = self.dismiss_box(box[1])
            expected = sum(1 for entry in folder.iterdir() if entry.is_file())
            imported = re.search(r'Imported: (\d+)', summary)
            if box[1] != 'Import Complete' or not imported or int(imported.group(1)) != expected:
                raise DriverError(f"the import of {folder} said '{summary}', not {expected} frames")
            time.sleep(0.5)
        refusal = self.x.find(REFUSAL_TITLES)
        if refusal:
            raise DriverError(f"the import said '{refusal[1]}'")
        self.note(f'{tab} imported')

    # The run.

    def set_up_full(self, main, frames: pathlib.Path, output: pathlib.Path, variant: int):
        """Opens Image Integration, adds each role's folder, names the output directory and, for the incremental
        variant, changes the light stack; returns the Execute Integration item, found and not yet pressed."""
        self.ax.act(self.ax.menu_item(main, ['Process', 'Preprocess', 'Image Integration']))
        window = self.window('ImageIntegrationProcess', 60)
        self.note('Image Integration shown')
        for tab, folder in FRAME_ROLES:
            self.import_role(window, tab, frames / folder)
        browse = self.element(window, 'the output directory button', identifier='outputDirectoryBrowseButton')
        self.ax.act(browse)
        self.answer_folder_dialog(output)
        time.sleep(0.5)
        edit = self.element(window, 'the output directory', identifier='outputDirectoryEdit')
        shown = self.ax.text(edit)
        if not _same_path(shown, output):
            raise DriverError(f"the output directory reads '{shown}', not '{output}'")
        if variant:
            self.change_light_stack(window)
        time.sleep(1.0)
        return self.ax.action(self.ax.menu_item(window, ['Run', 'Execute Integration']))

    def change_light_stack(self, window) -> None:
        """The incremental case changes the light stack alone, as a person changes it: the group's settings on the
        Stacking tab, its high rejection sigma 0.1 above what the window shows."""
        self.select_tab(window, 'Workflow')
        self.select_tab(window, 'Stacking')
        table = self.element(window, 'the integration groups', identifier='tblIntegrationGroups')
        if not self.ax.Atspi.Table.add_row_selection(table, 0):
            raise DriverError('the first integration group cannot be selected')
        self.note('the first integration group selected')
        time.sleep(0.3)
        settings = self.element(window, 'the stacking settings button', identifier='btnStackingSettings')
        self.ax.act(settings)
        if not self.wait_window([STACKING_SETTINGS_TITLE], 20, 0.05):
            raise DriverError('the light stacking settings did not open')
        self.note('the light stacking settings shown')
        dialog = self.ax.wait(lambda: self.ax.window(name=STACKING_SETTINGS_TITLE), 'the stacking settings', 10)
        sigma = self.element(dialog, 'the high sigma', identifier='sigmaHighValue')
        default = self.ax.Atspi.Value.get_current_value(sigma)
        self.ax.Atspi.Value.set_current_value(sigma, round(default + 0.1, 2))
        time.sleep(0.3)
        self.note(f'high sigma {default:g} -> {self.ax.Atspi.Value.get_current_value(sigma):g}')
        ok = self.element(dialog, "the settings' OK", role='button', name='OK')
        self.ax.act(ok)
        if not self.wait_window_gone([STACKING_SETTINGS_TITLE], 20):
            raise DriverError('the settings did not close')

    def set_up_lite(self, main, frames: pathlib.Path, output: pathlib.Path, variant: int):
        """Opens Image Integration Lite, names the output directory and adds the four role folders; returns its Run
        Integration, found and not yet pressed."""
        self.ax.act(self.ax.menu_item(main, ['Process', 'Preprocess', 'Image Integration Lite']))
        window = self.window('ImageIntegrationLiteProcess', 60)
        self.note('Image Integration Lite shown')
        if variant:
            raise DriverError('the Lite window has no driven variant yet')
        edit = self.element(window, 'the output directory', identifier='liteOutputDirectoryEdit')
        self.ax.set_text(edit, str(output))
        # Lite adds a folder and every folder below it, and reads each frame's kind from its cards and folders; the
        # import is done when its summary counts every frame of every role.
        counts = {folder: sum(1 for entry in (frames / folder).iterdir() if entry.is_file())
                  for _, folder in FRAME_ROLES}
        expected = f"{counts['LIGHTS']} light, {counts['FLAT']} flat, {counts['DARK']} dark, {counts['BIAS']} bias"
        self.ax.act(self.element(window, 'Add Folder', identifier='liteAddFolderButton'))
        self.answer_folder_dialog(frames)
        label = self.element(window, 'the frame summary', identifier='liteFrameSummaryLabel')
        self.ax.wait(lambda: self.ax.name(label) == expected, f"the frame summary '{expected}'", 600, 0.5)
        self.note(f'imported: {expected}')
        run = self.element(window, 'Run Integration', identifier='liteRunIntegrationButton')
        self.ax.wait(lambda: self.ax.has_state(run, 'enabled'), 'an enabled Run Integration', 60)
        if not _same_path(self.ax.text(edit), output):
            raise DriverError(f"the output directory reads '{self.ax.text(edit)}', not '{output}'")
        time.sleep(1.0)
        return self.ax.action(run)

    def run(self, record: dict) -> None:
        arguments = self.arguments
        application = pathlib.Path(arguments.application).resolve()
        deployment = application.parent
        frames = pathlib.Path(arguments.frames).resolve()
        output = pathlib.Path(arguments.output)
        storage = pathlib.Path(arguments.storage)
        output.mkdir(parents=True, exist_ok=True)
        storage.mkdir(parents=True, exist_ok=True)
        output, storage = output.resolve(), storage.resolve()
        (storage / 'config').mkdir(exist_ok=True)
        (storage / 'data').mkdir(exist_ok=True)
        record['driver']['application'] = str(application)
        self.session = DisplaySession(self.session_directory)
        self.session.start()
        self.note(f'session up on display {self.session.display}')
        os.environ['DISPLAY'] = self.session.display
        os.environ['DBUS_SESSION_BUS_ADDRESS'] = self.session.bus
        os.environ['AT_SPI_BUS_ADDRESS'] = self.session.accessibility_bus
        self.process = subprocess.Popen([str(application)], cwd=str(deployment), stdin=subprocess.DEVNULL,
                                        env=self.session.application_environment(deployment, storage),
                                        start_new_session=True)
        account = ProcessAccount(self.process.pid)
        sampler = Sampler(self.process.pid, arguments.sampling_interval_ms)
        sampler.start()
        self.sampler = sampler
        self.note(f'started {application}, pid {self.process.pid}')
        self.x = XWindows(self.session.display, self.process.pid)
        self.ax = Accessibility(self.process.pid, self.session.accessibility_bus, self.check_alive)
        self.ax.attach(120)
        main = self.ax.wait(lambda: self.ax.window(identifier='afterNightMainWindow'), 'the main window', 120)
        self.note('main window shown')
        time.sleep(1.5)
        libraries = set(account.shared_objects())
        self.refuse_checkout_libraries(record, libraries, deployment)

        if arguments.workflow == 'full':
            execute = self.set_up_full(main, frames, output, arguments.variant)
            libraries |= account.shared_objects()
            idle, peak_before, private_before = account.memory()
            cpu_before = account.cpu_seconds()
            driver_cpu_before = time.process_time()
            requested = now()
            self.ax.send(execute)
            plan = self.wait_window(('Execution Plan',) + REFUSAL_TITLES, 3600, 0.005)
            planned = now()
            if not plan or plan[1] != 'Execution Plan':
                raise DriverError(f"the request ended in '{plan[1] if plan else 'nothing'}'")
            self.note('execution plan shown')
            plan_window = self.ax.wait(lambda: self.ax.window(name='Execution Plan'), 'the execution plan', 10)
            run = self.ax.action(self.element(plan_window, "the plan's Run button", role='button', name='Run'))
            accepted = now()
            accepted_wall = time.time()
            self.ax.send(run)
        else:
            run = self.set_up_lite(main, frames, output, arguments.variant)
            libraries |= account.shared_objects()
            idle, peak_before, private_before = account.memory()
            cpu_before = account.cpu_seconds()
            driver_cpu_before = time.process_time()
            requested = now()
            planned = None
            accepted = requested
            accepted_wall = time.time()
            self.ax.send(run)
        self.accepted_wall = accepted_wall
        self.note('run requested')

        cancel_requested = None
        deadline = accepted + arguments.timeout_seconds
        ended = None
        while not ended:
            moment = now()
            if moment > deadline:
                raise DriverError(f'the run did not end within {arguments.timeout_seconds} s')
            if arguments.cancel_after_ms > 0 and cancel_requested is None \
                    and milliseconds(accepted, moment) >= arguments.cancel_after_ms:
                if not self.x.find([PROGRESS_TITLE]):
                    raise DriverError('no progress dialog to cancel')
                progress = self.ax.window(name=PROGRESS_TITLE)
                cancel = self.ax.find(progress, identifier='btnCancel') if progress is not None else None
                if cancel is None:
                    raise DriverError('the progress dialog has no Cancel button')
                prepared = self.ax.action(cancel)
                cancel_requested = now()
                self.ax.send(prepared)
                self.note('cancellation requested')
            ended = self.x.find(COMPLETION_TITLES)
            if not ended:
                self.check_alive()
                time.sleep(0.005)
        finished = now()
        working_set_at_end, peak_after, private_after = account.memory()
        cpu_after = account.cpu_seconds()
        driver_cpu = time.process_time() - driver_cpu_before
        self.note(f'run ended: {ended[1]}')
        self.note(f'the driver used {driver_cpu:.2f} s of processor time inside the region')
        libraries |= account.shared_objects()

        sampled_peak, count = sampler.peak_between(requested, finished)
        if peak_after > peak_before and peak_after >= sampled_peak:
            resident_peak = peak_after
            peak_source = "the process's high-water mark, which rose inside the region"
        elif peak_after > peak_before:
            resident_peak = sampled_peak
            peak_source = "the sampler's largest reading, above the high-water mark the kernel had recorded"
        else:
            resident_peak = sampled_peak
            peak_source = "the sampler's largest reading"
        outcome = record['outcome']
        outcome['success'] = ended[1] in ('Complete', 'Completed with Errors') and cancel_requested is None
        outcome['cancelled'] = ended[1] == 'Cancelled'
        outcome['completion'] = ended[1]
        if cancel_requested is not None:
            outcome['cancellationLatencyMs'] = milliseconds(cancel_requested, finished)
        timing = record['timing']
        timing['executionMs'] = milliseconds(accepted, finished)
        if planned is not None:
            timing['preparationMs'] = milliseconds(requested, planned)
            timing['totalMs'] = timing['preparationMs'] + timing['executionMs']
            timing['planWaitMs'] = milliseconds(planned, accepted)
        else:
            timing['totalMs'] = timing['executionMs']
        record['memory'] = {
            'residentPeakBytes': resident_peak, 'residentPeakSource': peak_source, 'sampledPeakBytes': sampled_peak,
            'samplesInRegion': count, 'idleBaselineBytes': idle, 'workingSetAtEndBytes': working_set_at_end,
            'lifetimePeakBeforeRegionBytes': peak_before, 'lifetimePeakBytes': peak_after,
            'privateBytesBeforeRegion': private_before, 'privateBytesAtEnd': private_after,
            'metric': "the process's resident set (VmRSS of /proc/<pid>/status), sampled from outside the process, "
                      'and its high-water mark (VmHWM); the private bytes are its resident anonymous memory and its '
                      'swapped-out pages (RssAnon + VmSwap), not the commit charge Windows reports'}
        record['process']['cpuSecondsInRegion'] = cpu_after - cpu_before
        record['process']['cpuSeconds'] = cpu_after
        self.refuse_checkout_libraries(record, libraries, deployment)

    def refuse_checkout_libraries(self, record: dict, libraries, deployment: pathlib.Path) -> None:
        """Records the mapped shared objects outside the deployment and the system, and refuses the run when one of
        them is the checkout's: the binaries name the build host's Qt in their RUNPATH, which the loader reaches for
        anything the deployment does not carry."""
        sorted_libraries = classify_shared_objects(libraries, deployment, REPOSITORY)
        record['process']['sharedObjects'] = {'deployment': len(sorted_libraries['deployment']),
                                              'system': sorted_libraries['system'],
                                              'outside': sorted_libraries['outside']}
        if sorted_libraries['checkout']:
            raise DriverError('the application mapped libraries of the checkout: '
                              + ', '.join(sorted_libraries['checkout']))

    def stop(self) -> None:
        """Stops the application - it is stopped, not closed: the integrated images it opens once the run has ended are
        not the measurement's, and a quit would ask about them - then the session."""
        if getattr(self, 'sampler', None) is not None:
            self.sampler.stop()
        if self.process is not None:
            descendants = descendants_of(self.process.pid)
            if self.process.poll() is None:
                try:
                    os.killpg(self.process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            for pid in descendants:
                try:
                    os.kill(pid, signal.SIGKILL)
                except (ProcessLookupError, PermissionError):
                    pass
            try:
                self.process.wait(30)
            except subprocess.TimeoutExpired:
                pass
        if self.x is not None:
            self.x.close()
        if getattr(self, 'session', None) is not None:
            self.session.stop()


# ---- What the run left behind: its log, the images it published, and the stages and engine accounting its log records.

_ENGINE_LINE = re.compile(r'Engine memory: peak (\d+) bytes \((\d+) when the run started\), '
                          r'(\d+) bytes still allocated, (\d+) allocations of (\d+) bytes, (\d+) refused')
_STARTED_LINE = re.compile(r'^\[([^\]]+)\] \[HEADER\] WORKFLOW STARTED')
_COMPLETED_LINE = re.compile(r'^\[([^\]]+)\] \[SUCCESS\] \S+ (.+?) Complete \S+ \(')
_STAGE_LINE = re.compile(r'STAGE: (.+?) \(\d+ group')
OUTPUT_ROLES = {'master_bias_': 'bias-master', 'master_dark_': 'dark-master', 'master_flat_': 'flat-master',
                'stacked_': 'light-master'}


def parse_log_time(text: str) -> datetime.datetime:
    """A timestamp of the run's log, which states a date and a time of day, or a time of day alone."""
    text = text.strip()
    for layout in ('%Y-%m-%d %H:%M:%S.%f', '%Y-%m-%d %H:%M:%S', '%Y-%m-%dT%H:%M:%S.%f', '%Y-%m-%dT%H:%M:%S',
                   '%H:%M:%S.%f', '%H:%M:%S'):
        try:
            return datetime.datetime.strptime(text, layout)
        except ValueError:
            continue
    raise ValueError(f'unknown log time {text!r}')


def read_workflow_log(lines: list[str]) -> tuple[list[dict], dict | None]:
    """Each stage's wall time, from the timestamps the run's log gives its start and each stage's completion, and the
    engine accounting the workflow executor writes."""
    completed = {}
    started = previous = None
    for line in lines:
        match = _STARTED_LINE.search(line)
        if match:
            started = previous = parse_log_time(match.group(1))
            continue
        match = _COMPLETED_LINE.search(line) if started is not None else None
        if match:
            at = parse_log_time(match.group(1))
            completed[match.group(2).replace(' ', '')] = {'ms': (at - previous).total_seconds() * 1000.0,
                                                         'endMs': (at - started).total_seconds() * 1000.0}
            previous = at
    stages = []
    engine = None
    for line in lines:
        match = _STAGE_LINE.search(line)
        if match:
            name = match.group(1).replace(' ', '')
            stage = {'name': name}
            if name in completed:
                stage.update(completed[name])
            stages.append(stage)
        match = _ENGINE_LINE.search(line)
        if match:
            values = [int(value) for value in match.groups()]
            engine = {'region': {'peakBytes': values[0], 'peakBeforeRegionBytes': values[1],
                                 'liveBytesAfterRegion': values[2], 'allocationsInRegion': values[3],
                                 'bytesAllocatedInRegion': values[4], 'deniedAllocationsInRegion': values[5]}}
    return stages, engine


def file_sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with open(path, 'rb') as handle:
        for block in iter(lambda: handle.read(8 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def collect_run_outputs(record: dict, output: pathlib.Path, storage: pathlib.Path, accepted_wall: float | None) -> None:
    """Fills the record from what the run left: only a log the run wrote after its request is its own, since an output
    directory keeps every earlier run's, and only an image it wrote then is its."""
    if accepted_wall is not None:
        logs = sorted((path for path in (output / 'history').rglob('workflow_log.txt')
                       if path.stat().st_mtime >= accepted_wall - 2), key=lambda path: path.stat().st_mtime)
        if logs:
            stages, engine = read_workflow_log(logs[-1].read_text(encoding='utf-8', errors='replace').splitlines())
            record['timing']['stages'] = stages
            if engine is not None:
                record['engine'] = engine
            record['workflowLog'] = logs[-1].relative_to(output).as_posix()
    application_log = storage / 'logs' / 'AfterNight.log'
    if application_log.is_file():
        text = application_log.read_text(encoding='utf-8', errors='replace')
        record['preparation']['executionMode'] = 'incremental' if 'starting incremental execution' in text else 'full'
    if accepted_wall is not None:
        published = []
        for path in sorted(output.iterdir(), key=lambda entry: entry.name):
            if not path.is_file() or path.suffix not in ('.fit', '.fits', '.fts'):
                continue
            if path.stat().st_mtime < accepted_wall - 2:
                continue
            role = next((name for prefix, name in OUTPUT_ROLES.items() if path.name.startswith(prefix)), None)
            if role is None:
                continue
            published.append({'role': role, 'path': path.name, 'bytes': path.stat().st_size,
                              'sha256': file_sha256(path)})
        record['outputs'] = published


def parse_arguments(argv: list[str] | None = None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n', 1)[0])
    parser.add_argument('--application', required=True, help="the deployment's AfterNight")
    parser.add_argument('--workflow', required=True, choices=('full', 'lite'))
    parser.add_argument('--frames', required=True, help='a directory holding BIAS, DARK, FLAT and LIGHTS')
    parser.add_argument('--output', required=True, help="the workflow's output directory")
    parser.add_argument('--storage', required=True, help="a new directory for the application's storage root")
    parser.add_argument('--result', required=True, help='the result file to write')
    parser.add_argument('--variant', type=int, choices=(0, 1), default=0)
    parser.add_argument('--cancel-after-ms', type=int, default=0)
    parser.add_argument('--sampling-interval-ms', type=int, default=10)
    parser.add_argument('--dataset-identity', default='')
    parser.add_argument('--timeout-seconds', type=int, default=1800)
    return parser.parse_args(argv)


def new_record(arguments) -> dict:
    return {
        'schema': 1,
        'driver': {'kind': 'application', 'platform': 'linux', 'application': arguments.application,
                   'workflow': arguments.workflow, 'variant': arguments.variant,
                   'samplingIntervalMs': arguments.sampling_interval_ms, 'cancelAfterMs': arguments.cancel_after_ms},
        'dataset': {'identityDigest': arguments.dataset_identity},
        'outcome': {'success': False, 'cancelled': False, 'cancellationLatencyMs': None, 'completion': None,
                    'error': None},
        'timing': {'totalMs': None, 'preparationMs': None, 'executionMs': None, 'stages': []},
        'memory': {},
        'process': {},
        'preparation': {'executionMode': None},
        'engine': None,
        'outputs': [],
        'events': [],
    }


class DriverTerminated(Exception):
    """The driver was asked to stop: the run is refused, its session torn down and its result still written."""


def _terminate(signum, _frame):
    raise DriverTerminated(f'the driver was stopped by signal {signum} before the run ended')


def main(argv: list[str] | None = None) -> int:
    arguments = parse_arguments(argv)
    signal.signal(signal.SIGTERM, _terminate)
    record = new_record(arguments)
    driver = ApplicationDriver(arguments)
    session_directory = pathlib.Path(tempfile.mkdtemp(prefix='an-application-driver-'))
    driver.session_directory = session_directory
    driver.accepted_wall = None
    try:
        driver.run(record)
    except (DriverError, OSError, RuntimeError, ValueError) as error:
        record['outcome']['error'] = str(error)
        record['outcome']['success'] = False
        driver.note(f'refused: {error}')
    except Exception as error:  # the run cannot go on; the record says why, and the session is still torn down
        record['outcome']['error'] = f'{type(error).__name__}: {error}'
        record['outcome']['success'] = False
        driver.note(f'refused: {record["outcome"]["error"]}')
    finally:
        driver.stop()
    output = pathlib.Path(arguments.output)
    storage = pathlib.Path(arguments.storage)
    if output.is_dir():
        collect_run_outputs(record, output.resolve(), storage.resolve(), driver.accepted_wall)
    if record['outcome']['error']:
        driver.note(f'the session\'s logs are kept in {session_directory}')
    else:
        shutil.rmtree(session_directory, ignore_errors=True)
    record['events'] = list(driver.events)
    pathlib.Path(arguments.result).write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    return 2 if record['outcome']['error'] else 0


if __name__ == '__main__':
    sys.exit(main())
