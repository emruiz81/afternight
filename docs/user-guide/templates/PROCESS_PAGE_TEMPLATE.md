# <Process name>

## Purpose and expected result

State the purpose, audience, prerequisites, and observable result. Distinguish
implementation facts from guidance.

## At a glance

| Item | Current behavior |
| --- | --- |
| Menu path | <exact path> |
| Enabled when | <target and application-state conditions> |
| Inputs | <supported image, sequence, metadata, and auxiliary inputs> |
| Outputs | <image/file/map/report outputs and whether the source changes> |
| Recommended image state | <linear/nonlinear and exceptions> |
| Preview support | <RT preview/full-image preview/apply behavior> |
| Sequence support | <supported/unsupported and scope> |
| Mask support | <supported/unsupported, geometry/channel and blend rules> |
| External dependencies | <tools, models, catalogs, network, or None> |

## Before you start

Document data preparation, metadata, target selection, external readiness,
destructive effects, and backup expectations.

## Procedure

Use task-oriented steps with an observable checkpoint after each meaningful
action, including preview/apply success and output location.

## Complete parameter and action reference

One row is required for every visible control and meaningful action, using the
exact current UI label.

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values, step, or constraints | Effect of increasing, decreasing, or selecting | Dependencies and enable/visibility rules | Quality, artifact, runtime, memory, or disk trade-off | Scope: preview/apply, channel, mask, still/sequence | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| <label> | <type/unit> | <default> | <none or exception> | <range/enums/files> | <effect> | <dependencies> | <trade-off> | <scope> | <key/lifetime/reset> |

## How the algorithm works

### Conceptual pipeline

1. <validated input and preprocessing>
2. <core estimator, transform, kernel, solver, or external engine>
3. <channel/color and mask handling>
4. <boundary, invalid-pixel, clipping, and output conversion behavior>

### Model and equations

Define every symbol and pair useful equations with plain language. State
estimators, kernels, transforms, resampling, numerical precision,
convergence/stopping behavior, and external implementation/provenance.

### Boundary and failure behavior

State validation, negative/NaN/invalid-pixel handling, edge policy,
cancellation, partial-output cleanup, and errors.

## Parameter interactions and starting points

| Combination | Conservative starting point | Too weak looks like | Too strong looks like | Adjustment order |
| --- | --- | --- | --- | --- |
| <controls> | <source/test-backed guidance> | <symptom> | <symptom> | <sequence> |

Do not invent quantitative or visual results.

## Inputs, outputs, and metadata

Document input/output counts, compatibility, naming, pixel type/precision,
source modification, metadata/history, auxiliary files, overwrite, and
temporary files.

## Performance

Identify cost drivers, scaling, memory/disk/network, external processes, and
safe prototypes.

## Worked example

- Input condition/provenance and settings: <record>.
- Expected checkpoint: <verified observation only>.
- Inspect at 100%/statistics/history: <record>.
- Limitations: <especially when not executed>.

## Warnings and limitations

Cover destructive actions, linear/nonlinear order, masks, clipping, color,
precision, resources, dependencies, unsupported states, and known limits.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| <symptom> | <cause> | <action> |

## Related workflow steps

Explain and link preceding/following steps and their handoff state.

## Credits

Credit what the page owes: the upstream work a derived process is computed with, its authors and its licence, and
the primary papers, standards and upstream documentation behind the explanation, each with a link. A derived
process requires the section; leave it out when there is nothing to credit.
