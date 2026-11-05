// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// How the external tools' progress is read from what they print: the percentages, fractions and finished counts their
/// text output carries, a token split across two reads, the machine-readable progress lines and their stream, and a
/// tool's percentage mapped into the share of a run it reports.

#include "processes/CliProgressParser.h"

#include <QByteArray>
#include <QString>
#include <QStringView>

#include <gtest/gtest.h>

#include <optional>

namespace
{

[[nodiscard]] std::optional<int> parseProgress(const QString& text)
{
    return an::ui::processes::cli_progress::extractLatestProgressPercent(QStringView(text));
}

} // namespace

// A percentage is read whether it is spelled with a sign or as a word.
TEST(CliProgressParserTest, ExtractsPercentFormats)
{
    ASSERT_TRUE(parseProgress(QStringLiteral("12% finished")).has_value());
    EXPECT_EQ(*parseProgress(QStringLiteral("12% finished")), 12);

    ASSERT_TRUE(parseProgress(QStringLiteral("DeepSNR: 87 percent")).has_value());
    EXPECT_EQ(*parseProgress(QStringLiteral("DeepSNR: 87 percent")), 87);
}

// A decimal percentage is rounded to the nearest whole one, and the latest of several lines wins.
TEST(CliProgressParserTest, ExtractsDecimalPercentFormats)
{
    ASSERT_TRUE(parseProgress(QStringLiteral("StarNet2 10.0% finished")).has_value());
    EXPECT_EQ(*parseProgress(QStringLiteral("StarNet2 10.0% finished")), 10);

    ASSERT_TRUE(parseProgress(QStringLiteral("DeepSNR 47.6% finished")).has_value());
    EXPECT_EQ(*parseProgress(QStringLiteral("DeepSNR 47.6% finished")), 48);

    ASSERT_TRUE(parseProgress(QStringLiteral("StarNet2 9.9%\nStarNet2 10.0%")).has_value());
    EXPECT_EQ(*parseProgress(QStringLiteral("StarNet2 9.9%\nStarNet2 10.0%")), 10);
}

// A count of a total is read as the share done, never as the total.
TEST(CliProgressParserTest, ExtractsFractionFormatsWithoutJumpingToDenominator)
{
    ASSERT_TRUE(parseProgress(QStringLiteral("tile 12 of 100 finished")).has_value());
    EXPECT_EQ(*parseProgress(QStringLiteral("tile 12 of 100 finished")), 12);

    ASSERT_TRUE(parseProgress(QStringLiteral("patch 3/4 processed")).has_value());
    EXPECT_EQ(*parseProgress(QStringLiteral("patch 3/4 processed")), 75);
}

// A finished count is read as progress, and a version line is not progress at all.
TEST(CliProgressParserTest, ExtractsFinishedValueAndIgnoresNonProgressText)
{
    ASSERT_TRUE(parseProgress(QStringLiteral("42 finished")).has_value());
    EXPECT_EQ(*parseProgress(QStringLiteral("42 finished")), 42);

    ASSERT_TRUE(parseProgress(QStringLiteral("10%\n42 finished")).has_value());
    EXPECT_EQ(*parseProgress(QStringLiteral("10%\n42 finished")), 42);

    EXPECT_FALSE(parseProgress(QStringLiteral("DeepSNR v1.2.2")).has_value());
}

// A token split across two reads is read once the output that completes it arrives.
TEST(CliProgressParserTest, AccumulatedOutputRecoversSplitProgressTokens)
{
    QString accumulated;
    accumulated += QStringLiteral("1");
    EXPECT_FALSE(parseProgress(accumulated).has_value());

    accumulated += QStringLiteral("0% finished");
    ASSERT_TRUE(parseProgress(accumulated).has_value());
    EXPECT_EQ(*parseProgress(accumulated), 10);
}

// A tool's percentage is mapped into the share of the run its hand-off reports in.
TEST(CliProgressParserTest, MapsCliProgressIntoExecutionRange)
{
    EXPECT_EQ(an::ui::processes::cli_progress::mapPercentToRange(0, 10, 90), 10);
    EXPECT_EQ(an::ui::processes::cli_progress::mapPercentToRange(50, 10, 90), 50);
    EXPECT_EQ(an::ui::processes::cli_progress::mapPercentToRange(100, 10, 90), 90);
}

// A machine progress line of the known schema is read, its percentage rounded and held to a hundred; another schema is
// not.
TEST(CliProgressParserTest, ParsesMachineProgressJsonLines)
{
    const auto progress = an::ui::processes::cli_progress::parseMachineProgressLine(QStringLiteral(
        R"json({"schema":"starnetastro.cli.progress.v1","event":"progress","stage":"inference","current":1,"total":4,"percent":25.6})json"));
    ASSERT_TRUE(progress.has_value());
    EXPECT_EQ(progress->event, QStringLiteral("progress"));
    EXPECT_EQ(progress->stage, QStringLiteral("inference"));
    ASSERT_TRUE(progress->percent.has_value());
    EXPECT_EQ(*progress->percent, 26);

    const auto error = an::ui::processes::cli_progress::parseMachineProgressLine(QStringLiteral(
        R"json({"schema":"starnetastro.cli.progress.v1","event":"error","stage":"processing","ok":false,"message":"bad input"})json"));
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->event, QStringLiteral("error"));
    EXPECT_EQ(error->message, QStringLiteral("bad input"));
    EXPECT_FALSE(error->percent.has_value());

    EXPECT_FALSE(an::ui::processes::cli_progress::parseMachineProgressLine(
                     QStringLiteral(R"json({"schema":"other.progress.v1","event":"progress","percent":50})json"))
                     .has_value());

    const auto clamped = an::ui::processes::cli_progress::parseMachineProgressLine(
        QStringLiteral(R"json({"schema":"starnetastro.cli.progress.v1","event":"progress","percent":250.4})json"));
    ASSERT_TRUE(clamped.has_value());
    ASSERT_TRUE(clamped->percent.has_value());
    EXPECT_EQ(*clamped->percent, 100);
}

// The machine progress stream joins a line split across reads, keeps a plain line as a diagnostic and flushes the last
// line when it ends.
TEST(CliProgressParserTest, MachineProgressStreamHandlesSplitJsonAndDiagnostics)
{
    an::ui::processes::cli_progress::MachineProgressStreamParser parser;

    const auto first = parser.append(
        QByteArrayLiteral(R"json({"schema":"starnetastro.cli.progress.v1","event":"progress","percent":7)json"));
    EXPECT_TRUE(first.events.isEmpty());
    EXPECT_TRUE(first.diagnostics.isEmpty());

    const auto second = parser.append(QByteArrayLiteral(".5}\r\nONNX Runtime warning\r\n"));
    ASSERT_EQ(second.events.size(), 1);
    ASSERT_TRUE(second.events.front().percent.has_value());
    EXPECT_EQ(*second.events.front().percent, 8);
    ASSERT_EQ(second.diagnostics.size(), 1);
    EXPECT_EQ(second.diagnostics.front(), QStringLiteral("ONNX Runtime warning"));

    const auto pending = parser.append(
        QByteArrayLiteral(R"json({"schema":"starnetastro.cli.progress.v1","event":"finish","percent":100.0})json"));
    EXPECT_TRUE(pending.events.isEmpty());

    const auto finished = parser.finish();
    ASSERT_EQ(finished.events.size(), 1);
    EXPECT_EQ(finished.events.front().event, QStringLiteral("finish"));
    ASSERT_TRUE(finished.events.front().percent.has_value());
    EXPECT_EQ(*finished.events.front().percent, 100);
}
