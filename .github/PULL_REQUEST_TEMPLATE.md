## What changed and why

<!-- What the change does, the problem it solves, and the issue it closes ("Closes #..."). -->

## Tests

<!-- The test that failed before the change and passes after it. A change that cannot be tested automatically says
why and what was checked instead. -->

## Validation

<!-- What you ran, with the counts: the build and configuration, each suite and how many cases ran, passed and were
skipped, the checks from docs/dev/LOCAL_VALIDATION.md, and anything you could not run. -->

## Checklist

- [ ] Every commit is signed off (`git commit -s`), under the [Developer Certificate of Origin](https://developercertificate.org/).
- [ ] A new feature has a test that failed first; a bug fix has a regression test.
- [ ] Changed C and C++ files under `src/` pass `check_clang_format` against `origin/main`.
- [ ] New and changed text files use CRLF (LF only where AGENTS.md says), with no mixed endings.
- [ ] Every new source file opens with its SPDX licence and copyright lines, and `check_license_headers.py` passes.
- [ ] Code derived from another project carries its credit, copyright line and licence in every file that holds it,
      with its provenance note and About dialog credit ("Credit for derived code" in AGENTS.md).
- [ ] The guide or user-guide page that describes the changed behaviour is updated in this change.
