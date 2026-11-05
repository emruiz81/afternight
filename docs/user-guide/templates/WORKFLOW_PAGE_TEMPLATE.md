# <Workflow name>

<!--
Frozen template: UG-00, 2026-07-10. A workflow is not review-ready without
representative project-owned data and verified checkpoints; block when a
mandatory prerequisite is unavailable.
-->

<!--
A page renders no authoring evidence. Record its owner, source-review date, reviewed revision, dirty reviewed
sources, source files and tests, UI reachability, parameter, algorithm and example coverage, content state,
technical and user review, media state and last strict build in its row of
docs/user-guide/DOCUMENTATION_COMPLETION_TRACKER.md.
-->

## Purpose, audience, prerequisites, and expected result

Define acquisition branches, user, knowledge/input quality, dependencies, and
final deliverables.

## Verification data and evidence mode

| Item | Required record |
| --- | --- |
| Project-owned sample set | <identifier, provenance, permission, availability> |
| Branches covered | <OSC/mono/RGB/LRGB/SHO/etc.> |
| External tools/models | <versions and readiness> |
| Catalogs/network | <packs/indexes and offline/online mode> |
| Platform/application build | <platform, revision, configuration> |
| Evidence artifacts | <logs, reports, checksums, captures, output paths> |
| Unverified claims excluded | <record> |

## Inputs and outputs

Document image/sequence states, metadata, frame sets, artifacts, naming,
precision, history, disk use, and source modification.

## Workflow map and decision points

Use a compact flow/decision table only when it clarifies branches and handoffs.

## Step-by-step procedure

Every step records the exact path, input/prerequisites, complete starting
settings or authoritative link, observable checkpoint and acceptance criteria,
output/metadata, decision/handoff, and failure recovery.

## Parameter handoff matrix

| From step | Output/checkpoint | Decision criterion | To step | Parameters/state carried forward | Recovery |
| --- | --- | --- | --- | --- | --- |
| <step> | <artifact> | <verified criterion> | <step> | <values/metadata> | <action> |

## Algorithms and why the order matters

Explain transforms/estimators, linear/nonlinear state, stage dependencies,
channels/color, masks, clipping, metadata, and resampling. Define useful
equations/symbols and link complete process pages.

## Performance and resource planning

Document cost drivers, memory/disk/network/catalogs, long stages,
cancellation/resume, safe prototypes, and intermediate verification.

## Verified worked run

- Sample-set provenance: <project-owned identifier>.
- Application revision/platform and settings artifact: <record>.
- Checkpoints/final 100% or statistics/history/report/export verification:
  <verified observations only>.
- Known limitations: <record>.

## Warnings and limitations

Cover destructive actions, unsupported branches, dependencies, privacy/network,
clipping/color/metadata, data limits, and non-generalized claims.

## Troubleshooting and recovery

| Stage/symptom | Verified cause or failed checkpoint | Recovery | Resume/restart consequence | Evidence |
| --- | --- | --- | --- | --- |
| <stage> | <cause> | <action> | <effect> | <source/run> |

## Related workflows and references

Explain preceding acquisition/preparation and following export/publishing.
Cite primary standards, upstream algorithms, process provenance, and exact
AfterNight pages used.

<!-- Media authoring: use only the canonical manifest. For missing retained media,
place a compact MEDIA_SLOT with its stable Media ID and no capture instructions. -->
