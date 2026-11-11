// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The figures a comparison of two runs sets side by side, taken from a report model.
///
/// A run is compared with an earlier one figure by figure, and the two sides of a figure must be the same figure:
/// the lights a stack combined, counted one way for this run and another for the earlier one, would show a change
/// where there is none. So both sides are read by the functions here - the model's builder reads an earlier run's
/// stored report through them, and the document reads this run's model through them - and neither computes a figure
/// of its own.

#include "processes/ImageIntegration/reporting/ReportModel.h"

namespace ImageIntegration::Reporting
{
/// The figures of a master a comparison states, as the master's own model states them: the lights its stack
/// combined of the lights its group took, their exposure, the master's noise and signal to noise, the median star
/// width of the combined lights, the stack's effective frames and the share of samples it rejected. A figure the
/// model does not state is not stated here. The run, the pictures and the shape are left as a caller sets them.
///
/// @param master a master of any run's model.
/// @return its figures, in a baseline's fields.
[[nodiscard]] ReportBaseline masterFigures(const ReportMaster& master);

/// A run's totals, as its model states them: the lights it took and those its masters combine, their exposures,
/// the masters that are there with the sum of their sizes, and the mean over them of the signal to noise and of the
/// overlap retained.
///
/// @param model a run's model, its run and its masters filled.
/// @return the totals.
[[nodiscard]] ReportRunTotals runTotals(const ReportModel& model);
} // namespace ImageIntegration::Reporting
