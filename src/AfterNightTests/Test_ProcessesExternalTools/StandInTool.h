// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A controlled substitute for the StarNet2 and DeepSNR programs: this test executable, started by the product with a
/// tool's own arguments while `AFTERNIGHT_STANDIN_TOOL` names the product it stands in for. It answers the product's
/// probes - `--version`, and `--machine-info` when told to report machine-readable progress - reads the image it is
/// handed through the engine's codecs, and writes the answers the arguments ask for in the handed representation: the
/// image as it was, the mask as its complement, the star layer as its half; each a known function of the input, so a
/// case can hold what the product brings back to what the arithmetic says. Environment variables choose a behaviour:
/// a program that exits with a code, crashes, writes nothing, writes the wrong size or takes its time printing
/// progress, so every path of the hand-off is reached without the real programs.
#pragma once

namespace an::tests::processes::externaltools
{

/// Names the product the executable stands in for: "starnet2" or "deepsnr". Set by a case before the product starts
/// the program, which inherits it.
inline constexpr const char* kStandInToolVariable = "AFTERNIGHT_STANDIN_TOOL";

/// The stand-in's behaviour: "identity" when unset, "half" for an image answered at half its codes, "machine" for an
/// identity that reports machine-readable progress, "exit-code", "crash", "no-output", "wrong-size", "slow" or
/// "machine-slow", which is both.
inline constexpr const char* kStandInBehaviourVariable = "AFTERNIGHT_STANDIN_TOOL_BEHAVIOUR";

/// A file the stand-in appends the arguments of each run over an image to, one per line, when set; the product's
/// probes of the program are not recorded.
inline constexpr const char* kStandInRecordVariable = "AFTERNIGHT_STANDIN_TOOL_RECORD";

/// A path the stand-in copies the image it was handed to, when set, so a case can read what the tool saw.
inline constexpr const char* kStandInKeepInputVariable = "AFTERNIGHT_STANDIN_TOOL_KEEP_INPUT";

/// How long the "slow" behaviour takes, in milliseconds; 6000 when unset.
inline constexpr const char* kStandInSlowMsVariable = "AFTERNIGHT_STANDIN_TOOL_SLOW_MS";

/// Runs the stand-in over the program's arguments and returns the program's exit code.
///
/// @param argc the argument count, the program's name first.
/// @param argv the arguments.
/// @return the exit code: zero for a run that wrote its answers.
[[nodiscard]] int runStandInTool(int argc, char** argv);

} // namespace an::tests::processes::externaltools
