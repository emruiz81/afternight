// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The words the PixelMath editor highlights, taken from the engine's language rather than kept beside it: every
/// function name the engine's function table answers to, and every operator its syntax defines, spelled as an
/// expression writes it.
///
/// The engine states its functions as a table of names and its operators as closed enumerations; the spelling of an
/// operator is the editor's, decided here once with a case for every enumerator, so an operator the engine adds is a
/// compile-time question rather than a word the editor silently fails to colour.
#pragma once

#include "AfterNightControlsExport.h"

#include <AfterNightEngine/Algorithms/PixelMath/PixelMathSyntax.h>

#include <QString>
#include <QStringList>

namespace an::ui
{

/// What the editor highlights, each list in ascending order and without repeats.
struct PixelMathVocabulary
{
    /// The functions: every name of the engine's function and image-statistic tables but the special ones.
    QStringList functions;

    /// The special functions - the conditional, the rectangle and ellipse tests and the sample of a bound image - which
    /// the editor colours apart.
    QStringList specialFunctions;

    /// The operators - prefix, infix and assignment - and the statement separator and the two comment markers.
    QStringList operators;
};

/// The editor's vocabulary, read from the engine's tables.
///
/// @return the vocabulary.
[[nodiscard]] AFTERNIGHTCONTROLS_API PixelMathVocabulary pixelMathVocabulary();

/// How a prefix operator is written.
///
/// @param op the operator.
/// @return its spelling, as `-` or `!`.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString pixelMathOperatorSpelling(an::PixelMathUnaryOperator op);

/// How an infix operator is written.
///
/// @param op the operator.
/// @return its spelling, as `--` or `<=`.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString pixelMathOperatorSpelling(an::PixelMathBinaryOperator op);

/// How an assignment operator is written.
///
/// @param op the operator.
/// @return its spelling, as `=` or `^=`.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString pixelMathOperatorSpelling(an::PixelMathAssignmentOperator op);

} // namespace an::ui
