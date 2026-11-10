---
kind: process
stage: stretch
menu:
  - Process > Stretching > Soft Stretch
  - Image > Soft Stretch
summary: A page as the guide's pages were before the rewrite, with a wide table and no figure.
data_in: linear
data_out: nonlinear
---
# Soft Stretch

A page that states only what every process page must: its stage, its menus and its summary. It draws its position and
no "At a glance" card.

## Complete parameter and action reference

| UI label/action | Type and unit | Runtime default | Persisted-default exception | Valid values | Effect | Dependencies | Trade-off | Scope | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **Amount** | Decimal, unitless | `0.25` | None | 0 to 1 in steps of 0.01 | Higher bends the curve more. | Enabled with an image. | A strong bend raises the noise with the signal. | Preview and Apply. | The window's lifetime. |
| **Protect highlights** | Check box | On | None | On or off | Holds the brightest samples below white. | None. | Star cores keep their color and lose some contrast. | Preview and Apply. | The window's lifetime. |

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| Nothing changes | Amount is 0 | Raise it |
