# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Hold the engine's implementation and its guides to the documentation floor a machine can judge.

The public reference has had a check since the first header - Doxygen refuses an undocumented
published declaration - and everything behind it was left to review. Review is still what judges
whether a comment states the ownership, the numeric behaviour and the reason a less obvious design
was chosen. But part of the standard every file of this engine is held to is a property a machine
can read, and a property nobody reads is one nobody has broken yet, which from inside looks exactly
like one that holds. So this reads five of them, and one more over the guides:

1. **Every file says what it is for.** Each header, source and inline file of the engine, the two
   optional adapters and the opt-in device backend opens with a comment before its first line of
   code: the module's responsibility, which is the one thing a reader needs before any declaration
   makes sense. A published header's comment is additionally a `/// @file` block, so that it reaches
   the reference; `check_public_documentation.py` holds that half.

2. **Every declaration in a private header is documented.** A private header is an internal
   interface - one translation unit declares something so another can use it, and the second has
   only the declaration to go on. `build/doxygen/AfterNightEngineInternal.doxyfile` runs the
   same generator the public check runs over every private header, without asking for the
   parameter-by-parameter contract a published declaration carries, and a diagnostic is a finding.
   A parameter list that is documented must be documented correctly, so a `@param` that names an
   argument the function no longer has is a finding too.

3. **No comment or message names a unit of work.** A work unit's identifier, a coordinate in a
   planning document, a registry number, a plan's file name or a tracker handle means nothing to a
   reader once its plan is archived. The reason is stated instead. This holds across the engine, the
   adapters, the backend, the engine's suites and fuzz targets and its benchmark layer.

4. **No untracked marker.** A known limitation is stated beside the interface it limits; a `TODO` or
   `FIXME` is a limitation left unstated.

5. **A path a comment cites exists.** A document, script or register named in a comment is a promise
   that a reader can open it, and a renamed file breaks the promise silently. A derived file names the
   upstream file it credits without a path of this tree, which that file is not in.

6. **The guides name what exists.** Every guide under `docs/dev/guides/`, the public API
   contract and the reference's main page are read for their links and for every code span that
   names a repository path or a C++ identifier: a link must resolve, a path must exist, and an
   identifier must be spelled somewhere in the code of the tree. The guides index lists every guide.
   This is a floor against the commonest way prose goes stale - a renamed symbol or moved file - and
   says nothing about whether a sentence is still true.

What this cannot judge is most of the standard: whether a module comment explains the module,
whether a function's documentation states what a caller needs, whether an algorithm names its
source. Review does that, by reading what a change touches.

Usage:
    python build/scripts/tools/check_engine_documentation.py [--repo-root <dir>]
                                                             [--doxygen <path>]
                                                             [--output-dir <dir>]
                                                             [--skip-doxygen]

Exits 0 when every rule holds, 1 when any rule found a problem or the internal configuration has
been weakened, and 2 when every other rule held but Doxygen could not run the second rule - which is
not a pass, because the rule it could not apply is the one about declarations. `--skip-doxygen`
applies the other rules alone and says so. DOXYGEN_BIN names the executable when it is not on PATH.

Tests: build/scripts/tools/check_engine_documentation_test.py
"""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_public_documentation as public  # noqa: E402
import source_scan  # noqa: E402

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]

INTERNAL_DOXYFILE = 'build/doxygen/AfterNightEngineInternal.doxyfile'

# The components whose every file is held to rules 1, 2, 4 and 5. The opt-in backend is read whether
# or not a tree was generated with it, as every other checker on this side of the workspace does.
COMPONENT_ROOTS = (
    'src/AfterNightEngine',
    'src/AfterNightEngineGmicAdapter',
    'src/AfterNightEnginePythonAdapter',
    'src/AfterNightEngineOpenClBackend',
)

# Rule 3 reaches further than the components, because a test failure message and a benchmark row's
# comment are read by the same person as the code they test or measure. The engine's suites and fuzz
# targets are found by the prefixes their names carry, and the suite that checks the engine against
# its recorded expectations by its name.
REFERENCE_ROOTS = ('src/AfterNightBench/engine',)
REFERENCE_GLOBS = ('src/AfterNightTests/Test_FixtureEngine', 'src/AfterNightTests/Test_Engine*',
                   'src/AfterNightTests/Fuzz_Engine*', 'src/AfterNightTests/FuzzEngine*')

SOURCE_SUFFIXES = ('.h', '.hpp', '.inl', '.cpp')

# The settings that make the internal configuration a check. Any of them turned around makes the run
# quiet rather than clean, which is the failure a guard exists for.
INTERNAL_REQUIRED_SETTINGS = {
    'WARNINGS': 'YES',
    'WARN_IF_UNDOCUMENTED': 'YES',
    'WARN_IF_DOC_ERROR': 'YES',
    'WARN_IF_INCOMPLETE_DOC': 'YES',
    'WARN_IF_UNDOC_ENUM_VAL': 'YES',
    'WARN_AS_ERROR': 'FAIL_ON_WARNINGS',
    'EXTRACT_ALL': 'NO',
    'EXTRACT_STATIC': 'YES',
    'HIDE_UNDOC_MEMBERS': 'NO',
    'HIDE_UNDOC_CLASSES': 'NO',
    'HIDE_UNDOC_NAMESPACES': 'NO',
    'RECURSIVE': 'YES',
    'JAVADOC_AUTOBRIEF': 'YES',
    'FILE_PATTERNS': '*.h *.inl',
}

GUIDES_DIR = 'docs/dev/guides'
GUIDES_INDEX = GUIDES_DIR + '/README.md'
# Documents outside the guides directory that are read as guides: what is true of every header, and
# the page the generated reference opens on.
EXTRA_GUIDES = ('docs/dev/PUBLIC_API_CONTRACT.md', 'build/doxygen/AfterNightEngine.mainpage.md')

TOOL_UNUSABLE = 2
PROBLEMS_FOUND = 1
CLEAN = 0

# ---- Rule 3: the spellings of a reference to a unit of work.
#
# Each is written to be precise over this tree rather than to be complete, because a rule that fires
# on "a phase of the pipeline" or "the plan a planner returns" - both of which this engine says on
# purpose - is a rule that gets switched off. `R5` alone is read only in comments: a string literal
# may hold a camera model.
_WORK_UNIT = r'\bR(?:[0-9]|1[0-9])\.(?:[0-9]{1,2}[a-z]?|G)\b'
_WORK_SERIES = r'\bR(?:[0-9]|1[0-9])\b(?![.\-/])'
_COORDINATE = (r'\b[Ss]ection\s+\d+(?:\.\d+)+|§\s*\d|\bAppendix\s+[A-Z]\b|'
               r'\b[Dd]ecision\s+\d+(?:\.\d+)+|\b(?:ADR|REQ)-\d+\b')
_ARTIFACT = r'_PLAN_20\d\d|_REVIEW_20\d\d|\b(?:issue|PR|pull request)\s*#\d+'
COMMENT_REFERENCE = re.compile('|'.join((_WORK_UNIT, _WORK_SERIES, _COORDINATE, _ARTIFACT)))
LITERAL_REFERENCE = re.compile('|'.join((_WORK_UNIT, _COORDINATE, _ARTIFACT)))

# ---- Rule 4.
MARKER = re.compile(r'\b(?:TODO|FIXME|HACK|XXX)\b')

# ---- Rule 5. A repository path.
CITED_PATH = re.compile(r'(?<![\w/.-])(?:docs|build|src)/[A-Za-z0-9_./-]*[A-Za-z0-9_]\.[A-Za-z0-9]+')

# ---- Rule 6.
LINK = re.compile(r'\[[^\]\n]*\]\(([^)\s]+)\)')
CODE_SPAN = re.compile(r'`([^`\n]+)`')
FENCE = re.compile(r'^\s*(```|~~~)')
IDENTIFIER_SPAN = re.compile(r'^(?:[A-Za-z_]\w*::)*~?[A-Za-z_]\w*(?:\(\))?$')
ASSIGNMENT_SPAN = re.compile(r'^([A-Z][A-Z0-9_]+)=\S*$')
PATH_SPAN = re.compile(r'^<?[A-Za-z0-9_.][A-Za-z0-9_./-]*>?$')
PATH_SUFFIXES = ('.h', '.hpp', '.inl', '.cpp', '.py', '.md', '.txt', '.bat', '.sh', '.lua', '.json',
                 '.tsv', '.doxyfile', '.cmake', '.ps1', '.cl')
IDENTIFIER = re.compile(r'[A-Za-z_]\w*')
# Names a guide may give from another library's own namespace, which no source of this tree defines.
EXTERNAL_NAMESPACES = ('std', 'cv', 'tbb', 'hwy', 'Exiv2', 'pybind11', 'py', 'gmic_library', 'Imf',
                       'LibRaw', 'rawspeed')
# Where a relative path in a code span is looked for, in order. A guide names a published header by
# its area (`Runtime/EngineContext.h`), a private one by its layer (`runtime/ProgressRange.h`), a
# unit of the product by its path inside its library (`controls/ImageViewControl.h`,
# `workflow/stages/StageFramePipeline.h`), a script by its name, and a document relative to itself.
PATH_BASES = ('', 'src/AfterNightEngine/include/AfterNightEngine', 'src/AfterNightEngine/include',
              'src/AfterNightEngine/src', 'src/AfterNightEnginePythonAdapter/include',
              'src/AfterNightEngineGmicAdapter/include', 'src', 'src/AfterNightTests', 'src/AfterNightBench',
              'src/AfterNightPlatform/include', 'src/AfterNightPlatform/include/AfterNightPlatform',
              'src/AfterNightConsumerRuntime/include',
              'src/AfterNightConsumerRuntime/include/AfterNightConsumerRuntime', 'src/AfterNightCLI',
              'src/AfterNightControls', 'src/AfterNightProcesses', 'src/AfterNightProcesses/processes',
              'src/AfterNightProcesses/processes/ImageIntegration', 'src/AfterNightApp',
              'src/AfterNightExtensionHost', 'src/AfterNightExtensionRuntimeBackend',
              'build/scripts/tools', 'build/scripts/msvc', 'build/scripts/linux', 'build/scripts',
              'build/premake', 'build/doxygen', 'docs/dev', GUIDES_DIR,
              )
# Where the identifier universe is read from: every C++ source in the tree, since a guide names the
# engine and its consumers alike, and the scripts that define the build switches a guide names.
UNIVERSE_CODE_ROOTS = ('src',)
UNIVERSE_SCRIPT_ROOTS = ('build/premake', 'build/scripts')
UNIVERSE_SCRIPT_SUFFIXES = ('.py', '.bat', '.sh', '.lua', '.ps1', '.txt', '.cmake')


class Problem:
    """One finding: where it is, which rule, and what is wrong."""

    def __init__(self, path, line, rule, message):
        self.path = path
        self.line = line
        self.rule = rule
        self.message = message

    def __str__(self):
        return '%s:%d: %s: %s' % (self.path, self.line, self.rule, self.message)


def read_text(path):
    return path.read_bytes().decode('utf-8', errors='replace').replace('\r\n', '\n')


def tokens_with_lines(text):
    """Every comment and literal of a source as (line, kind, text), the line being where it starts."""
    line = 1
    last = 0
    for start, end, kind in source_scan.tokens(text):
        line += text.count('\n', last, start)
        last = start
        yield line, kind, text[start:end]


def first_code_line(text):
    """The number of the first line holding code rather than a comment or a directive, or None.

    A directive continues while its line ends in a backslash, so a multi-line macro is the one
    directive it is rather than the code it expands to.
    """
    continued = False
    for number, line in enumerate(source_scan.strip_comments(text).split('\n'), start=1):
        stripped = line.strip()
        if continued:
            continued = stripped.endswith('\\')
            continue
        if not stripped:
            continue
        if stripped.startswith('#'):
            continued = stripped.endswith('\\')
            continue
        return number
    return None


def has_module_comment(text):
    """Whether a comment stands before the first line of code, where a module's comment goes.

    The licence header's SPDX lines stand there too and say nothing of what the file is for, so they are not one.
    """
    code_line = first_code_line(text)
    for line, kind, body in tokens_with_lines(text):
        if kind == 'literal':
            continue
        if code_line is not None and line >= code_line:
            return False
        content = body.strip('/*! \t\n')
        if content and not content.startswith('SPDX-'):
            return True
    return False


def component_files(repo_root, roots=COMPONENT_ROOTS):
    """Every source of the components present in `repo_root`, by relative path, in a stable order."""
    found = []
    for root in roots:
        base = repo_root / root
        if not base.is_dir():
            continue
        for path in sorted(base.rglob('*')):
            if path.is_file() and path.suffix in SOURCE_SUFFIXES:
                found.append((path.relative_to(repo_root).as_posix(), path))
    return found


def reference_files(repo_root):
    """Every source rule 3 and rule 5 read: the components, the engine's suites and the benchmark layer."""
    roots = list(COMPONENT_ROOTS) + list(REFERENCE_ROOTS)
    for pattern in REFERENCE_GLOBS:
        roots += sorted(path.relative_to(repo_root).as_posix() for path in repo_root.glob(pattern)
                        if path.is_dir())
    # The globs overlap, and a directory two of them reach is read once.
    return component_files(repo_root, list(dict.fromkeys(roots)))


def check_module_comments(repo_root, files):
    problems = []
    for relative, path in files:
        if not has_module_comment(read_text(path)):
            problems.append(Problem(relative, 1, 'module-comment',
                                    'opens with no comment saying what the file is for'))
    return problems


def _match_lines(line, body, pattern):
    for match in pattern.finditer(body):
        yield line + body.count('\n', 0, match.start()), match.group(0)


def check_references_and_markers(repo_root, reference, components):
    """Rules 3, 4 and 5 over one pass of each file's comments and literals."""
    problems = []
    component_paths = {relative for relative, _ in components}
    for relative, path in reference:
        text = read_text(path)
        in_component = relative in component_paths
        for line, kind, body in tokens_with_lines(text):
            if kind == 'literal':
                for number, found in _match_lines(line, body, LITERAL_REFERENCE):
                    problems.append(Problem(relative, number, 'planning-reference',
                                            "'%s' in a literal names a unit of work or a place in a planning "
                                            'document; state the reason instead' % found))
                continue
            for number, found in _match_lines(line, body, COMMENT_REFERENCE):
                problems.append(Problem(relative, number, 'planning-reference',
                                        "'%s' names a unit of work or a place in a planning document; "
                                        'state the reason instead' % found))
            if in_component:
                for number, found in _match_lines(line, body, MARKER):
                    problems.append(Problem(relative, number, 'untracked-marker',
                                            "'%s' marks a limitation with no statement; state it beside the "
                                            'interface it limits' % found))
            for number, found in _match_lines(line, body, CITED_PATH):
                if not cited_path_exists(repo_root, found):
                    problems.append(Problem(relative, number, 'cited-path',
                                            "'%s' is cited and does not exist" % found))
    return problems


def cited_path_exists(repo_root, cited):
    return (repo_root / cited.rstrip('.')).exists()


# ---- Rule 2.

def appended_values(text, name):
    """Every value a `NAME += ...` line appends, in order."""
    values = []
    pattern = re.compile(r'^\s*%s\s*\+=\s*(.*?)\s*$' % re.escape(name))
    for line in text.splitlines():
        if line.lstrip().startswith('#'):
            continue
        match = pattern.match(line)
        if match:
            values += match.group(1).split()
    return values


def check_internal_configuration(text, repo_root):
    """The internal configuration still carries the settings that make it a check, and reads every
    component's private tree."""
    settings = public.read_settings(text)
    problems = []
    for name in sorted(INTERNAL_REQUIRED_SETTINGS):
        expected = INTERNAL_REQUIRED_SETTINGS[name]
        actual = settings.get(name)
        if actual != expected:
            problems.append('%s: %s is %s; it must be %s for this to be a check'
                            % (INTERNAL_DOXYFILE, name, 'not set' if actual is None else actual, expected))
    inputs = settings.get('INPUT', '').split() + appended_values(text, 'INPUT')
    for root in COMPONENT_ROOTS:
        private = root + '/src'
        if (repo_root / private).is_dir() and private not in inputs:
            problems.append('%s: INPUT does not read %s, so its private headers are not checked'
                            % (INTERNAL_DOXYFILE, private))
    return problems


def run_internal_doxygen(repo_root, executable, text, output_dir=None, runner=None):
    """(exit code, diagnostics) of one run of the internal configuration."""
    if output_dir:
        pathlib.Path(output_dir).mkdir(parents=True, exist_ok=True)
        code, output = public.run_doxygen(executable, public.compose_configuration(text, output_dir),
                                          repo_root, runner)
    else:
        with tempfile.TemporaryDirectory(prefix='an-engine-internal-reference-') as temporary:
            code, output = public.run_doxygen(executable, public.compose_configuration(text, temporary),
                                              repo_root, runner)
    return code, public.extract_diagnostics(output)


# ---- Rule 6.

def guide_files(repo_root):
    found = []
    base = repo_root / GUIDES_DIR
    if base.is_dir():
        found += sorted(path.relative_to(repo_root).as_posix() for path in base.glob('*.md'))
    found += [relative for relative in EXTRA_GUIDES if (repo_root / relative).is_file()]
    return found


def identifier_universe(repo_root):
    """Every identifier spelled in the code of the tree's C++ sources and build scripts."""
    names = set()
    for root in UNIVERSE_CODE_ROOTS:
        base = repo_root / root
        if not base.is_dir():
            continue
        for path in base.rglob('*'):
            if path.suffix in SOURCE_SUFFIXES and path.is_file():
                names.update(IDENTIFIER.findall(source_scan.strip_comments(read_text(path))))
    for root in UNIVERSE_SCRIPT_ROOTS:
        base = repo_root / root
        if not base.is_dir():
            continue
        for path in base.rglob('*'):
            if path.suffix in UNIVERSE_SCRIPT_SUFFIXES and path.is_file():
                names.update(IDENTIFIER.findall(read_text(path)))
    return names


def _prose_lines(text):
    """(line number, line) for every line of a Markdown document outside a fenced block."""
    fenced = False
    for number, line in enumerate(text.split('\n'), start=1):
        if FENCE.match(line):
            fenced = not fenced
            continue
        if not fenced:
            yield number, line


def resolve_path(repo_root, document, span):
    """Whether a path named in a guide exists, looked for where a guide would mean it."""
    candidate = span.strip('<>').rstrip('/')
    if candidate.startswith('./'):
        candidate = candidate[2:]
    own = (repo_root / document).parent
    if (own / candidate).exists():
        return True
    return any((repo_root / base / candidate).exists() for base in PATH_BASES)


def is_path_span(span):
    if not PATH_SPAN.match(span) or '..' in span:
        return False
    stripped = span.strip('<>')
    # A suffix on its own - "a shared `.inl`" - names a kind of file, not a file.
    if stripped.startswith('.') or not pathlib.PurePosixPath(stripped).stem.strip('_'):
        return False
    return stripped.endswith(PATH_SUFFIXES) or ('/' in stripped and stripped.endswith('/')) or \
        (stripped.count('/') >= 1 and pathlib.PurePosixPath(stripped).suffix in PATH_SUFFIXES)


def check_guides(repo_root, universe=None):
    problems = []
    guides = guide_files(repo_root)
    if not (repo_root / GUIDES_INDEX).is_file():
        problems.append(Problem(GUIDES_INDEX, 1, 'guide-index', 'the guides index does not exist'))
    else:
        index = read_text(repo_root / GUIDES_INDEX)
        linked = {target.split('#')[0] for target in LINK.findall(index)}
        for relative in guides:
            if not relative.startswith(GUIDES_DIR + '/') or relative == GUIDES_INDEX:
                continue
            name = relative[len(GUIDES_DIR) + 1:]
            if name not in linked:
                problems.append(Problem(GUIDES_INDEX, 1, 'guide-index', '%s is a guide the index does not list'
                                        % name))
    if universe is None:
        universe = identifier_universe(repo_root)
    for relative in guides:
        text = read_text(repo_root / relative)
        own = (repo_root / relative).parent
        for number, line in _prose_lines(text):
            for target in LINK.findall(line):
                if re.match(r'^(?:[a-z]+:|#)', target):
                    continue
                path = target.split('#')[0]
                if path and not (own / path).exists():
                    problems.append(Problem(relative, number, 'guide-link', "'%s' does not resolve" % target))
            for span in CODE_SPAN.findall(line):
                span = span.strip()
                problems += _check_span(repo_root, relative, number, span, universe)
    return problems


def _check_span(repo_root, relative, number, span, universe):
    if any(marker in span for marker in ('<Area>', '<Name>', '<toolchain>', '<configuration>', '*', '...')):
        return []
    assignment = ASSIGNMENT_SPAN.match(span)
    if assignment:
        if assignment.group(1) not in universe:
            return [Problem(relative, number, 'guide-name', "'%s' is not spelled anywhere in the tree's code or "
                            'scripts' % assignment.group(1))]
        return []
    if is_path_span(span):
        if not resolve_path(repo_root, relative, span):
            return [Problem(relative, number, 'guide-path', "'%s' names a path that does not exist" % span)]
        return []
    if IDENTIFIER_SPAN.match(span):
        parts = span.replace('()', '').lstrip('~').split('::')
        if parts[0] in EXTERNAL_NAMESPACES:
            return []
        missing = [part.lstrip('~') for part in parts if part.lstrip('~') not in universe]
        if missing:
            return [Problem(relative, number, 'guide-name', "'%s' names %s, which is not spelled anywhere in the "
                            "tree's code" % (span, ', '.join("'%s'" % name for name in missing)))]
    return []


# ---- Everything.

def check_lexical(repo_root):
    """Rules 1, 3, 4, 5 and 6, which need nothing but a checkout."""
    components = component_files(repo_root)
    problems = check_module_comments(repo_root, components)
    problems += check_references_and_markers(repo_root, reference_files(repo_root), components)
    problems += check_guides(repo_root)
    return problems, components


def main(argv=None, stream=None, runner=None, environment=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--repo-root', default=str(REPO_ROOT),
                        help='repository root to check; defaults to the one containing this script')
    parser.add_argument('--doxygen', default=None,
                        help='Doxygen executable; defaults to DOXYGEN_BIN, then PATH')
    parser.add_argument('--output-dir', default=None,
                        help='keep the generated internal reference here instead of a temporary directory')
    parser.add_argument('--skip-doxygen', action='store_true',
                        help='apply every rule but the internal-declaration rule, and say so')
    arguments = parser.parse_args(argv)
    out = stream if stream is not None else sys.stdout
    repo_root = pathlib.Path(arguments.repo_root).resolve()

    problems, components = check_lexical(repo_root)
    for problem in problems:
        print(problem, file=out)

    declaration_problems = []
    unusable = None
    if arguments.skip_doxygen:
        unusable = 'the internal-declaration rule was skipped on request, so no private declaration was checked'
    else:
        doxyfile = repo_root / INTERNAL_DOXYFILE
        if not doxyfile.is_file():
            declaration_problems.append('%s: the internal configuration does not exist' % INTERNAL_DOXYFILE)
        else:
            text = doxyfile.read_bytes().decode('utf-8')
            configuration = check_internal_configuration(text, repo_root)
            if configuration:
                declaration_problems += configuration
            else:
                executable = public.find_doxygen(arguments.doxygen, environment)
                if executable is None:
                    unusable = ('doxygen was not found, so no private declaration was checked. Install it, or '
                                'name it with --doxygen or DOXYGEN_BIN.')
                else:
                    code, diagnostics = run_internal_doxygen(repo_root, executable, text,
                                                             arguments.output_dir, runner)
                    about_the_run = [line for line in diagnostics if not public.is_source_diagnostic(line)]
                    if about_the_run:
                        for line in about_the_run:
                            print(line, file=out)
                        unusable = ('doxygen did not run %s as written: %d diagnostic(s) above are about the run '
                                    'rather than a declaration' % (INTERNAL_DOXYFILE, len(about_the_run)))
                    else:
                        declaration_problems += diagnostics
                        if not diagnostics and code != 0:
                            unusable = ('doxygen exited %d without reporting a diagnostic, so nothing can be '
                                        'concluded about the private declarations' % code)
    for problem in declaration_problems:
        print(problem, file=out)

    total = len(problems) + len(declaration_problems)
    if total:
        print('%d documentation problem(s) in the engine, its adapters, its backend and its guides' % total,
              file=out)
        if unusable:
            print(unusable, file=out)
        return PROBLEMS_FOUND
    if unusable:
        print(unusable, file=out)
        return CLEAN if arguments.skip_doxygen else TOOL_UNUSABLE
    print('the engine documentation floor holds: %d file(s) open with a module comment, every private '
          'declaration is documented, no comment names a unit of work, every cited path exists, and '
          '%d guide(s) name only what exists' % (len(components), len(guide_files(repo_root))), file=out)
    return CLEAN


if __name__ == '__main__':
    sys.exit(main())
