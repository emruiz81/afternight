// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Every behaviour record of the channel and conversion actions and of the image menu's grey, colour, duplicate and
/// negative operations, reproduced through the action itself: the record's scene opened in a view made active, the
/// action run as the menu runs it - a registered action through the registry, an image operation through the image
/// operations coordinator, a sequence-frame record over a one-frame sequence - and what it publishes held to the
/// record, by the record's own bound or by the bound an accepted difference states.

#include "Test_ProcessesChannels/ChannelsTestSupport.h"

#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
using namespace an::tests::processes;
using namespace an::tests::processes::channels;

/// How one corpus file's action is run.
enum class Route : std::uint8_t
{
    /// A registered channel or conversion action, through the registry.
    Registered,

    /// An image operation of the coordinator, in place.
    InPlace,

    /// Duplicate, which opens a new view.
    Duplicate
};

/// One corpus file and the action that reproduces it.
struct RecordedAction
{
    /// The corpus file, without its extension.
    const char* records = "";

    /// The registered action, for `Route::Registered`.
    const char* action = "";

    Route route = Route::Registered;

    /// The coordinator's operation, for the other routes.
    std::function<void(const an::ui::ImageOperationsCoordinator&)> operation;
};

const std::vector<RecordedAction>& recordedActions()
{
    static const std::vector<RecordedAction> actions = [] {
        std::vector<RecordedAction> table;
        const std::vector<std::pair<const char*, const char*>> registered{
            {"extract-red-channel", "actionExtract_Red_Channel"},
            {"extract-green-channel", "actionExtract_Green_Channel"},
            {"extract-blue-channel", "actionExtract_Blue_Channel"},
            {"extract-rgb-channels", "actionExtract_RGB_Channels"},
            {"extract-cie-l", "actionExtract_CIE_L"},
            {"extract-cie-a", "actionExtract_CIE_a"},
            {"extract-cie-b", "actionExtract_CIE_b"},
            {"extract-cie-ab", "actionExtract_CIE_a_b"},
            {"extract-cie-lab", "actionExtract_CIE_L_a_b"},
            {"extract-cie-c", "actionExtract_CIE_c"},
            {"extract-cie-h", "actionExtract_CIE_h"},
            {"extract-cie-lch", "actionExtract_CIE_L_c_h"},
            {"extract-cie-x", "actionExtract_CIE_X"},
            {"extract-cie-y", "actionExtract_CIE_Y"},
            {"extract-cie-z", "actionExtract_CIE_Z"},
            {"extract-cie-xyz", "actionExtract_CIE_X_Y_Z"},
            {"extract-hsl-hue", "actionExtract_HSL_Hue"},
            {"extract-hsl-saturation", "actionExtract_HSL_Saturation"},
            {"extract-hsl-lightness", "actionExtract_HSL_Lightness"},
            {"extract-hsl", "actionExtract_HSL"},
            {"extract-hsv-hue", "actionExtract_HSV_Hue"},
            {"extract-hsv-saturation", "actionExtract_HSV_Saturation"},
            {"extract-hsv-value", "actionExtract_HSV_Value"},
            {"extract-hsv", "actionExtract_HSV"},
            {"extract-hsi-hue", "actionExtract_HSI_Hue"},
            {"extract-hsi-saturation", "actionExtract_HSI_Saturation"},
            {"extract-hsi-intensity", "actionExtract_HSI_Intensity"},
            {"extract-hsi", "actionExtract_HSI"},
            {"extract-ycbcr-luma", "actionExtract_YCbCr_Luminance"},
            {"extract-ycbcr-blue-chroma", "actionExtract_YCbCr_Blue_Chrominance"},
            {"extract-ycbcr-red-chroma", "actionExtract_YCbCr_Red_Chrominance"},
            {"extract-ycbcr", "actionExtract_YCbCr"},
            {"convert-to-8-bit", "actionConvert_to_8_bit"},
            {"convert-to-signed-16-bit", "actionConvert_to_signed_16_bit"},
            {"convert-to-unsigned-16-bit", "actionConvert_to_unsigned_16_bit"},
            {"convert-to-signed-32-bit", "actionConvert_to_signed_32_bit"},
            {"convert-to-unsigned-32-bit", "actionConvert_to_unsigned_32_bit"},
            {"convert-to-32-bit-float", "actionConvert_to_32_bit_float"},
            {"convert-to-64-bit-float", "actionConvert_to_64_bit_float"},
        };
        for (const auto& [records, action] : registered)
        {
            table.push_back(RecordedAction{.records = records, .action = action});
        }
        table.push_back(RecordedAction{.records = "convert-to-grayscale",
                                       .route = Route::InPlace,
                                       .operation = [](const an::ui::ImageOperationsCoordinator& operations) {
                                           operations.convertToGrayscale();
                                       }});
        table.push_back(RecordedAction{
            .records = "convert-to-rgb",
            .route = Route::InPlace,
            .operation = [](const an::ui::ImageOperationsCoordinator& operations) { operations.convertToRGB(); }});
        table.push_back(RecordedAction{.records = "negative-transformation",
                                       .route = Route::InPlace,
                                       .operation = [](const an::ui::ImageOperationsCoordinator& operations) {
                                           operations.negativeTransformation();
                                       }});
        table.push_back(RecordedAction{
            .records = "duplicate",
            .route = Route::Duplicate,
            .operation = [](const an::ui::ImageOperationsCoordinator& operations) { operations.duplicateImage(); }});
        return table;
    }();
    return actions;
}

/// Whether a sample of a record's result is one an accepted difference explains a larger departure at: the record's
/// input, the position and channel, what the record holds and what the action made, in the record's units.
using Explanation = std::function<bool(const fixtures::FixtureSamples& input, std::uint32_t x, std::uint32_t y,
                                       std::uint32_t channel, double recorded, double made)>;

/// A difference from the record that a case accepts: the records it applies to, how far a result may be from the
/// record, whether the action makes another representation than the record holds, and why.
struct AcceptedDifference
{
    /// The start of the ids of the records it applies to.
    std::string records;

    /// The input it applies to, or empty for every input.
    std::string input;

    /// Whether the action makes a floating image where the record holds the input's integer codes: the result is
    /// compared as the record's codes.
    bool representation = false;

    /// The largest absolute difference allowed against the record, in the record's units.
    double bound = 0.0;

    /// Why.
    std::string reason;

    /// The samples at which a result may lie further than the bound, and why; none when no sample may.
    Explanation explains;

    /// The departure an explained sample shows, for a reader.
    std::string explained;
};

constexpr const char* kComponent = "a colour component of a sixteen-bit image is a Float32 plane, where the record "
                                   "holds it in the image's own codes, truncated";
constexpr const char* kLumaChroma = "the record's luma-chroma coefficients are rounded to three decimals, and the "
                                    "engine derives them from the BT.601 weights";
constexpr const char* kThirtyTwo = "the record scales a thirty-two-bit code in single precision, and offsets a signed "
                                   "one two codes from its own convention; the engine computes in double";

/// The records' 32-bit conversion defect: a single-precision scale of a full-scale sample is 2^32, which no 32-bit
/// integer holds, and the record holds the lowest code there.
[[nodiscard]] Explanation atOrAboveFullScale()
{
    return [](const fixtures::FixtureSamples& input, std::uint32_t x, std::uint32_t y, std::uint32_t channel, double,
              double) {
        const bool floating = input.sampleType() == fixtures::FixtureSampleType::Float32 ||
                              input.sampleType() == fixtures::FixtureSampleType::Float64;
        return floating && input.value(x, y, channel) >= 1.0;
    };
}

/// The record's signed negative defect: it takes a signed sample from 32767, half the range from the complement of its
/// own fractions, wrapping below zero; every sample is 32,768 codes away modulo the range.
[[nodiscard]] Explanation halfTheRangeAway()
{
    return [](const fixtures::FixtureSamples&, std::uint32_t, std::uint32_t, std::uint32_t, double recorded,
              double made) { return std::fmod(std::abs(made - recorded), 65536.0) == 32768.0; };
}

const std::vector<AcceptedDifference>& acceptedDifferences()
{
    static const std::vector<AcceptedDifference> differences = [] {
        std::vector<AcceptedDifference> table;
        for (const char* records : {"extract-ycbcr-blue-chroma.", "extract-ycbcr-red-chroma.", "extract-ycbcr."})
        {
            table.push_back({.records = records, .input = "scene-rgb-float32", .bound = 2.5e-4, .reason = kLumaChroma});
            table.push_back({.records = records,
                             .input = "scene-rgb-uint16",
                             .representation = true,
                             .bound = 2.5e-4 * 65535.0 + 1.01,
                             .reason = std::string(kLumaChroma) + "; " + kComponent});
        }
        for (const char* records : {"extract-cie-", "extract-hsl", "extract-hsv", "extract-hsi", "extract-ycbcr-luma."})
        {
            table.push_back({.records = records,
                             .input = "scene-rgb-uint16",
                             .representation = true,
                             .bound = 1.01,
                             .reason = kComponent});
        }
        for (const char* records : {"convert-to-signed-32-bit.", "convert-to-unsigned-32-bit."})
        {
            table.push_back({.records = records,
                             .bound = 130.0,
                             .reason = kThirtyTwo,
                             .explains = atOrAboveFullScale(),
                             .explained = "a sample at or above full scale, which the record holds as the lowest "
                                          "code"});
        }
        table.push_back({.records = "negative-transformation.default/scene-mono-int16",
                         .bound = 0.0,
                         .reason = "the record takes a signed sample from 32767 rather than from the sum of the "
                                   "smallest and the largest code",
                         .explains = halfTheRangeAway(),
                         .explained = "every sample, half the range from the complement"});
        return table;
    }();
    return differences;
}

const AcceptedDifference* acceptedDifferenceFor(const fixtures::Expectation& record)
{
    for (const AcceptedDifference& difference : acceptedDifferences())
    {
        if (record.id.rfind(difference.records, 0u) == 0u &&
            (difference.input.empty() || difference.input == record.inputName))
        {
            return &difference;
        }
    }
    return nullptr;
}

/// The largest value of an unsigned integer representation, by which a fraction is the record's code; one otherwise.
[[nodiscard]] double recordScale(fixtures::FixtureSampleType recorded, fixtures::FixtureSampleType made)
{
    const bool recordedInteger =
        recorded == fixtures::FixtureSampleType::UInt16 || recorded == fixtures::FixtureSampleType::UInt8;
    const bool madeFloating =
        made == fixtures::FixtureSampleType::Float32 || made == fixtures::FixtureSampleType::Float64;
    if (!recordedInteger || !madeFloating)
    {
        return 1.0;
    }
    return recorded == fixtures::FixtureSampleType::UInt16 ? 65535.0 : 255.0;
}

/// Holds the planes an action made to a record: plane i of the record against the first plane of output i when the
/// action made one view per plane, and the record against the one output otherwise.
void expectRecord(const fixtures::Expectation& record, const fixtures::FixtureSamples& input,
                  const std::vector<fixtures::FixtureSamples>& made)
{
    ASSERT_FALSE(made.empty());
    const bool perPlane = made.size() > 1u;
    ASSERT_EQ(perPlane ? static_cast<std::uint32_t>(made.size()) : made.front().channelCount(),
              record.result.channelCount());
    const AcceptedDifference* difference = acceptedDifferenceFor(record);
    const double bound = difference != nullptr ? difference->bound : record.absoluteTolerance;
    double largest = 0.0;
    double largestExplained = 0.0;
    std::uint64_t explained = 0u;
    std::uint64_t beyond = 0u;
    for (std::uint32_t channel = 0u; channel < record.result.channelCount(); ++channel)
    {
        const fixtures::FixtureSamples& output = perPlane ? made[channel] : made.front();
        const std::uint32_t plane = perPlane ? 0u : channel;
        ASSERT_EQ(output.width(), record.result.width());
        ASSERT_EQ(output.height(), record.result.height());
        if (difference == nullptr || !difference->representation)
        {
            EXPECT_EQ(output.sampleType(), record.result.sampleType()) << record.id;
        }
        const double scale = recordScale(record.result.sampleType(), output.sampleType());
        for (std::uint32_t y = 0u; y < output.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < output.width(); ++x)
            {
                const double expected = record.result.value(x, y, channel);
                const double value = output.value(x, y, plane) * scale;
                if (std::isnan(expected) && std::isnan(value))
                {
                    continue;
                }
                const double apart = std::isnan(expected) || std::isnan(value) ? std::numeric_limits<double>::infinity()
                                                                               : std::abs(value - expected);
                const double allowed = (std::max)(bound, record.relativeTolerance * std::abs(expected));
                if (apart > allowed && difference != nullptr && difference->explains &&
                    difference->explains(input, x, y, (std::min)(channel, input.channelCount() - 1u), expected, value))
                {
                    largestExplained = (std::max)(largestExplained, apart);
                    ++explained;
                    continue;
                }
                largest = (std::max)(largest, apart);
                if (apart > allowed)
                {
                    if (++beyond <= 4u)
                    {
                        ADD_FAILURE() << record.id << ": " << apart << " apart at " << x << "," << y << " channel "
                                      << channel << ", " << value << " against " << expected << " (bound " << allowed
                                      << (difference != nullptr ? ", " + difference->reason : std::string()) << ")";
                    }
                }
            }
        }
    }
    std::cout << record.id << ": largest difference " << largest << " within " << bound;
    if (explained > 0u)
    {
        std::cout << "; " << explained << " samples up to " << largestExplained << " apart at "
                  << difference->explained;
    }
    std::cout << "\n";
}

class ChannelRecordTest : public ::testing::TestWithParam<std::size_t>
{
protected:
    [[nodiscard]] const RecordedAction& recorded() const
    {
        return recordedActions()[GetParam()];
    }
};

} // namespace

// Every record of the action's corpus file, through the action as the menu runs it.
TEST_P(ChannelRecordTest, EveryRecordOfTheActionIsReproducedThroughTheAction)
{
    const RecordedAction& under = recorded();
    const std::vector<fixtures::Expectation> records = recordsOf(under.records);
    ASSERT_FALSE(records.empty()) << under.records;
    const shared::RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    for (const fixtures::Expectation& record : records)
    {
        SCOPED_TRACE(record.id);
        const fixtures::FixtureImage* image = fixtures::fixtureNamed(record.inputName);
        ASSERT_NE(image, nullptr);
        std::string error;
        const std::optional<fixtures::FixtureSamples> input = fixtures::materializeFixture(*image, error);
        ASSERT_TRUE(input.has_value()) << error;
        ASSERT_TRUE(fixtures::expectationDescribes(record, *image, *input))
            << "the record was captured from another input than this scene";
        const std::string mode = modeOf(record);
        const int messagesBefore = static_cast<int>(context->messages.size());
        std::vector<fixtures::FixtureSamples> made;
        if (mode == "sequence-frame")
        {
            // A frame of a sequence, converted as the sequence path converts every frame.
            QTemporaryDir directory;
            ASSERT_TRUE(directory.isValid());
            const QString path = writeSceneSequence(context->engineSession(), directory, {record.inputName});
            ASSERT_FALSE(path.isEmpty());
            ImageViewControl* source = openSequenceView(*context, path, an::SequenceFormat::ImageList);
            ASSERT_NE(source, nullptr);
            const std::size_t before = context->madeViews.size();
            runRegisteredAction(under.action);
            const std::vector<ImageViewControl*> views = awaitNewViews(*context, before, 1u, messagesBefore);
            ASSERT_EQ(views.size(), 1u) << context->errorTexts.join(QStringLiteral("; ")).toStdString();
            std::vector<fixtures::FixtureSamples> frames = sequenceFrames(context->engineSession(), *views.front());
            ASSERT_EQ(frames.size(), 1u);
            made.push_back(std::move(frames.front()));
            expectRecord(record, *input, made);
            continue;
        }
        ViewHandle target = sceneView(context->engineSession(), record.inputName);
        ASSERT_NE(target, nullptr);
        activate(*context, *target);
        ViewHandle mask;
        if (mode == "masked")
        {
            // The mono scene as the mask, enabled and not inverted.
            mask = sceneView(context->engineSession(), "scene-mono-float32", "mask");
            ASSERT_NE(mask, nullptr);
            target->setCurrentMask(mask.get());
            target->setEnableMask(true);
            ASSERT_TRUE(target->isMasked());
        }
        const std::size_t before = context->madeViews.size();
        if (under.route == Route::Registered)
        {
            runRegisteredAction(under.action);
            const process_utils::ChannelAction* action = process_utils::channelActionNamed(under.action);
            ASSERT_NE(action, nullptr);
            const std::vector<ImageViewControl*> views =
                awaitNewViews(*context, before, process_utils::channelActionImageCount(*action), messagesBefore);
            ASSERT_EQ(views.size(), process_utils::channelActionImageCount(*action))
                << context->errorTexts.join(QStringLiteral("; ")).toStdString();
            for (ImageViewControl* view : views)
            {
                std::optional<fixtures::FixtureSamples> samples = documentSamples(view->document());
                ASSERT_TRUE(samples.has_value());
                made.push_back(std::move(*samples));
            }
        }
        else if (under.route == Route::Duplicate)
        {
            under.operation(imageOperations());
            const std::vector<ImageViewControl*> views = awaitNewViews(*context, before, 1u, messagesBefore);
            ASSERT_EQ(views.size(), 1u) << context->errorTexts.join(QStringLiteral("; ")).toStdString();
            std::optional<fixtures::FixtureSamples> samples = documentSamples(views.front()->document());
            ASSERT_TRUE(samples.has_value());
            made.push_back(std::move(*samples));
        }
        else
        {
            const std::uint64_t edits = target->workTelemetry().editsCommitted;
            under.operation(imageOperations());
            ASSERT_TRUE(pumpUntil([&]() {
                return target->workTelemetry().editsCommitted > edits ||
                       static_cast<int>(context->messages.size()) > messagesBefore;
            }));
            ASSERT_TRUE(settleAll(*context));
            EXPECT_EQ(target->workTelemetry().editsCommitted, edits + 1u)
                << context->errorTexts.join(QStringLiteral("; ")).toStdString();
            std::optional<fixtures::FixtureSamples> samples = documentSamples(target->document());
            ASSERT_TRUE(samples.has_value());
            made.push_back(std::move(*samples));
        }
        EXPECT_EQ(static_cast<int>(context->messages.size()), messagesBefore)
            << context->errorTexts.join(QStringLiteral("; ")).toStdString();
        expectRecord(record, *input, made);
        if (mask != nullptr)
        {
            target->removeCurrentMask();
            dropView(*context, mask);
        }
        dropView(*context, target);
    }
}

INSTANTIATE_TEST_SUITE_P(ChannelAndImageActions, ChannelRecordTest,
                         ::testing::Range<std::size_t>(0u, recordedActions().size()),
                         [](const ::testing::TestParamInfo<std::size_t>& info) {
                             std::string name = recordedActions()[info.param].records;
                             std::replace(name.begin(), name.end(), '-', '_');
                             return name;
                         });
