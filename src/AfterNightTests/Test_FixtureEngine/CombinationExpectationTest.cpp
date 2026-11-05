// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The product's image combinations and external-tool hand-off through the engine, held to their reference records.
//
// Two sets of records. The behavior corpus already holds the twenty-seven blend modes at their dialog's defaults with
// the framework's opacity and kept lightness, two LRGB settings and the unmasked recombination; each is run through
// the engine's composition from the same inputs and held within the bound its arithmetic explains.
// `combination.expectations` and `handoff.expectations` hold what the corpus does not reach, and are compared by each
// record's classification. The records read every image combined as a fraction of full scale, so a source stating a
// sensor code is restated as normalized, as they assume; the hand-off states its own reading of integers.

#include "fixtures/common/CombinationCases.h"
#include "fixtures/common/FixtureCatalogue.h"
#include "fixtures/common/FixtureExpectation.h"
#include "fixtures/harness/TestHarness.h"
#include "fixtures/engine/EngineFixtureAdapter.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Color/LrgbCombination.h>
#include <AfterNightEngine/Algorithms/Pointwise/BlendComposition.h>
#include <AfterNightEngine/Algorithms/Pointwise/ExternalToolHandoff.h>
#include <AfterNightEngine/Algorithms/Stars/StarMaskRecombination.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
namespace adapter = an::tests::fixtures::engine;

an::Status failure(const std::string& message)
{
    return an::Status::failure(an::ErrorCode::InvalidArgument, an::StatusDetail{.message = message});
}

/// An image the adapter made, kept alive beside the description and layout a composition reads.
struct Made
{
    std::optional<adapter::EngineImage> image;
    std::optional<an::ImageDescriptor> descriptor;
    std::optional<an::ImageLayout> layout;
    const void* samples = nullptr;
};

/// A fixture as the engine holds it, its domain restated as normalized when it states a sensor code.
an::Result<Made> make(const an::EngineContext& engine, const std::string& name, bool restateNormalized)
{
    const fixtures::FixtureImage* fixture = fixtures::fixtureNamed(name);
    if (fixture == nullptr)
    {
        return failure("no fixture is named " + name);
    }
    std::string error;
    const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(*fixture, error);
    if (!samples.has_value())
    {
        return failure(error);
    }
    an::Result<adapter::EngineImage> image = adapter::makeEngineImage(engine, *fixture, *samples);
    if (!image.hasValue())
    {
        return image.status();
    }
    Made made;
    made.image.emplace(std::move(*image));
    const bool viewed = std::visit(
        [&](const auto& typed) {
            const auto descriptor = typed.descriptor();
            const auto view = typed.view();
            if (!descriptor.has_value() || !view.has_value())
            {
                return false;
            }
            an::ImageDescriptorRequest request = descriptor->toRequest();
            if (restateNormalized && request.numericDomain == an::NumericDomain::RawSensorCode)
            {
                request.numericDomain = an::NumericDomain::NormalizedLinear;
            }
            made.descriptor = an::ImageDescriptor::create(request);
            made.layout = view->layout();
            made.samples = view->data();
            return made.descriptor.has_value();
        },
        *made.image);
    if (!viewed)
    {
        return failure(name + " cannot be viewed");
    }
    return made;
}

/// A destination of a representation and channel count, tightly packed and planar, and its samples read back.
struct Output
{
    an::ImageLayout layout;
    std::vector<std::uint8_t> bytes;
    fixtures::FixtureSampleType type;

    [[nodiscard]] fixtures::FixtureSamples samples() const
    {
        fixtures::FixtureSamples result(layout.extent().width(), layout.extent().height(),
                                        layout.extent().channelCount(), type);
        for (std::uint32_t channel = 0u; channel < layout.extent().channelCount(); ++channel)
        {
            const std::span<std::byte> plane = result.mutablePlane(channel);
            std::memcpy(plane.data(), bytes.data() + static_cast<std::uint64_t>(channel) * layout.planeStrideBytes(),
                        plane.size());
        }
        return result;
    }
};

an::Result<Output> outputFor(const an::Result<an::ImageLayoutRequest>& request, fixtures::FixtureSampleType type)
{
    if (!request.hasValue())
    {
        return request.status();
    }
    const std::optional<an::ImageLayout> layout = an::ImageLayout::create(*request, an::defaultEngineLimits());
    if (!layout.has_value())
    {
        return failure("the output cannot be laid out");
    }
    return Output{*layout, std::vector<std::uint8_t>(layout->totalBytes() + layout->rowStrideBytes(), 0u), type};
}

const std::map<std::string, an::BlendCompositionMode>& modesByLabel()
{
    using Mode = an::BlendCompositionMode;
    static const std::map<std::string, Mode> modes = {{"Replace", Mode::Replace},
                                                      {"Darken/Min", Mode::Darken},
                                                      {"Multiply", Mode::Multiply},
                                                      {"Color Burn", Mode::ColorBurn},
                                                      {"Linear Burn", Mode::LinearBurn},
                                                      {"Darker Color", Mode::DarkerColor},
                                                      {"Lighten/Max", Mode::Lighten},
                                                      {"Screen", Mode::Screen},
                                                      {"Color Dodge", Mode::ColorDodge},
                                                      {"Linear Dodge/Add", Mode::LinearDodge},
                                                      {"Lighter Color", Mode::LighterColor},
                                                      {"Overlay", Mode::Overlay},
                                                      {"Soft Light", Mode::SoftLight},
                                                      {"Hard Light", Mode::HardLight},
                                                      {"Vivid Light", Mode::VividLight},
                                                      {"Linear Light", Mode::LinearLight},
                                                      {"Pin Light", Mode::PinLight},
                                                      {"Difference", Mode::Difference},
                                                      {"Exclusion", Mode::Exclusion},
                                                      {"Subtract", Mode::Subtract},
                                                      {"Divide", Mode::Divide},
                                                      {"Power", Mode::Power},
                                                      {"Arctan", Mode::ArcTangent},
                                                      {"Hue", Mode::Hue},
                                                      {"Saturation", Mode::Saturation},
                                                      {"Color", Mode::Color},
                                                      {"Lightness", Mode::Lightness}};
    return modes;
}

an::BlendCompositionRequest blendRequestOf(const fixtures::BlendCaseSettings& settings, an::SampleType output)
{
    an::BlendCompositionRequest request;
    request.mode = modesByLabel().at(settings.mode);
    request.targetTransfer = {.midtones = settings.targetMidtones,
                              .shadows = settings.targetBlackPoint,
                              .highlights = settings.targetHighlights};
    request.blendTransfer = {.midtones = settings.blendMidtones,
                             .shadows = settings.blendBlackPoint,
                             .highlights = settings.blendHighlights};
    if (settings.filter == "Gaussian Blur")
    {
        request.filter = an::BlendPreFilter::GaussianBlur;
    }
    else if (settings.filter == "High Pass")
    {
        request.filter = an::BlendPreFilter::HighPass;
    }
    else if (settings.filter == "Hue")
    {
        request.filter = an::BlendPreFilter::ColorizeLightness;
    }
    else if (settings.filter == "Color")
    {
        request.filter = an::BlendPreFilter::ColorizeChannels;
    }
    request.filterSigma = settings.filterRadius;
    request.filterHueDegrees = settings.filterHue;
    request.filterSaturation = settings.filterSaturation;
    request.preserveLightness = settings.preserveLightness;
    request.opacity = settings.opacity;
    request.outputSampleType = output;
    return request;
}

an::Result<fixtures::FixtureSamples> blendedByEngine(const an::consumer::EngineSession& session,
                                                     const fixtures::BlendCaseSettings& settings,
                                                     const std::string& target, const std::string& second,
                                                     fixtures::FixtureSampleType type)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return hold.status();
    }
    const an::EngineContext& engine = hold->engine();
    const an::Result<Made> base = make(engine, target, true);
    const an::Result<Made> over = make(engine, second, true);
    if (!base.hasValue() || !over.hasValue())
    {
        return base.hasValue() ? over.status() : base.status();
    }
    const an::Result<an::BlendCompositionPlan> plan =
        an::planBlendComposition(blendRequestOf(settings, adapter::engineSampleType(type)));
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::Result<Output> output =
        outputFor(an::blendCompositionLayout(*plan, *base->descriptor, *over->descriptor), type);
    if (!output.hasValue())
    {
        return output.status();
    }
    const an::Result<an::consumer::OperationScope> admitted = adapter::admitFixtureOperation(session);
    if (!admitted.hasValue())
    {
        return admitted.status();
    }
    const an::ExecutionContext& execution = admitted->execution();
    const an::Result<an::BlendCompositionResult> done =
        an::composeBlend(execution, *plan,
                         an::BlendCompositionSource{
                             .descriptor = &*base->descriptor, .samples = base->samples, .layout = &*base->layout},
                         an::BlendCompositionSource{
                             .descriptor = &*over->descriptor, .samples = over->samples, .layout = &*over->layout},
                         output->bytes.data(), output->layout);
    if (!done.hasValue())
    {
        return done.status();
    }
    return output->samples();
}

an::Result<fixtures::FixtureSamples> combinedByEngine(const an::consumer::EngineSession& session,
                                                      const fixtures::LrgbCaseSettings& settings,
                                                      const std::string& color, const std::string& luminance,
                                                      fixtures::FixtureSampleType type)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return hold.status();
    }
    const an::EngineContext& engine = hold->engine();
    const an::Result<Made> rgb = make(engine, color, true);
    const an::Result<Made> lum = make(engine, luminance, true);
    if (!rgb.hasValue() || !lum.hasValue())
    {
        return rgb.hasValue() ? lum.status() : rgb.status();
    }
    const an::Result<an::LrgbCombinationPlan> plan = an::planLrgbCombination(
        an::LrgbCombinationRequest{.lightnessWeight = settings.lightnessWeight,
                                   .lightnessMidtones = settings.lightnessMidtones,
                                   .chromaMidtones = settings.saturationMidtones,
                                   .channelWeights = {settings.redWeight, settings.greenWeight, settings.blueWeight},
                                   .outputSampleType = adapter::engineSampleType(type)});
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::Result<Output> output = outputFor(an::lrgbCombinationLayout(*plan, *rgb->descriptor), type);
    if (!output.hasValue())
    {
        return output.status();
    }
    const an::Result<an::consumer::OperationScope> admitted = adapter::admitFixtureOperation(session);
    if (!admitted.hasValue())
    {
        return admitted.status();
    }
    const an::ExecutionContext& execution = admitted->execution();
    const an::Result<an::LrgbCombinationResult> done = an::combineLrgb(
        execution, *plan,
        an::LrgbCombinationSource{.descriptor = &*rgb->descriptor, .samples = rgb->samples, .layout = &*rgb->layout},
        an::LrgbCombinationSource{.descriptor = &*lum->descriptor, .samples = lum->samples, .layout = &*lum->layout},
        output->bytes.data(), output->layout);
    if (!done.hasValue())
    {
        return done.status();
    }
    return output->samples();
}

an::Result<fixtures::FixtureSamples> recombinedByEngine(const an::consumer::EngineSession& session,
                                                        const fixtures::RecombinationCaseSettings& settings,
                                                        const std::string& stars, const std::string& starless,
                                                        fixtures::FixtureSampleType type)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return hold.status();
    }
    const an::EngineContext& engine = hold->engine();
    const an::Result<Made> added = make(engine, stars, true);
    const an::Result<Made> base = make(engine, starless, true);
    if (!added.hasValue() || !base.hasValue())
    {
        return added.hasValue() ? base.status() : added.status();
    }
    std::optional<Made> mask;
    if (!settings.mask.empty())
    {
        an::Result<Made> made = make(engine, settings.mask, true);
        if (!made.hasValue())
        {
            return made.status();
        }
        mask.emplace(std::move(*made));
    }
    const an::Result<an::StarMaskRecombinationPlan> plan = an::planStarMaskRecombination(
        an::StarMaskRecombinationRequest{.edgeTightening = settings.tightenEdges,
                                         .invertMask = settings.invertMask,
                                         .outputSampleType = adapter::engineSampleType(type)});
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::Result<Output> output =
        outputFor(an::starMaskRecombinationLayout(*plan, *base->descriptor, *added->descriptor), type);
    if (!output.hasValue())
    {
        return output.status();
    }
    const an::Result<an::consumer::OperationScope> admitted = adapter::admitFixtureOperation(session);
    if (!admitted.hasValue())
    {
        return admitted.status();
    }
    const an::ExecutionContext& execution = admitted->execution();
    const an::StarMaskRecombinationSource maskSource =
        mask.has_value() ? an::StarMaskRecombinationSource{.descriptor = &*mask->descriptor,
                                                           .samples = mask->samples,
                                                           .layout = &*mask->layout}
                         : an::StarMaskRecombinationSource{};
    const an::Result<an::StarMaskRecombinationResult> done = an::recombineStarMask(
        execution, *plan,
        an::StarMaskRecombinationSource{
            .descriptor = &*base->descriptor, .samples = base->samples, .layout = &*base->layout},
        an::StarMaskRecombinationSource{
            .descriptor = &*added->descriptor, .samples = added->samples, .layout = &*added->layout},
        mask.has_value() ? &maskSource : nullptr, output->bytes.data(), output->layout);
    if (!done.hasValue())
    {
        return done.status();
    }
    return output->samples();
}

an::Result<fixtures::FixtureSamples> handedByEngine(const an::consumer::EngineSession& session,
                                                    const fixtures::HandoffCaseSettings& settings,
                                                    const std::string& input, fixtures::FixtureSampleType type)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return hold.status();
    }
    const an::EngineContext& engine = hold->engine();
    const an::Result<Made> source = make(engine, input, false);
    if (!source.hasValue())
    {
        return source.status();
    }
    const an::ExternalToolPreStretch stretch = settings.stretch == "unlinked" ? an::ExternalToolPreStretch::Unlinked
                                               : settings.stretch == "linked" ? an::ExternalToolPreStretch::Linked
                                                                              : an::ExternalToolPreStretch::None;
    const an::Result<an::ExternalToolHandoffPlan> plan =
        an::planExternalToolHandoff(an::ExternalToolHandoffRequest{.preStretch = stretch});
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::Result<Output> handed =
        outputFor(an::externalToolHandoffLayout(*plan, *source->descriptor), fixtures::FixtureSampleType::UInt16);
    if (!handed.hasValue())
    {
        return handed.status();
    }
    const an::Result<an::consumer::OperationScope> admitted = adapter::admitFixtureOperation(session);
    if (!admitted.hasValue())
    {
        return admitted.status();
    }
    const an::ExecutionContext& execution = admitted->execution();
    const an::ExternalToolSource sourceView{
        .descriptor = &*source->descriptor, .samples = source->samples, .layout = &*source->layout};
    const an::Result<an::ExternalToolHandoff> handoff =
        an::prepareExternalToolHandoff(execution, *plan, sourceView, handed->bytes.data(), handed->layout);
    if (!handoff.hasValue())
    {
        return handoff.status();
    }
    if (settings.output == fixtures::HandoffCaseOutput::Handed)
    {
        return handed->samples();
    }
    // A tool that changes nothing answers with the codes it was handed.
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(handed->layout.extent().width(), handed->layout.extent().height(),
                                handed->layout.extent().channelCount(), an::defaultEngineLimits());
    const std::optional<an::ImageDescriptor> answerDescriptor =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = an::SampleType::UInt16,
                                                               .channelSchema = source->descriptor->channelSchema(),
                                                               .colorSpace = an::ColorSpace::Unspecified,
                                                               .numericDomain = an::NumericDomain::NormalizedLinear,
                                                               .cfa = {},
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    if (!answerDescriptor.has_value())
    {
        return failure("the answer cannot be described");
    }
    an::ImageLayoutRequest restoredRequest = *an::externalToolHandoffLayout(*plan, *source->descriptor);
    restoredRequest.elementSizeBytes = an::sampleTypeSizeBytes(adapter::engineSampleType(type));
    restoredRequest.rowAlignmentBytes = restoredRequest.elementSizeBytes;
    an::Result<Output> restored = outputFor(restoredRequest, type);
    if (!restored.hasValue())
    {
        return restored.status();
    }
    const an::Result<an::ExternalToolRestoreResult> done = an::restoreExternalToolAnswer(
        execution, *plan, *handoff,
        settings.output == fixtures::HandoffCaseOutput::RestoredStars ? an::ExternalToolLayer::StarLayer
                                                                      : an::ExternalToolLayer::Image,
        an::ExternalToolSource{
            .descriptor = &*answerDescriptor, .samples = handed->bytes.data(), .layout = &handed->layout},
        restored->bytes.data(), restored->layout);
    if (!done.hasValue())
    {
        return done.status();
    }
    return restored->samples();
}

an::Result<fixtures::FixtureSamples> runByEngine(const an::consumer::EngineSession& session,
                                                 const fixtures::CombinationCase& item,
                                                 fixtures::FixtureSampleType type)
{
    switch (item.kind)
    {
    case fixtures::CombinationCaseKind::Blend:
        return blendedByEngine(session, item.blend, item.input, item.second, type);
    case fixtures::CombinationCaseKind::Lrgb:
        return combinedByEngine(session, item.lrgb, item.input, item.second, type);
    case fixtures::CombinationCaseKind::Recombination:
        return recombinedByEngine(session, item.recombination, item.input, item.second, type);
    case fixtures::CombinationCaseKind::Handoff:
        return handedByEngine(session, item.handoff, item.input, type);
    }
    return failure("unknown case kind");
}

double largestDifference(const fixtures::FixtureSamples& left, const fixtures::FixtureSamples& right)
{
    double largest = 0.0;
    for (std::uint32_t channel = 0u; channel < left.channelCount(); ++channel)
    {
        for (std::uint32_t y = 0u; y < left.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < left.width(); ++x)
            {
                largest = (std::max)(largest, std::abs(left.value(x, y, channel) - right.value(x, y, channel)));
            }
        }
    }
    return largest;
}

/// One corpus record of a combination, the settings that reproduce it, and how close the engine must come.
struct CorpusCase
{
    const char* file;
    const char* mode;
    fixtures::CombinationCase item;
    double bound;
    const char* reason;
};

constexpr const char* kSameFormula = "the same formula over the same Float32 samples, computed in double by both";
/// What a CIE conversion through the record's matrices and the engine's may part by: the bound the channel scope's
/// CIE records are held to.
constexpr double kCieBound = 5.0e-5;
constexpr const char* kLabMatrix =
    "a CIE conversion through an XYZ matrix and its inverse, the record's inverse a printed approximation";

const std::vector<CorpusCase>& corpusCases()
{
    static const std::vector<CorpusCase> cases = [] {
        std::vector<CorpusCase> built;
        const std::vector<std::pair<const char*, const char*>> labels = {{"replace", "Replace"},
                                                                         {"darken-min", "Darken/Min"},
                                                                         {"multiply", "Multiply"},
                                                                         {"color-burn", "Color Burn"},
                                                                         {"linear-burn", "Linear Burn"},
                                                                         {"darker-color", "Darker Color"},
                                                                         {"lighten-max", "Lighten/Max"},
                                                                         {"screen", "Screen"},
                                                                         {"color-dodge", "Color Dodge"},
                                                                         {"linear-dodge-add", "Linear Dodge/Add"},
                                                                         {"lighter-color", "Lighter Color"},
                                                                         {"overlay", "Overlay"},
                                                                         {"soft-light", "Soft Light"},
                                                                         {"hard-light", "Hard Light"},
                                                                         {"vivid-light", "Vivid Light"},
                                                                         {"linear-light", "Linear Light"},
                                                                         {"pin-light", "Pin Light"},
                                                                         {"difference", "Difference"},
                                                                         {"exclusion", "Exclusion"},
                                                                         {"subtract", "Subtract"},
                                                                         {"divide", "Divide"},
                                                                         {"power", "Power"},
                                                                         {"arctan", "Arctan"},
                                                                         {"hue", "Hue"},
                                                                         {"saturation", "Saturation"},
                                                                         {"color", "Color"},
                                                                         {"lightness", "Lightness"}};
        for (const auto& [mode, label] : labels)
        {
            fixtures::CombinationCase item;
            item.kind = fixtures::CombinationCaseKind::Blend;
            item.input = "scene-rgb-float32";
            item.second = "scene-rgb-float32-second";
            item.blend.mode = label;
            const std::string name = mode;
            const bool lab =
                name == "darker-color" || name == "lighter-color" || name == "color" || name == "lightness";
            built.push_back({"behavior/image-blend.expectations", mode, item, lab ? kCieBound : 1.0e-6,
                             lab ? kLabMatrix : kSameFormula});
        }
        fixtures::CombinationCase opacity;
        opacity.kind = fixtures::CombinationCaseKind::Blend;
        opacity.input = "scene-rgb-float32";
        opacity.second = "scene-rgb-float32-second";
        opacity.blend.mode = "Overlay";
        opacity.blend.opacity = 0.5;
        built.push_back({"behavior/image-blend.expectations", "overlay-opacity-0.5", opacity, 1.0e-6, kSameFormula});
        fixtures::CombinationCase preserve = opacity;
        preserve.blend.mode = "Multiply";
        preserve.blend.opacity = 1.0;
        preserve.blend.preserveLightness = true;
        built.push_back(
            {"behavior/image-blend.expectations", "multiply-preserve-lightness", preserve, kCieBound, kLabMatrix});

        fixtures::CombinationCase lrgb;
        lrgb.kind = fixtures::CombinationCaseKind::Lrgb;
        lrgb.input = "scene-rgb-float32";
        lrgb.second = "scene-mono-float32";
        built.push_back({"behavior/lrgb-combination.expectations", "default", lrgb, kCieBound, kLabMatrix});
        lrgb.lrgb = fixtures::LrgbCaseSettings{.lightnessWeight = 0.6,
                                               .redWeight = 0.9,
                                               .greenWeight = 1.0,
                                               .blueWeight = 0.8,
                                               .lightnessMidtones = 0.3,
                                               .saturationMidtones = 0.4};
        built.push_back({"behavior/lrgb-combination.expectations",
                         "lightness-midtones-0.3-saturation-midtones-0.4-weights", lrgb, kCieBound, kLabMatrix});

        fixtures::CombinationCase recombination;
        recombination.kind = fixtures::CombinationCaseKind::Recombination;
        recombination.input = "stars-rgb-float32";
        recombination.second = "stars-rgb-float32-starless";
        built.push_back(
            {"behavior/starmask-recombination.expectations", "default", recombination, 1.0e-6, kSameFormula});
        return built;
    }();
    return cases;
}

/// The corpus case a record belongs to: its id is `<action>.<mode>/<input>`, the mode `default` for a dash.
const CorpusCase* corpusCaseOf(const fixtures::Expectation& record, const char* file)
{
    for (const CorpusCase& item : corpusCases())
    {
        if (std::string(item.file) != file)
        {
            continue;
        }
        const std::string prefix = record.id.substr(0u, record.id.find('/'));
        const std::string mode = prefix.substr(prefix.find('.') + 1u);
        if (mode == item.mode && record.inputName == item.item.input)
        {
            return &item;
        }
    }
    return nullptr;
}

class CombinationExpectationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        an::Result<an::consumer::EngineSession> session = adapter::createFixtureSession(4u);
        ASSERT_TRUE(session.hasValue()) << session.status().message();
        m_session.emplace(std::move(*session));
        std::string error;
        const std::optional<std::filesystem::path> directory = fixtures::expectationDirectory(error);
        ASSERT_TRUE(directory.has_value()) << error;
        m_directory = *directory;
    }

    void TearDown() override
    {
        m_session.reset();
    }

    [[nodiscard]] std::vector<fixtures::Expectation> read(const char* file) const
    {
        std::string error;
        const std::optional<std::vector<fixtures::Expectation>> records =
            fixtures::readExpectationFile(m_directory / file, error);
        EXPECT_TRUE(records.has_value()) << file << ": " << error;
        return records.value_or(std::vector<fixtures::Expectation>{});
    }

    std::optional<an::consumer::EngineSession> m_session;
    std::filesystem::path m_directory;
};

} // namespace

// Every blend mode, LRGB setting and recombination the behavior corpus records, run through the engine's
// compositions from the same inputs and held within the bound its arithmetic explains.
TEST_F(CombinationExpectationTest, EveryCorpusCombinationMatchesItsRecordWithinItsStatedBound)
{
    std::set<const CorpusCase*> seen;
    for (const char* file : {"behavior/image-blend.expectations", "behavior/lrgb-combination.expectations",
                             "behavior/starmask-recombination.expectations"})
    {
        for (const fixtures::Expectation& record : read(file))
        {
            const CorpusCase* item = corpusCaseOf(record, file);
            ASSERT_NE(item, nullptr) << record.id << " belongs to no corpus case";
            EXPECT_TRUE(seen.insert(item).second) << record.id;
            const fixtures::FixtureImage* input = fixtures::fixtureNamed(record.inputName);
            ASSERT_NE(input, nullptr) << record.id;
            std::string error;
            const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(*input, error);
            ASSERT_TRUE(samples.has_value()) << error;
            ASSERT_TRUE(fixtures::expectationDescribes(record, *input, *samples))
                << record.id << " was captured from another input than this one";
            const an::Result<fixtures::FixtureSamples> produced =
                runByEngine(*m_session, item->item, record.result.sampleType());
            ASSERT_TRUE(produced.hasValue()) << record.id << ": " << produced.status().message();
            ASSERT_EQ(produced->channelCount(), record.result.channelCount()) << record.id;
            const double largest = largestDifference(record.result, *produced);
            RecordProperty(record.id, std::to_string(largest));
            EXPECT_LE(largest, item->bound) << record.id << " (" << item->reason << ")";
        }
    }
    EXPECT_EQ(seen.size(), corpusCases().size());
}

// Every recorded combination and hand-off, run by the engine from the same inputs and held to the record by its
// classification; an accepted one is held to its case's bound.
TEST_F(CombinationExpectationTest, EveryCombinationAndHandOffMatchesItsRecordByItsClassification)
{
    std::size_t compared = 0u;
    for (const char* file : {fixtures::kCombinationExpectationFile, fixtures::kHandoffExpectationFile})
    {
        const std::vector<fixtures::Expectation> records = read(file);
        ASSERT_FALSE(records.empty()) << file;
        for (const fixtures::Expectation& record : records)
        {
            const fixtures::CombinationCase* item = fixtures::combinationCase(record.id);
            ASSERT_NE(item, nullptr) << record.id << " belongs to no case";
            const fixtures::FixtureImage* input = fixtures::fixtureNamed(item->input);
            ASSERT_NE(input, nullptr) << record.id;
            std::string error;
            const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(*input, error);
            ASSERT_TRUE(samples.has_value()) << error;
            ASSERT_TRUE(fixtures::expectationDescribes(record, *input, *samples))
                << record.id << " was captured from another input than this one";
            const an::Result<fixtures::FixtureSamples> produced =
                runByEngine(*m_session, *item, record.result.sampleType());
            ASSERT_TRUE(produced.hasValue()) << record.id << ": " << produced.status().message();
            const fixtures::ExpectationComparison comparison = fixtures::compareWithExpectation(record, *produced);
            const double largest = largestDifference(record.result, *produced);
            RecordProperty(record.id, std::to_string(largest));
            EXPECT_TRUE(comparison.matched)
                << record.id << ": " << comparison.reason << "; the largest difference is " << largest;
            if (item->classification == fixtures::ExpectationClass::Accepted)
            {
                EXPECT_LE(largest, item->acceptedBound) << record.id;
            }
            ++compared;
        }
    }
    EXPECT_EQ(compared, fixtures::combinationCases().size());
}

// The edge-tightened recombinations part only where the record's edge finder read across a row's end: every pixel
// away from the first and last column agrees within the rounding.
TEST_F(CombinationExpectationTest, TheEdgeTightenedRecombinationsDifferOnlyAtTheRowEnds)
{
    std::size_t checked = 0u;
    for (const fixtures::Expectation& record : read(fixtures::kCombinationExpectationFile))
    {
        const fixtures::CombinationCase* item = fixtures::combinationCase(record.id);
        ASSERT_NE(item, nullptr) << record.id;
        if (item->kind != fixtures::CombinationCaseKind::Recombination || item->recombination.tightenEdges == 0.0)
        {
            continue;
        }
        const an::Result<fixtures::FixtureSamples> produced =
            runByEngine(*m_session, *item, record.result.sampleType());
        ASSERT_TRUE(produced.hasValue()) << record.id << ": " << produced.status().message();
        double interior = 0.0;
        double ends = 0.0;
        for (std::uint32_t channel = 0u; channel < record.result.channelCount(); ++channel)
        {
            for (std::uint32_t y = 0u; y < record.result.height(); ++y)
            {
                for (std::uint32_t x = 0u; x < record.result.width(); ++x)
                {
                    const double difference =
                        std::abs(record.result.value(x, y, channel) - produced->value(x, y, channel));
                    const bool atEnd = x == 0u || x + 1u == record.result.width();
                    (atEnd ? ends : interior) = (std::max)(atEnd ? ends : interior, difference);
                }
            }
        }
        RecordProperty(record.id + " interior", std::to_string(interior));
        RecordProperty(record.id + " row ends", std::to_string(ends));
        EXPECT_LE(interior, 1.0e-6) << record.id;
        EXPECT_GT(ends, interior) << record.id << ": the difference is the row ends'";
        ++checked;
    }
    EXPECT_EQ(checked, 2u);
}

// Every case is recorded once, and no record compared was produced by the engine under test.
TEST_F(CombinationExpectationTest, EveryCaseHasARecord)
{
    std::set<std::string> ids;
    for (const char* file : {fixtures::kCombinationExpectationFile, fixtures::kHandoffExpectationFile})
    {
        for (const fixtures::Expectation& record : read(file))
        {
            EXPECT_TRUE(ids.insert(record.id).second) << record.id;
            EXPECT_EQ(record.provenance.producer, "AfterNightEngine") << record.id;
            EXPECT_TRUE(adapter::producedElsewhere(record.provenance)) << record.id;
        }
    }
    for (const fixtures::CombinationCase& item : fixtures::combinationCases())
    {
        EXPECT_EQ(ids.count(item.id()), 1u) << item.id();
    }
}
