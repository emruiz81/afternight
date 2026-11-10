# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Prove that the packagers ship what the active package profile says, and no test, tool or optional component.

Both packagers copy a whole deployed `bin` directory and subtract a list of exclusion patterns.
That shape is convenient and quietly dangerous: a binary added to the build tree ships by default,
and nobody has to decide to ship it. The engine's optional Python adapter, the opt-in GPU backend
and the benchmark runners are development artifacts - an optional seam onto a runtime the product
does not acquire this way, a proof that is never shipped, and measurement tools - so shipping any of
them would mean distributing something no user can use and no support process expects.

What must ship and what must not is not listed here. It is derived from the target registry,
`build/scripts/tools/consumer_targets.txt`, and the profile `build/scripts/tools/package_profiles.txt` marks as the
one the packagers produce: every target that profile ships must reach the payload in its Release spelling, and every
other declared target - a suite, a runner, and each optional adapter or opt-in backend by its own name - must stay
out of it in both configurations' spellings, because a rule written against the Release spelling
alone lets the Debug binary through, and a developer machine packaging a Debug tree is exactly when that happens. The
profile adds the payload that is no target's output - a Qt runtime, the product's RAW decoder, which must ship - and
representative debris that must not. A target declared tomorrow is therefore held by the next run without an edit
here, and moving the packagers to another profile moves both lists. An adapter is held by its own name rather than
left to a wildcard: a pattern that excluded an engine by its prefix would happen to exclude the adapter too, and a
property that holds by accident is one a narrower pattern silently loses.

The fuzz targets are read from the campaign's registry rather than listed here. The target-select build
scripts write every registered target into the same deployed directory the packagers sweep, so a target
registered tomorrow ships by default unless something excludes it; a list kept in this file would be the
one place that did not learn about it. A missing registry is reported rather than read as no targets.

Three more things reach that directory from outside either solution, and each is read from the file that
puts it there, for the same reason. The target-select build scripts record the instrumentation the binaries
were built with in a file beside them; the RAW qualification project installs its engine drivers and
reference oracles there; and the RAW deployment copies the private LibRaw and RawSpeed packages, which
nothing but the engine imports. The product ships their Release builds and keeps their Debug builds out. The
product's RAW decoder is held on the list that must ship, because Inno Setup matches without case and a `raw*`
exclusion written for the tools would take it too.

The product suites run from the deployed directory and can leave logs and a debugger or crash tool can
leave `core.*` or `vgcore.*` there. Those are not generated targets, so the registry cannot enumerate them;
the profile's representative names hold the Linux packager to generic exclusions instead.

A pattern written for a file at the payload's root reaches below it unless it is anchored: rsync and Inno Setup
both match a pattern with no leading slash or backslash against a file or directory name at every depth, and an
excluded directory takes everything below it. So every module of the extension SDK package `python/modules/afternight`
is held to reach each payload at the depth its deployment puts it, which is what an unanchored `core.*` written for a
core dump took from the AppImage. A pattern with a slash inside it is a path, as both packagers read one: anchored,
it names one place below the root; unanchored, it matches the tail of a path at any depth; and a wildcard never
crosses a separator. The profile's lists name a file at the root, or, with a slash, a path judged where it lies, so
the Qt Test deployed in `lib/` is kept out of the AppImage without taking the copy the bundled Python's PySide6
carries under `PythonLib`, which must ship with it.

Both packagers also name the product's version - the Windows installer's one statement of it, from which its name,
its registration and its file version are taken, the AppImage's name - and each is held to the version the application
states, `kProductVersion` in
`src/AfterNightApp/app/ApplicationSession.h`, because a package that names another version than the window, the log
and the crash reports it installs is a release nobody can match to its reports.

The binary the Linux packager copies by a rule of its own - the application, which the profile leaves out of the sweep
- is held to land in the directory the sweep fills, and AppRun to start it there: the application looks beside itself
for the bundled Python, both extension hosts, the bundled extensions and the crash handler, so an application copied
into `usr/bin` beside a sweep into `usr/lib` runs, and can provision no extension environment, start no host and report
no crash.

The check does not grep for a pattern. It reconstructs the payload rule from each packager and
runs candidate file names through it, so an exclusion that is present but does not actually match
the file it was written for still fails. It also runs the *product's* own binaries through the
same rule, because an over-broad exclusion - `AfterNight*` rather than `AfterNightEnginePythonAdapter*` -
would satisfy every "is it excluded" test while silently emptying the installer.

Usage:
    python build/scripts/tools/check_installer_payload.py [--repo-root <dir>]

Exits 0 when both packagers behave, 1 otherwise, printing one line per problem.

Tests: build/scripts/tools/check_installer_payload_test.py
"""

from __future__ import annotations

import argparse
import ast
import fnmatch
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))

WINDOWS_PACKAGER = os.path.join('installer', 'win_installer.iss')
LINUX_PACKAGER = os.path.join('installer', 'linux_packager.sh')
# The desktop entry the Linux packager copies into the AppImage, which appimagetool refuses with a carriage return.
DESKTOP_ENTRY = os.path.join('installer', 'AfterNight.desktop')
FUZZ_REGISTRY = os.path.join('build', 'scripts', 'tools', 'fuzz_targets.txt')
TARGET_SELECT_SCRIPTS = (
    os.path.join('build', 'scripts', 'msvc', 'build_afternightengine.bat'),
    os.path.join('build', 'scripts', 'linux', 'build_afternightengine.sh'),
)
RAW_QUALIFICATION_PROJECT = os.path.join('build', 'scripts', 'tools', 'rawspeed', 'CMakeLists.txt')
RAW_DEPLOYMENT = os.path.join('build', 'scripts', 'tools', 'deploy_raw_deps.py')

# The registries the lists are derived from, read with the tool that holds the tree to them.
REGISTRY = os.path.join('build', 'scripts', 'tools', 'consumer_targets.txt')
SOURCE_LISTS = os.path.join('build', 'scripts', 'tools', 'isolated_sources.txt')
PROFILES = os.path.join('build', 'scripts', 'tools', 'package_profiles.txt')
REGISTRIES = (REGISTRY, SOURCE_LISTS, PROFILES)

# Where the application states the product's version, which both packagers name.
PRODUCT_VERSION_SOURCE = os.path.join('src', 'AfterNightApp', 'app', 'ApplicationSession.h')
PRODUCT_VERSION = re.compile(r'kProductVersion\[\]\s*=\s*"([^"]+)"')
# What each packager states the version as, and the spellings of the product's version each statement may take: the
# installer's one statement of it, which names the package and registers it, and its file version, which Inno Setup
# fills with zeros when it is given three parts. The installer's file version may name that one statement rather than
# repeat it, and is read as what the statement holds.
WINDOWS_VERSION_DEFINE = re.compile(r'^#define\s+MyAppVersion\s+"([^"]*)"', re.MULTILINE)
WINDOWS_VERSION_REFERENCE = '{#MyAppVersion}'
WINDOWS_VERSIONS = ((WINDOWS_VERSION_DEFINE, ('{}',)),
                    (re.compile(r'^VersionInfoVersion=(\S*)', re.MULTILINE), ('{}.0', '{}')))
LINUX_VERSIONS = ((re.compile(r'^APP_VERSION="([^"]*)"', re.MULTILINE), ('{}',)),)

# The extension SDK package, and where each platform's deployment puts it below the payload root.
SDK_PACKAGE = os.path.join('python', 'modules', 'afternight')
SDK_DEPLOYED = {'windows': 'PythonLib/Lib/site-packages/afternight',
                'linux': 'PythonLib/lib/python3.14/site-packages/afternight'}


def profile_lists(repo_root):
    """{platform: (must_ship, must_not_ship)} for the profile the packagers produce, its name, the outputs the Linux
    packager copies by a rule of its own, and what went wrong."""
    import pathlib
    import consumer_targets  # imported here: the payload rules below need nothing of it
    for relative in REGISTRIES:
        if not os.path.isfile(os.path.join(repo_root, relative)):
            return None, None, (), ['%s does not exist, so no list the packagers are held to can be derived'
                                    % relative.replace(os.sep, '/')]
    registry = consumer_targets.read_registry(pathlib.Path(repo_root))
    problems = list(registry.problems)
    profile = registry.packager_profile()
    if profile is None:
        return None, None, (), problems + ['no single package profile is marked as what the packagers produce']
    lists = {}
    for platform in consumer_targets.PLATFORMS:
        must_ship = list(profile.ship[platform])
        must_not_ship = list(profile.keep_out[platform])
        for name, entry in sorted(registry.targets.items()):
            names = consumer_targets.binary_names(entry, platform)
            if not names:
                continue
            if profile.name in entry.ship:
                if not (platform == 'linux' and entry.output in profile.separate_linux):
                    must_ship.append(names[0])
            else:
                must_not_ship.extend(names)
                if platform == 'windows':
                    must_not_ship.extend(os.path.splitext(spelled)[0] + '.pdb' for spelled in names)
                else:
                    must_not_ship.append(names[0] + '.debug')
        lists[platform] = (tuple(must_ship), tuple(must_not_ship))
    return lists, profile.name, tuple(profile.separate_linux), problems


# The Inno Setup rule that sweeps the deployed Windows binaries.
_ISS_SOURCE = re.compile(r'^\s*Source:\s*"(?P<source>[^"]+)"', re.IGNORECASE)
_ISS_EXCLUDES = re.compile(r'Excludes:\s*"(?P<excludes>[^"]*)"', re.IGNORECASE)
_WINDOWS_PAYLOAD_SOURCE = re.compile(r'bin[\\/]msvc[\\/]\w+[\\/]\*$', re.IGNORECASE)

# The rsync invocation that sweeps the deployed Linux binaries into the AppDir.
_SHELL_ASSIGNMENT = re.compile(r'^\s*(?P<name>[A-Za-z_][A-Za-z0-9_]*)="(?P<value>[^"]*)"')
_RSYNC_EXCLUDE = re.compile(r'--exclude=(?:"(?P<quoted>[^"]*)"|(?P<bare>\S+))')
# The Linux packager's copy of one deployed binary by a rule of its own, and the binary its AppRun starts, written in
# the unquoted here-document that escapes AppRun's own variables.
_SHELL_COPY = re.compile(r'^\s*cp\s+(?P<source>"[^"]+"|\S+)\s+(?P<destination>"[^"]+"|\S+)\s*$')
_APPRUN_EXEC = re.compile(r'^\s*exec\s+"\\?\$HERE/(?P<path>[^"]+)"')
# A variable whose whole value is one quoted word; a command substitution is left unexpanded.
_WHOLE_SHELL_ASSIGNMENT = re.compile(r'^\s*(?P<name>[A-Za-z_][A-Za-z0-9_]*)="(?P<value>[^"$]*(?:\$\{?[A-Za-z_]'
                                     r'[A-Za-z0-9_]*\}?[^"$]*)*)"\s*$')

# Where each script names the file it records the instrumentation in, and what the qualification project installs.
_RECORD_ASSIGNMENT = re.compile(r'SANITIZER_MARKER="?(?P<path>[^"\r\n]+)')
_RUNTIME_INSTALL = re.compile(r'install\s*\(\s*TARGETS\s+(?P<targets>[^)]*?)\s+RUNTIME\s+DESTINATION\s+\.\s*\)',
                              re.IGNORECASE)


class PayloadRule:
    """One packager rule: everything under a source directory, minus a list of patterns."""

    def __init__(self, description, excludes, case_sensitive):
        self.description = description
        self.excludes = excludes
        self.case_sensitive = case_sensitive

    @staticmethod
    def _parts(text):
        return [part for part in re.split(r'[\\/]', text) if part]

    def _matches(self, parts, pattern):
        """Whether the path `parts`, from the payload's root, is what one pattern excludes.

        A leading slash (rsync) or backslash (Inno Setup) anchors a pattern to the payload's root; unanchored, it matches
        the tail of the path. A pattern with a slash inside it is a path and matches component by component, so a
        wildcard never crosses a separator; both packagers read a pattern that way."""
        anchored = pattern[:1] in ('/', '\\')
        target = self._parts(pattern[1:] if anchored else pattern)
        if not target or len(target) > len(parts) or (anchored and len(target) != len(parts)):
            return False
        tail = parts[-len(target):]
        if not self.case_sensitive:
            tail, target = [part.lower() for part in tail], [part.lower() for part in target]
        return all(fnmatch.fnmatchcase(part, wanted) for part, wanted in zip(tail, target))

    def is_excluded(self, name):
        """Whether a file at the payload's root is excluded."""
        return self.excludes_path(name)

    def excludes_path(self, relative):
        """Whether a file below the payload's root is excluded: a pattern matches the file or any directory above it,
        and an excluded directory takes everything below it."""
        parts = self._parts(relative)
        return any(self._matches(parts[:depth + 1], pattern)
                   for depth in range(len(parts)) for pattern in self.excludes)


def parse_windows_packager(text):
    """Return the payload rule of the Inno Setup script, or None when it has no such rule."""
    for line in text.splitlines():
        source = _ISS_SOURCE.match(line)
        if not source:
            continue
        if not _WINDOWS_PAYLOAD_SOURCE.search(source.group('source')):
            continue
        excludes = _ISS_EXCLUDES.search(line)
        patterns = []
        if excludes:
            patterns = [entry.strip() for entry in excludes.group('excludes').split(',') if entry.strip()]
        # Inno Setup matches exclusion patterns case-insensitively, as Windows itself does.
        return PayloadRule(source.group('source'), patterns, case_sensitive=False)
    return None


def expand_shell_variables(text, variables):
    """Substitute `$NAME` and `${NAME}` from `variables`; leave unknown names untouched."""

    def replace(match):
        name = match.group('braced') or match.group('bare')
        return variables.get(name, match.group(0))

    return re.sub(r'\$(?:\{(?P<braced>[A-Za-z_][A-Za-z0-9_]*)\}|(?P<bare>[A-Za-z_][A-Za-z0-9_]*))',
                  replace, text)


def parse_linux_packager(text):
    """Return the payload rule of the AppImage packager, or None when it has no such rule."""
    variables = {}
    for line in text.splitlines():
        assignment = _SHELL_ASSIGNMENT.match(line)
        if assignment:
            variables[assignment.group('name')] = expand_shell_variables(assignment.group('value'),
                                                                        variables)
        if not line.strip().startswith('rsync'):
            continue
        patterns = []
        for match in _RSYNC_EXCLUDE.finditer(line):
            pattern = match.group('quoted') if match.group('quoted') is not None else match.group('bare')
            patterns.append(expand_shell_variables(pattern, variables))
        # rsync patterns are matched case-sensitively, as the filesystem is.
        return PayloadRule(line.strip(), patterns, case_sensitive=True)
    return None


def _shell_word(word, variables):
    """A shell word with its quotes removed and its variables expanded, without a trailing slash."""
    if len(word) >= 2 and word[0] == word[-1] == '"':
        word = word[1:-1]
    return expand_shell_variables(word, variables).rstrip('/')


def linux_application_problems(text, separate):
    """The binaries the AppImage packager copies by a rule of its own, held to the directory its sweep fills.

    A binary the profile excludes from the sweep and the packager copies itself is the application, which looks beside
    itself for the bundled Python, both extension hosts, the bundled extensions and the crash handler - everything the
    sweep carries. So each one must be copied into the sweep's destination, and the binary AppRun starts must be started
    from there.
    """
    label = os.path.basename(LINUX_PACKAGER)
    variables = {}
    sweep = None
    copies = {}
    started = None
    for line in text.splitlines():
        assignment = _WHOLE_SHELL_ASSIGNMENT.match(line)
        if assignment:
            variables[assignment.group('name')] = expand_shell_variables(assignment.group('value'), variables)
            continue
        stripped = line.strip()
        if stripped.startswith('rsync') and sweep is None:
            sweep = _shell_word(stripped.split()[-1], variables)
            continue
        copy = _SHELL_COPY.match(line)
        if copy:
            source = _shell_word(copy.group('source'), variables)
            name = source.rsplit('/', 1)[-1]
            if name in separate:
                destination = _shell_word(copy.group('destination'), variables)
                if destination.rsplit('/', 1)[-1] == name:
                    destination = destination.rsplit('/', 1)[0]
                copies.setdefault(name, destination)
            continue
        launch = _APPRUN_EXEC.match(line)
        if launch and started is None:
            started = expand_shell_variables(launch.group('path'), variables)
    if sweep is None:
        return []
    problems = []
    for name in separate:
        destination = copies.get(name)
        if destination is None:
            problems.append('%s: copies %s by no rule of its own, though the profile leaves it out of the sweep into %s'
                            % (label, name, sweep))
        elif destination != sweep:
            problems.append('%s: copies %s into %s, apart from %s, where the sweep puts the bundled Python, the '
                            'extension hosts, the extensions and the crash handler it looks for beside itself'
                            % (label, name, destination, sweep))
    if started is None:
        problems.append('%s: writes no AppRun that starts a binary' % label)
    else:
        name = started.rsplit('/', 1)[-1]
        directory = variables.get('APPDIR', '$APPDIR') + ('/' + started.rsplit('/', 1)[0] if '/' in started else '')
        if directory != sweep:
            problems.append('%s: AppRun starts %s from %s, not from %s beside the payload' % (label, name, directory,
                                                                                            sweep))
    return problems


# The launcher the Linux packager writes for the command-line tool: a here-document into usr/bin, the library path it
# exports and the binary it starts, written as AppRun's are, with their own variables escaped.
CLI_LAUNCHER = 'usr/bin/AfterNightCLI'
_CLI_LAUNCHER_START = re.compile(r'^\s*cat\s+>\s*"\$APPDIR/' + re.escape(CLI_LAUNCHER) + r'"\s*<<\s*(?P<end>\w+)\s*$')
_LAUNCHER_LIBRARY_PATH = re.compile(r'^\s*export\s+LD_LIBRARY_PATH="(?P<value>[^"]*)"')
_LAUNCHER_EXEC = re.compile(r'^\s*exec\s+"\\?\$(?P<base>[A-Za-z_]+)/(?P<path>[^"]+)"')


def linux_cli_launcher_problems(text):
    """The AppImage's command-line tool has no AppRun of its own, so the packager must write it a launcher in usr/bin
    that sets the package's library path - usr/lib and usr/lib/lib, as AppRun does for the application - and starts
    usr/lib/AfterNightCLI. A run path reaches only a binary's own imports: without the path the third-party libraries
    below usr/lib/lib would take the host's copies of what they need, or the tool would not start at all."""
    label = os.path.basename(LINUX_PACKAGER)
    lines = text.splitlines()
    if not any(line.strip().startswith('rsync') for line in lines):
        return []
    body, end = None, None
    for line in lines:
        if body is None:
            start = _CLI_LAUNCHER_START.match(line)
            if start:
                body, end = [], start.group('end')
        elif line.strip() == end:
            break
        else:
            body.append(line)
    if body is None:
        return ['%s: writes no launcher %s for the command-line tool, which has no AppRun of its own'
                % (label, CLI_LAUNCHER)]
    problems = []
    paths = [match.group('value') for match in map(_LAUNCHER_LIBRARY_PATH.match, body) if match]
    entries = [entry.replace('\\', '') for entry in (paths[0].split(':') if paths else [])]
    for directory in ('usr/lib', 'usr/lib/lib'):
        if not any(entry.endswith('/' + directory) for entry in entries):
            problems.append('%s: the launcher %s does not put %s on the library path, as AppRun does'
                            % (label, CLI_LAUNCHER, directory))
    started = [match.group('path') for match in map(_LAUNCHER_EXEC.match, body) if match]
    if started != ['usr/lib/AfterNightCLI']:
        problems.append('%s: the launcher %s starts %s, not usr/lib/AfterNightCLI'
                        % (label, CLI_LAUNCHER, ', '.join(started) or 'nothing'))
    return problems


def check_rule(label, rule, must_not_ship, must_ship, must_ship_paths=()):
    """Hold one packager rule to the lists. An entry is a file at the payload's root or, with a slash in it, a path below
    the root, judged where it lies."""
    problems = []
    for entry in must_not_ship:
        if not rule.excludes_path(entry):
            problems.append('%s: %s would be packaged' % (label, entry))
    for entry in tuple(must_ship) + tuple(must_ship_paths):
        if rule.excludes_path(entry):
            problems.append('%s: %s would be excluded from the package' % (label, entry))
    return problems


def sdk_payload_paths(repo_root):
    """{platform: [path below the payload root]} for every module of the SDK package, and what went wrong."""
    directory = os.path.join(repo_root, SDK_PACKAGE)
    if not os.path.isdir(directory):
        return ({platform: [] for platform in SDK_DEPLOYED},
                ['%s does not exist, so no SDK module can be proved to reach a payload' % SDK_PACKAGE.replace(os.sep, '/')])
    modules = sorted(name for name in os.listdir(directory) if name.endswith('.py'))
    return {platform: [root + '/' + name for name in modules] for platform, root in SDK_DEPLOYED.items()}, []


def registered_fuzz_targets(repo_root):
    """The project names the fuzz campaign registers, or None when the registry is not there."""
    path = os.path.join(repo_root, FUZZ_REGISTRY)
    if not os.path.isfile(path):
        return None
    names = []
    with open(path, encoding='utf-8', errors='replace') as handle:
        for line in handle:
            text = line.strip()
            if text and not text.startswith('#'):
                names.append(text.split()[0])
    return names


def instrumentation_records(repo_root):
    """The file names the target-select build scripts record instrumentation in, and what could not be read."""
    names = set()
    problems = []
    for relative in TARGET_SELECT_SCRIPTS:
        path = os.path.join(repo_root, relative)
        shown = relative.replace(os.sep, '/')
        if not os.path.isfile(path):
            problems.append('%s was not found, so the instrumentation record it writes cannot be proved absent '
                            'from a payload' % shown)
            continue
        with open(path, encoding='utf-8', errors='replace') as handle:
            assignment = _RECORD_ASSIGNMENT.search(handle.read())
        if assignment is None:
            problems.append('%s names no instrumentation record, so none can be proved absent from a payload'
                            % shown)
            continue
        names.add(re.split(r'[\\/]', assignment.group('path').strip())[-1])
    return sorted(names), problems


def qualification_tools(repo_root):
    """The executables the RAW qualification project installs beside the binaries, and what could not be read."""
    path = os.path.join(repo_root, RAW_QUALIFICATION_PROJECT)
    shown = RAW_QUALIFICATION_PROJECT.replace(os.sep, '/')
    if not os.path.isfile(path):
        return [], ['%s was not found, so no RAW qualification tool can be proved absent from a payload' % shown]
    with open(path, encoding='utf-8', errors='replace') as handle:
        text = handle.read()
    names = []
    for rule in _RUNTIME_INSTALL.finditer(text):
        names.extend(rule.group('targets').split())
    if not names:
        return [], ['%s installs nothing this check can read, so no RAW qualification tool can be proved absent '
                    'from a payload' % shown]
    return names, []


def private_raw_libraries(repo_root):
    """The private RAW packages the deployment copies beside the binaries, each as (Release name, Debug name), and what
    could not be read."""
    path = os.path.join(repo_root, RAW_DEPLOYMENT)
    shown = RAW_DEPLOYMENT.replace(os.sep, '/')
    if not os.path.isfile(path):
        return [], ['%s was not found, so no private RAW library can be proved absent from a payload' % shown]
    with open(path, encoding='utf-8', errors='replace') as handle:
        module = ast.parse(handle.read(), filename=path)
    for statement in module.body:
        if isinstance(statement, ast.Assign) and any(
                isinstance(target, ast.Name) and target.id == 'PRIVATE_LIBRARIES' for target in statement.targets):
            try:
                # Each name leaves its debug suffix to fill, as the deployment spells it for both configurations.
                return [(pattern.format(suffix=''), pattern.format(suffix='d'))
                        for pattern in ast.literal_eval(statement.value)], []
            except (AttributeError, IndexError, KeyError, ValueError):
                break
    return [], ['%s declares no PRIVATE_LIBRARIES this check can spell, so no private RAW library can be proved '
                'absent from a payload' % shown]


def packaged_version_problems(repo_root):
    """Each packager's statement of the version, held to the version the application states."""
    source = os.path.join(repo_root, PRODUCT_VERSION_SOURCE)
    if not os.path.isfile(source):
        return ['%s: not found, so no packaged version can be held to the product\'s'
                % PRODUCT_VERSION_SOURCE.replace(os.sep, '/')]
    with open(source, encoding='utf-8', errors='replace') as handle:
        stated = PRODUCT_VERSION.search(handle.read())
    if stated is None:
        return ['%s: states no kProductVersion' % PRODUCT_VERSION_SOURCE.replace(os.sep, '/')]
    version = stated.group(1)
    problems = []
    for relative, statements in ((WINDOWS_PACKAGER, WINDOWS_VERSIONS), (LINUX_PACKAGER, LINUX_VERSIONS)):
        path = os.path.join(repo_root, relative)
        label = os.path.basename(relative)
        if not os.path.isfile(path):
            continue
        with open(path, encoding='utf-8', errors='replace') as handle:
            text = handle.read()
        defined = WINDOWS_VERSION_DEFINE.search(text)
        for pattern, spellings in statements:
            found = pattern.search(text)
            expected = [spelling.format(version) for spelling in spellings]
            if found is None:
                problems.append('%s: states no version where %s would' % (label, pattern.pattern))
                continue
            stated = found.group(1)
            if defined is not None:
                stated = stated.replace(WINDOWS_VERSION_REFERENCE, defined.group(1))
            if stated not in expected:
                problems.append('%s: states version %s where the application states %s (%s)'
                                % (label, stated, version, ' or '.join(expected)))
    return problems


def check_packager(repo_root, relative, parse, must_not_ship, must_ship, must_ship_paths=()):
    path = os.path.join(repo_root, relative)
    label = os.path.basename(relative)
    if not os.path.isfile(path):
        return ['%s: not found at %s' % (label, relative)], None
    with open(path, encoding='utf-8', errors='replace') as handle:
        rule = parse(handle.read())
    if rule is None:
        return ['%s: no rule found that packages the deployed binaries' % label], None
    return check_rule(label, rule, must_not_ship, must_ship, must_ship_paths), label


def desktop_entry_problems(repo_root):
    """The desktop entry the Linux packager copies must hold no carriage return, or appimagetool refuses the package.

    A checkout writes the file as `.gitattributes` says, so a working copy that happens to hold line feeds says nothing
    of the next fresh checkout: where the root is a git checkout, the attribute it is written by is held as well."""
    path = os.path.join(repo_root, DESKTOP_ENTRY)
    if not os.path.isfile(path):
        return []
    problems = []
    with open(path, 'rb') as handle:
        if b'\r' in handle.read():
            problems.append('%s: holds a carriage return, which appimagetool refuses' % DESKTOP_ENTRY)
    attribute = subprocess.run(['git', '-C', repo_root, 'check-attr', 'eol', '--', DESKTOP_ENTRY.replace(os.sep, '/')],
                               capture_output=True, text=True)
    if attribute.returncode == 0 and not attribute.stdout.strip().endswith(': eol: lf'):
        problems.append('%s: a checkout writes it with %s, and appimagetool refuses a carriage return; state it '
                        'eol=lf in .gitattributes' % (DESKTOP_ENTRY, attribute.stdout.strip().rsplit(': ', 1)[-1]))
    return problems


def main(argv=None, stream=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--repo-root', default=REPO_ROOT,
                        help='repository root to inspect (default: the checkout this script lives in)')
    options = parser.parse_args(argv)
    out = stream if stream is not None else sys.stdout

    problems = []
    inspected = []
    lists, profile, separate, unread = profile_lists(options.repo_root)
    problems.extend(unread)
    if lists is None:
        lists = {'windows': ((), ()), 'linux': ((), ())}
    fuzz_targets = registered_fuzz_targets(options.repo_root)
    if fuzz_targets is None:
        problems.append('%s was not found, so no fuzz target can be proved absent from a payload'
                        % FUZZ_REGISTRY.replace(os.sep, '/'))
        fuzz_targets = []
    records, unread = instrumentation_records(options.repo_root)
    problems.extend(unread)
    tools, unread = qualification_tools(options.repo_root)
    problems.extend(unread)
    libraries, unread = private_raw_libraries(options.repo_root)
    problems.extend(unread)
    # The private RAW packages are what the engine imports: the product ships their Release builds, and nothing ships
    # their Debug builds.
    raw_ship = tuple(release for release, _ in libraries)
    raw_keep_out = tuple(debug for _, debug in libraries)
    sdk_paths, unread = sdk_payload_paths(options.repo_root)
    problems.extend(unread)
    # Both configurations of each target, for the reason both configurations of every library are listed. The
    # qualification project gives its executables no debug suffix, and the private libraries are linked into the
    # engine on Linux, so neither has a second spelling or a Linux name to hold.
    windows_extra = (tuple(name + suffix for name in fuzz_targets for suffix in ('.exe', '_d.exe'))
                     + tuple(records) + tuple(name + '.exe' for name in tools) + raw_keep_out)
    linux_extra = (tuple(name + suffix for name in fuzz_targets for suffix in ('', '_d'))
                   + tuple(records) + tuple(tools))
    for relative, parse, must_not_ship, must_ship, must_ship_paths in (
        (WINDOWS_PACKAGER, parse_windows_packager, lists['windows'][1] + windows_extra, lists['windows'][0] + raw_ship,
         sdk_paths['windows']),
        (LINUX_PACKAGER, parse_linux_packager, lists['linux'][1] + linux_extra, lists['linux'][0], sdk_paths['linux']),
    ):
        found, label = check_packager(options.repo_root, relative, parse, must_not_ship, must_ship, must_ship_paths)
        problems.extend(found)
        if label is not None:
            inspected.append(label)

    problems.extend(packaged_version_problems(options.repo_root))
    linux_packager = os.path.join(options.repo_root, LINUX_PACKAGER)
    if os.path.isfile(linux_packager):
        with open(linux_packager, encoding='utf-8', errors='replace') as handle:
            text = handle.read()
        problems.extend(linux_application_problems(text, separate))
        problems.extend(linux_cli_launcher_problems(text))
    problems.extend(desktop_entry_problems(options.repo_root))

    out.write('inspected packagers: %s, against the %s profile\n'
              % (', '.join(inspected) if inspected else 'none', profile or 'unknown'))
    for problem in problems:
        out.write('%s\n' % problem)
    if problems:
        out.write('%d installer payload problem(s)\n' % len(problems))
        return 1
    out.write('installer payload ok\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
