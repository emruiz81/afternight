# <Application component>

## Purpose, audience, prerequisites, and expected result

State the component's job, intended user, prerequisites, and observable result.

## Reachability and availability

| Item | Current behavior |
| --- | --- |
| Path | <exact menu, toolbar, dashboard, context-menu, or dock path> |
| Enabled/visible when | <application, target, selection, and readiness state> |
| Supported inputs/state | <images, sequences, metadata, selections, masks> |
| Outputs/side effects | <views, settings, files, metadata, history> |
| Persistence | <settings keys/session/project behavior/reset> |
| External dependencies | <tools/catalogs/network/files or None> |

## Before you start

List data preparation, selection, permissions, disk/network needs, and
irreversible or privacy-sensitive actions.

## Procedure

Use task-oriented steps with observable checkpoints. Include reachable empty,
disabled, progress, cancellation, success, and failure states.

## Complete control and action reference

| UI label/action | Type and unit | Default | Valid values/constraints | Effect | Enabled, hidden, or ignored rules | Trade-off or side effect | Scope | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| <exact label> | <type/unit> | <runtime default> | <range/enums/file types> | <behavior> | <dependencies> | <quality/runtime/disk/privacy> | <target/session/project> | <key/lifetime/reset> |

## Behavior and state model

Explain data flow, state transitions, image-versus-display distinctions,
selection, channels/color, sequences, masks, metadata/history, refresh timing,
and user-visible algorithms. Define useful equations/symbols in plain language.

## Inputs, outputs, and metadata

Record inputs, outputs/naming/pixel type, metadata/history, source modification,
temporary data, overwrite, and recovery.

## Performance and responsiveness

Document CPU/GPU, memory, disk, network, catalog, responsiveness, and safe ways
to reduce load.

## Worked example

- Starting state, actions, settings, checkpoints, result, and limitations:
  <verified record>.

## Warnings and limitations

Cover destructive operations, unsupported states, clipping/color/metadata,
privacy/network, dependencies, and known limits.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| <symptom> | <cause> | <action> |

## Related workflow steps

Link preceding/following tasks and explain the handoff artifact/state.

## Credits

Credit what the page owes: the upstream work a derived process is computed with, its authors and its licence, and
the primary papers, standards and upstream documentation behind the explanation, each with a link. A derived
process requires the section; leave it out when there is nothing to credit.
