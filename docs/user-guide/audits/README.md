# User Guide Audit Framework

UG-01 adds a deterministic, read-only audit of the navigated English guide.
Run it from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\audit_user_guide.ps1
```

Write a reviewable report or compare current output with the checked-in
baseline:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\audit_user_guide.ps1 `
  -OutputPath artifacts\user-guide-audit.md
powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\audit_user_guide.ps1 `
  -CheckBaseline
```

`-FailOnFindings` is available for the final remediation phase. It is expected
to fail against the UG-01 baseline because later phases own the existing content
deficits. `build\scripts\validate_ug01_user_guide_audits.ps1` checks the
structural baseline, expected initial findings, Process/Image-menu parity,
source-drift detection, deleted paths, and unavailable source records. It runs
the full repository audit once; deterministic drift behavior is exercised by
the small focused Git fixture instead of a second full audit.

Historical phase-specific content assertions are retained but are not part of
the routine repository gate. Run them explicitly only while maintaining those
phase contracts:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  build\scripts\validate_ug01_user_guide_audits.ps1 -RunHistoricalContracts
```

Canonical screenshot/media state has a separate deterministic validator because
it joins the media manifest, triage decisions, the contract's per-phase
ownership counts, consumer-page slots/links, tracker/page media states and
capture-phase rows, provenance hashes, and the filesystem:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\validate_user_guide_media.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File build\scripts\validate_user_guide_media.ps1 -RunFixtures
```

`user-guide-media-contract.json` is the checked baseline/fixture for the original
120-row reconciliation, 59 retained canonical rows, the kept, deferred, and
future standalone counts of each capture phase, four merges, 57 not-needed rows,
seven deferred rows, and the current 24 future standalone captures after UG-66
through UG-68 validation. A change to a manifest row's disposition, phase, or
capture status updates these counts in the same change. The fixture run injects
eight deterministic contract failures and requires each one to be rejected for
the intended reason. UG-01 validation runs this suite before the
registry/menu/source-drift audit.

## What is reported

The audit reads current repository files rather than a hand-maintained process
list:

- `ProcessRegistryBuiltins.cpp` supplies registered built-in action IDs,
  categories, and launch macros.
- `AfterNightAppUiBuilder.h` supplies current English action labels, menu
  membership, and the Process- and Image-menu hierarchies.
- `mkdocs.yml` supplies navigated guide titles and destinations.
- The four frozen UG-00 templates supply required headings. Process pages also
  receive a canonical parameter-table shape check.
- `DOCUMENTATION_COMPLETION_TRACKER.md` supplies source-review dates,
  revisions, and source path records. For a full recorded commit, the audit uses
  Git to resolve paths in both the reviewed commit and current tree, then lists
  the stable union of committed and working-tree changes as paths changed since
  the reviewed revision. The same modified or untracked reviewed paths therefore
  produce identical audit text before and after commit, while deleted and
  renamed reviewed paths remain drift findings. Repository state is captured in
  batches and reused for every page sharing a reviewed revision.
- Exact generic-template signatures in `user-guide-audit-config.json` identify
  the known short-form boilerplate without treating ordinary shared terminology
  as a finding.

The config contains only mappings that cannot be derived uniquely: consolidated
immediate actions, UI-label/page-title exceptions, direct-wired Process- and
Image-menu actions, page-kind exceptions, and the boilerplate signatures. A new
action that cannot be derived or explicitly mapped is reported.

## Baseline policy

`user-guide-audit-baseline.json` records the SHA-256 hash of the structural,
date-free Markdown report plus a human-readable summary of the accepted UG-01
starting findings. The volatile source-drift count and per-page drift details
remain visible in generated reports but are excluded from this hash. Editing a
reviewed source therefore does not require baseline maintenance. A changed
structural hash is not automatically accepted: generate the report, review the
difference, then update the config or baseline only for an intentional audit
structure, navigation, tracker-metadata, heading, parity, or coverage change.

The UG-01 baseline is explicitly not clean. It records the two destinations
scheduled for UG-02 and UG-03, tracker rows with no review date or revision, missing frozen
template headings, generic boilerplate, and noncanonical parameter tables. It does not
authorize those findings to remain at the final UG-74/UG-75 audits.

## Limitations

- This is a source-text audit, not a C++ compiler or runtime menu inspection. It
  recognizes the current registration macros and generated UI-builder patterns;
  structural rewrites may require the smallest parser update allowed by the
  phase contract.
- Only the configured English MkDocs navigation is audited. Orphan pages,
  rendered links, Settings reachability, and broader component parity remain
  separate UG-74 categories. Canonical screenshot slots, links, ownership,
  counts, files, and provenance are covered by the focused media validator.
- Menu parity covers registered built-ins plus direct-wired actions reachable
  below the generated Process and Image menus. Runtime extension processes are
  outside this static inventory.
- Exact heading matches prove section presence, not factual completeness.
  Likewise, a canonical table proves its columns and at least one row, not that
  every visible control is represented.
- Generic-boilerplate detection uses exact signatures. It can miss rewritten
  filler and can flag intentional reuse; findings require human review.
- Source drift is authoritative only for full commit revisions and exact,
  repository-relative Git path records. Missing revisions, unavailable commits,
  and unresolved shorthand records are reported as unavailable or partial rather
  than guessed; those states are findings for `-FailOnFindings`.
