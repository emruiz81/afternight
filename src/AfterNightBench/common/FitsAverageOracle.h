// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - the exact average of a list of FITS frames, held against a stacked
// plane.
//
// A stage row that stacks frames from files is scored against the files, read by a reader of its
// own: the engine's codec does not decode them for the check, so a decoder defect could not pass as
// agreement. The reader takes the primary image of an uncompressed FITS file with 8, 16
// or 32-bit integer samples at the unit scale, which is what a camera writes, or 32 or 64-bit
// floating samples with no scale and no zero point, which is what an aligned light is, and refuses
// anything else by name rather than guessing at it.
//
// The average is the one the engine computes for a stack with no normalization: every exact zero,
// of either sign, is no data. The remaining integer codes of each element are summed exactly as
// integers and the remaining floating samples in double in frame order, and the mean is their
// quotient in double. An element no frame reaches is zero. A floating sample that is not finite has
// no average, so the check refuses its frame.
//
// A residual is measured against the average magnitude of the samples an element's mean is made of,
// which is the scale any floating sum of them is accurate to; for samples of one sign - every code a
// camera writes - that is the mean itself. Where samples of both signs nearly cancel, the mean alone
// would turn an error in the last place of the terms into an arbitrarily large relative one.
//
// An engine that writes its average clamped to a range - a floating result inside [0, 1], say - is
// held to the clamped average, and the check counts the elements the range
// decided. Nothing else about the average changes. The check walks every element in fixed chunks on every
// core outside the timed region; its maxima are the serial walk's and its sum of squares is summed in
// chunk order, whatever the machine.
#pragma once

#include "Results.h"
#include "Scenario.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace an::bench
{

/// The version of the check's arithmetic. A change to what it computes moves it, so a record made
/// under another is not read as this one's.
inline constexpr double kFitsAverageOracleVersion = 2.0;

/// The range an engine writes its average inside, in the frames' units: a mean below `lowest` is
/// written as `lowest` and one above `highest` as `highest`.
struct FitsAverageResultRange
{
    double lowest = 0.0; ///< The least value the engine writes.
    double highest = 0.0; ///< The greatest value the engine writes.
};

/// What one check of a stacked plane against the exact average of its frames found.
struct FitsAverageCheck
{
    /// Whether every frame was read and every one matches the plane's size.
    bool valid = false;

    /// Why the check is not valid, naming the frame; empty when it is.
    std::string problem;

    /// Frames read.
    std::uint64_t frames = 0u;

    /// Elements compared.
    std::uint64_t elements = 0u;

    /// Elements every frame holds a zero at, whose expected value is zero.
    std::uint64_t elementsWithoutSamples = 0u;

    /// Elements whose mean lies outside the stated result range, so the range decided their
    /// expected value; zero when no range was stated.
    std::uint64_t elementsClamped = 0u;

    /// The largest residual relative to the average magnitude of the samples averaged, which is the
    /// exact mean's own magnitude for samples of one sign; infinite where a value is not finite or an
    /// element without samples is not zero.
    double maximumRelative = 0.0;

    /// The largest absolute residual, in the frames' code units.
    double maximumAbsolute = 0.0;

    /// The root mean square residual, in the frames' code units.
    double rmsAbsolute = 0.0;
};

/// Holds a stacked plane against the exact average of the non-zero samples of `paths`.
///
/// @param paths the frames, as UTF-8 paths, in stack order.
/// @param result the stacked plane, one Float32 value per element in row order.
/// @param resultScale what one unit of the result is in code units: one for a result in codes, the
///        full scale for one divided by it.
/// @param range the range the engine writes its average inside, or nothing when it writes the
///        average as it is.
/// @return the check.
[[nodiscard]] FitsAverageCheck checkFitsAverage(std::span<const std::string> paths, std::span<const float> result,
                                                double resultScale,
                                                std::optional<FitsAverageResultRange> range = std::nullopt);

/// Writes a check into a result's counters under the `stageOracle` names, and its largest relative
/// residual as the correctness metric when it is valid.
///
/// @param check the check.
/// @param out the result.
void recordFitsAverageCheck(const FitsAverageCheck& check, ResultRecord& out);

/// Whether a result carries this check, complete for the row's frames and geometry and inside the
/// row's tolerance.
///
/// @param scenario the stage row.
/// @param frames the frames the row stacks.
/// @param record the result.
/// @return true only when the check ran under this version, over every element of every frame, and
///         passed.
[[nodiscard]] bool fitsAverageEvidenceValid(const ScenarioSpec& scenario, std::uint64_t frames,
                                            const ResultRecord& record);

} // namespace an::bench
