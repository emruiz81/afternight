# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Hold the capability register of the interface to the suites that prove it.

`src/AfterNightTests/Test_ShellParity/Capabilities.txt` lists every capability of the interface, a record each:

    capability view.context-menu
        title        The context menu of a view, entry for entry
        shared       ViewMenuBuilder.EqualsTheRecordedTree
        classic      ShellScenario.ViewMenuEntries/classic
        observatory  pending

`shared` names cases of code every shell uses - a dialog, a control, a model; `classic` and `observatory` name cases
that drive that shell's own window. A case is named `Suite.Case`, and a scenario run for one shell `Suite.Case/<shell>`,
as Google Test names it. The check holds the register to the tree:

- every record states its title and its three lines, in that order, and no capability is listed twice;
- every case named exists in the suites' sources under src/AfterNightTests - a `TEST`, `TEST_F`, `TEST_P`,
  `TYPED_TEST` or `TYPED_TEST_P` of that suite and name - and a case named for a shell is a value-parameterised case,
  named on that shell's line; a value-parameterised case named whole, every instance of it, is named so only on the
  `shared` line;
- every capability has an `observatory` line: one case or more, or the word `pending` until its cases are written.

An empty `shared` or `classic` line is a capability with no case of that kind: 1.1 has no case for it, or no code of
it is shared.

With `--require-shell <shell>`, no line of that shell may be `pending` or empty: every capability names a case that
drives that shell - the condition a shell meets before the shell it replaces is retired.

Usage:
    python build/scripts/tools/check_shell_capabilities.py [--repo-root <dir>] [--require-shell <shell>]
"""

from __future__ import annotations

import argparse
import dataclasses
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
REGISTER = 'src/AfterNightTests/Test_ShellParity/Capabilities.txt'
TESTS_ROOT = 'src/AfterNightTests'
SHELLS = ('classic', 'observatory')
FIELDS = ('title', 'shared') + SHELLS
PENDING = 'pending'

CAPABILITY_NAME = re.compile(r'^[a-z][a-z0-9-]*(\.[a-z][a-z0-9-]*)+$')
CASE_NAME = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)(?:/([A-Za-z0-9_-]+))?$')
CASE_DEFINITION = re.compile(
    r'\b(TEST|TEST_F|TEST_P|TYPED_TEST|TYPED_TEST_P)\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)')
SOURCE_SUFFIXES = ('.cpp', '.h', '.hpp', '.inl')


@dataclasses.dataclass
class Capability:
    name: str
    line: int
    fields: dict[str, str] = dataclasses.field(default_factory=dict)

    def cases(self, field: str) -> list[str]:
        text = self.fields.get(field, '')
        return [name.strip() for name in text.split(',') if name.strip()]


def read_register(repo_root: pathlib.Path) -> tuple[list[Capability], list[str]]:
    """The register's records, and what is wrong with its form."""
    path = repo_root / REGISTER
    if not path.is_file():
        return [], [f'{REGISTER} does not exist']
    problems: list[str] = []
    capabilities: list[Capability] = []
    current: Capability | None = None
    for number, raw in enumerate(path.read_text(encoding='utf-8-sig').splitlines(), start=1):
        line = raw.rstrip()
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        where = f'{REGISTER}:{number}'
        if line.startswith('capability '):
            current = Capability(name=line[len('capability '):].strip(), line=number)
            capabilities.append(current)
            continue
        if not line.startswith('    ') or current is None:
            problems.append(f'{where}: {line.strip()!r} is neither a capability nor a line of one')
            continue
        field, _, value = line.strip().partition(' ')
        if field not in FIELDS:
            problems.append(f'{where}: {current.name} has a line {field!r}, which is not one of {", ".join(FIELDS)}')
            continue
        if field in current.fields:
            problems.append(f'{where}: {current.name} states its {field} line twice')
            continue
        expected = FIELDS[len(current.fields)] if len(current.fields) < len(FIELDS) else None
        if field != expected:
            problems.append(f'{where}: {current.name} states its {field} line where its {expected} line belongs; the '
                            f'lines are {", ".join(FIELDS)}, in that order')
        current.fields[field] = value.strip()
    return capabilities, problems


def scan_cases(repo_root: pathlib.Path) -> dict[tuple[str, str], str]:
    """Every case the suites define, by suite and name, with the macro that defines it."""
    cases: dict[tuple[str, str], str] = {}
    root = repo_root / TESTS_ROOT
    if not root.is_dir():
        return cases
    for path in sorted(root.rglob('*')):
        if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
            continue
        text = path.read_bytes().decode('utf-8', errors='replace')
        for match in CASE_DEFINITION.finditer(text):
            cases.setdefault((match.group(2), match.group(3)), match.group(1))
    return cases


def case_problems(capability: Capability, field: str, name: str, cases: dict[tuple[str, str], str]) -> list[str]:
    where = f'{REGISTER}:{capability.line}: {capability.name}'
    match = CASE_NAME.match(name)
    if match is None:
        return [f'{where} names {name!r} on its {field} line, which is not a case name (Suite.Case or '
                'Suite.Case/<shell>)']
    suite, case, instance = match.groups()
    macro = cases.get((suite, case))
    if macro is None:
        return [f'{where} names {suite}.{case} on its {field} line, which no suite under {TESTS_ROOT} defines']
    parameterised = macro == 'TEST_P'
    if instance is None:
        # A parameterised case named whole means every instance of it - on a shell's line, the one run for that shell.
        if parameterised and field in SHELLS:
            return [f'{where} names {suite}.{case}, a value-parameterised case, on its {field} line without the '
                    f'instance run for that shell; name it {suite}.{case}/{field}']
        return []
    if not parameterised:
        return [f'{where} names {name}, an instance of {suite}.{case}, which is not a value-parameterised case']
    if instance in SHELLS and instance != field:
        return [f'{where} names {name}, a case run for the {instance} shell, on its {field} line']
    if field in SHELLS and instance != field:
        return [f'{where} names {name} on its {field} line, which names the instance a case ran for that shell in: '
                f'{suite}.{case}/{field}']
    return []


def check(repo_root: pathlib.Path, required_shells: tuple[str, ...] = ()) -> list[str]:
    capabilities, problems = read_register(repo_root)
    cases = scan_cases(repo_root)
    seen: dict[str, int] = {}
    for capability in capabilities:
        where = f'{REGISTER}:{capability.line}: {capability.name}'
        if not CAPABILITY_NAME.match(capability.name):
            problems.append(f'{where} is not a capability name: lowercase words joined by hyphens, an area and a '
                            'capability joined by a dot')
        if capability.name in seen:
            problems.append(f'{where} is listed twice; first at line {seen[capability.name]}')
            continue
        seen[capability.name] = capability.line
        missing = [field for field in FIELDS if field not in capability.fields]
        if missing:
            problems.append(f'{where} states no {", ".join(missing)} line')
        if 'title' in capability.fields and not capability.fields['title']:
            problems.append(f'{where} has an empty title')
        for field in ('shared',) + SHELLS:
            if field not in capability.fields:
                continue
            names = capability.cases(field)
            if len(names) != len(set(names)):
                problems.append(f'{where} names a case twice on its {field} line')
            if field == 'observatory' or field in required_shells:
                if not names:
                    problems.append(f'{where} names no case on its {field} line, and is not {PENDING}')
                    continue
                if PENDING in names:
                    if len(names) > 1:
                        problems.append(f'{where} is {PENDING} on its {field} line and names cases there too')
                    elif field in required_shells:
                        problems.append(f'{where} is still {PENDING} for the {field} shell')
                    continue
            for name in names:
                if name == PENDING:
                    problems.append(f'{where} is {PENDING} on its {field} line, which only an observatory line may be')
                    continue
                problems.extend(case_problems(capability, field, name, cases))
    return problems


def summary(repo_root: pathlib.Path) -> str:
    capabilities, _ = read_register(repo_root)
    named = {field: sum(len([name for name in capability.cases(field) if name != PENDING])
                        for capability in capabilities) for field in ('shared',) + SHELLS}
    pending = sum(1 for capability in capabilities if capability.cases('observatory') == [PENDING])
    without = sum(1 for capability in capabilities if not capability.cases('classic'))
    return (f'the capability register holds {len(capabilities)} capabilities and the suites define every case it '
            f'names: shared {named["shared"]}, classic {named["classic"]}, observatory {named["observatory"]}; '
            f'{without} with no classic case, {pending} pending for the observatory shell')


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--repo-root', type=pathlib.Path, default=REPO_ROOT)
    parser.add_argument('--require-shell', action='append', choices=SHELLS, default=[],
                        help='a shell every capability must name a case of: none may be pending')
    arguments = parser.parse_args(argv)
    repo_root = arguments.repo_root.resolve()
    problems = check(repo_root, tuple(arguments.require_shell))
    for problem in problems:
        print(f'ERROR: {problem}', file=sys.stderr)
    if problems:
        print(f'{len(problems)} capability register problem(s)', file=sys.stderr)
        return 1
    print(summary(repo_root))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
