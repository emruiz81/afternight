# <Workflow name>

## Purpose, audience, prerequisites, and expected result

Define acquisition branches, user, knowledge/input quality, dependencies, and
final deliverables.

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

| Symptom | Cause | Fix | Resume or restart |
| --- | --- | --- | --- |
| <stage> | <cause> | <action> | <effect> |

## Related workflows and references

Explain preceding acquisition/preparation and following export/publishing.
Cite primary standards, upstream algorithms, process provenance, and exact
AfterNight pages used.
