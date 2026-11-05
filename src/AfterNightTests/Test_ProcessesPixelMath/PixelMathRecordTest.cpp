// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Every behaviour record of PixelMath, reproduced through the window as a person runs it: the record's scene opened in
/// a view made active, its expression written in the first tab and applied in place, and the view's image held to the
/// record. Where the record clamps a value to the unit interval and where it truncates a fraction to an integer code,
/// the result is instead what the expression says, narrowed once to the nearest value - each sample that departs from
/// the record is held to that answer, computed here from the scene, and to being one the clamp or the truncation
/// explains.

#include "Test_ProcessesPixelMath/PixelMathTestSupport.h"

#include <AfterNightEngine/Image/Provenance.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>

namespace
{

namespace fixtures = an::tests::fixtures;
namespace shared = an::tests::processes::shared;
using namespace an::tests::processes;
using namespace an::tests::processes::channels;
using namespace an::tests::processes::pixelmath;

/// One mode the corpus captured: its expression, what it computes of a sample's fraction of full scale, and which way
/// the engine runs it.
struct RecordedMode
{
    const char* mode = "";
    const char* expression = "";
    std::function<double(double)> computes;
    bool fused = false;
};

[[nodiscard]] const RecordedMode* modeNamed(std::string_view mode)
{
    static const RecordedMode kModes[] = {
        {.mode = "target-times-2", .expression = "$T * 2", .computes = [](double v) { return v * 2.0; }, .fused = true},
        {.mode = "square-root", .expression = "sqrt($T)", .computes = [](double v) { return std::sqrt(v); }},
        {.mode = "conditional",
         .expression = "iif($T > 0.4, $T - 0.1, 0)",
         .computes = [](double v) { return v > 0.4 ? v - 0.1 : 0.0; }},
    };
    for (const RecordedMode& recorded : kModes)
    {
        if (mode == recorded.mode)
        {
            return &recorded;
        }
    }
    return nullptr;
}

/// A sample of a scene as a fraction of full scale, and the code a fraction narrows to.
[[nodiscard]] double fractionOf(double stored, fixtures::FixtureSampleType type)
{
    return fixtures::isFloating(type) ? stored : stored / fixtures::highestOf(type);
}

[[nodiscard]] double narrowed(double fraction, fixtures::FixtureSampleType type)
{
    if (fixtures::isFloating(type))
    {
        return type == fixtures::FixtureSampleType::Float32 ? static_cast<double>(static_cast<float>(fraction))
                                                            : fraction;
    }
    const double highest = fixtures::highestOf(type);
    return std::nearbyint(std::clamp(fraction, 0.0, 1.0) * highest);
}

/// The last step of a document's lineage, the engine operation that wrote it.
[[nodiscard]] std::string lastLineage(const an::ui::ImageDocument& document)
{
    an::Result<an::consumer::AssetReadLease> lease = document.read();
    EXPECT_TRUE(lease.hasValue());
    if (!lease.hasValue())
    {
        return {};
    }
    const an::ImageMetadata& metadata =
        std::visit([](const auto& typed) -> const an::ImageMetadata& { return typed.metadata(); }, lease->image());
    const std::span<const an::ProvenanceEntry> entries = metadata.provenance().entries();
    return entries.empty() ? std::string() : entries.back().operation();
}

TEST(PixelMathRecordTest, EveryRecordIsReproducedThroughTheWindowWithTheClampAndTruncationExplained)
{
    const std::vector<fixtures::Expectation> records = recordsOf("pixel-math");
    ASSERT_EQ(records.size(), 5u) << "the corpus holds five PixelMath records";
    const shared::RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    for (const fixtures::Expectation& record : records)
    {
        SCOPED_TRACE(record.id);
        const RecordedMode* recorded = modeNamed(modeOf(record));
        ASSERT_NE(recorded, nullptr) << "no expression for the record's mode";
        const fixtures::FixtureImage* image = fixtures::fixtureNamed(record.inputName);
        ASSERT_NE(image, nullptr);
        std::string error;
        const std::optional<fixtures::FixtureSamples> input = fixtures::materializeFixture(*image, error);
        ASSERT_TRUE(input.has_value()) << error;
        ASSERT_TRUE(fixtures::expectationDescribes(record, *image, *input))
            << "the record was captured from another input than this scene";

        ViewHandle target = sceneView(context->engineSession(), record.inputName);
        ASSERT_NE(target, nullptr);
        activate(*context, *target);
        WindowHandle window = openWindow(target.get());
        setExpression(*window, an::ui::processes::PixelMathExpressionTab::RedOrAll,
                      QString::fromLatin1(recorded->expression));
        const std::size_t before = context->madeViews.size();
        ASSERT_TRUE(applyAndSettle(*context, *window));
        EXPECT_EQ(context->madeViews.size(), before) << "the result replaces the target";
        EXPECT_EQ(context->errorCount(), 0) << context->errorTexts.join(QStringLiteral("; ")).toStdString();
        const std::optional<fixtures::FixtureSamples> made = documentSamples(target->document());
        ASSERT_TRUE(made.has_value());
        EXPECT_EQ(lastLineage(target->document()),
                  recorded->fused ? "an::executePointwiseProgram" : "an::executePixelMath")
            << "the expression ran another way than its shape decides";
        window.reset();
        dropView(*context, target);

        ASSERT_EQ(made->width(), record.result.width());
        ASSERT_EQ(made->height(), record.result.height());
        ASSERT_EQ(made->channelCount(), record.result.channelCount());
        ASSERT_EQ(made->sampleType(), record.result.sampleType()) << "the representation is kept";
        const fixtures::FixtureSampleType type = made->sampleType();
        std::size_t clamped = 0u;
        std::size_t truncated = 0u;
        for (std::uint32_t channel = 0u; channel < made->channelCount(); ++channel)
        {
            for (std::uint32_t y = 0u; y < made->height(); ++y)
            {
                for (std::uint32_t x = 0u; x < made->width(); ++x)
                {
                    const double held = record.result.value(x, y, channel);
                    const double value = made->value(x, y, channel);
                    const double fraction = fractionOf(input->value(x, y, channel), type);
                    const double expected = narrowed(recorded->computes(fraction), type);
                    // The result is what the expression says of the scene, narrowed once.
                    EXPECT_NEAR(value, expected, fixtures::isFloating(type) ? 1e-6 * (1.0 + std::fabs(expected)) : 0.0)
                        << "at " << x << ", " << y << ", channel " << channel;
                    const double tolerance = record.absoluteTolerance + record.relativeTolerance * std::fabs(held);
                    if (std::fabs(value - held) <= tolerance)
                    {
                        continue;
                    }
                    if (fixtures::isFloating(type))
                    {
                        // The record holds every result to the unit interval.
                        const bool clampExplains = (held == 1.0 && value > 1.0) || (held == 0.0 && value < 0.0);
                        EXPECT_TRUE(clampExplains) << "at " << x << ", " << y << ", channel " << channel << ": "
                                                   << value << " where the record holds " << held;
                        clamped += clampExplains ? 1u : 0u;
                    }
                    else
                    {
                        // The record truncates a fraction to the code below it; this narrows to the nearest.
                        EXPECT_LE(std::fabs(value - held), 1.0) << "at " << x << ", " << y << ", channel " << channel;
                        truncated += 1u;
                    }
                }
            }
        }
        std::cout << record.id << ": " << clamped << " samples beyond the record's clamp, " << truncated
                  << " codes the record truncated\n";
    }
}

} // namespace
