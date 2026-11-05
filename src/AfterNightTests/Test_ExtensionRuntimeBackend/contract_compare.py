# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Comparing runtime SDK contracts, and holding the differences between two records to their register.

`compare` lists every difference between a recorded contract and another one, holding floating-point numbers to
`FLOAT_BOUND` absolutely and relatively - the bound a record is compared with on another host. `unexplained` holds the
differences between the recorded reference contract and the current record to
`runtime-bindings-differences.json`: a difference no entry names is unexplained, and an entry that names no difference
is stale, so the register can neither fall behind the code nor outlive what it explains. An entry names the
differences at and below its path, or only the one at its path when it is `exact`.
"""

FLOAT_BOUND = 1e-6

CLASSES = ("defect repair", "intentional change", "algorithm change", "precision change", "representation change")


def compare(expected, actual, where="contract", problems=None):
    """Every difference between a recorded contract and another; floats are held to FLOAT_BOUND."""
    problems = [] if problems is None else problems
    if isinstance(expected, dict) and isinstance(actual, dict):
        if sorted(expected) != sorted(actual):
            problems.append("%s: keys %s, recorded %s" % (where, sorted(actual), sorted(expected)))
        for key in expected:
            if key in actual:
                compare(expected[key], actual[key], "%s.%s" % (where, key), problems)
    elif isinstance(expected, list) and isinstance(actual, list):
        if len(expected) != len(actual):
            problems.append("%s: %d items, recorded %d" % (where, len(actual), len(expected)))
        for index, (left, right) in enumerate(zip(expected, actual)):
            label = left.get("call", index) if isinstance(left, dict) else index
            compare(left, right, "%s[%s]" % (where, label), problems)
    elif isinstance(expected, float) and isinstance(actual, (float, int)) and not isinstance(actual, bool):
        bound = FLOAT_BOUND * max(1.0, abs(expected))
        if abs(expected - actual) > bound:
            problems.append("%s: %r, recorded %r" % (where, actual, expected))
    elif expected != actual:
        problems.append("%s: %r, recorded %r" % (where, actual, expected))
    return problems


def _names(entry, path):
    """Whether an entry names a difference: its own path, or - unless the entry is exact - any path below it."""
    stated = entry["path"]
    if entry.get("exact", False):
        return path == stated
    return path == stated or path.startswith(stated + ".") or path.startswith(stated + "[")


def unexplained(differences, register):
    """The differences no register entry names, the entries that name none, and the entries that are malformed."""
    entries = register.get("differences", [])
    malformed = [entry.get("path", "?") for entry in entries
                 if entry.get("class") not in CLASSES or not entry.get("reason") or not entry.get("path")]
    used = set()
    missing = []
    for difference in differences:
        path = difference.split(": ", 1)[0]
        hits = [index for index, entry in enumerate(entries) if _names(entry, path)]
        if hits:
            used.update(hits)
        else:
            missing.append(difference)
    stale = [entry["path"] for index, entry in enumerate(entries) if index not in used]
    return missing, stale, malformed
