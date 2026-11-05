// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "StackingNumericsCases.h"

#include <utility>

namespace an::tests::fixtures
{

namespace
{

constexpr const char* kCometSource = "src/AfterNightProcesses/processes/ImageIntegration/workflow/stages/"
                                     "StackCometProducts.cpp";
constexpr const char* kStackingSource = "src/AfterNightProcesses/processes/ImageIntegration/workflow/stages/"
                                        "LightsStackingStage.cpp";
constexpr const char* kDrizzleSource = "src/AfterNightProcesses/processes/ImageIntegration/workflow/stages/"
                                       "LightsDrizzleStage.cpp";
constexpr const char* kStandaloneSource = "src/AfterNightProcesses/processes/core/StandaloneStackingUtils.cpp";

FixtureImage describe(std::string name, std::uint32_t width, std::uint32_t height, FixtureChannels channels,
                      FixtureSampleType sampleType, FixtureDomain domain, FixturePattern pattern,
                      std::vector<FixtureChannelLevels> levels)
{
    FixtureImage image;
    image.name = std::move(name);
    image.width = width;
    image.height = height;
    image.channels = channels;
    image.sampleType = sampleType;
    image.domain = domain;
    image.pattern = pattern;
    image.levels = std::move(levels);
    return image;
}

/// The field's stars, away from where the comet moves.
std::vector<FixtureStar> fieldStars(double scale)
{
    std::vector<FixtureStar> stars{{.x = 8.0, .y = 6.0, .width = 1.4, .peak = 0.55},
                                   {.x = 52.0, .y = 8.0, .width = 1.2, .peak = 0.45},
                                   {.x = 12.0, .y = 40.0, .width = 1.6, .peak = 0.7},
                                   {.x = 56.0, .y = 41.0, .width = 1.3, .peak = 0.5},
                                   {.x = 40.0, .y = 38.0, .width = 1.1, .peak = 0.35}};
    for (FixtureStar& star : stars)
    {
        star.x *= scale;
        star.y *= scale;
        star.width *= scale;
    }
    return stars;
}

/// A rejection count map: zero everywhere but the stated blocks, each a rectangle of one count.
struct CountBlock
{
    std::uint32_t x = 0u;
    std::uint32_t y = 0u;
    std::uint32_t width = 1u;
    std::uint32_t height = 1u;
    std::uint32_t channel = 0u;
    double count = 1.0;
};

FixtureImage countMap(std::string name, std::uint32_t width, std::uint32_t height, FixtureChannels channels,
                      const std::vector<CountBlock>& blocks)
{
    FixtureImage map =
        describe(std::move(name), width, height, channels, FixtureSampleType::UInt32, FixtureDomain::SensorCode,
                 FixturePattern::Constant, std::vector<FixtureChannelLevels>(channelCountOf(channels), {.base = 0.0}));
    for (const CountBlock& block : blocks)
    {
        for (std::uint32_t y = block.y; y < block.y + block.height; ++y)
        {
            for (std::uint32_t x = block.x; x < block.x + block.width; ++x)
            {
                map.overrides.push_back({.x = x, .y = y, .channel = block.channel, .value = block.count});
            }
        }
    }
    return map;
}

std::vector<FixtureImage> buildInputs()
{
    std::vector<FixtureImage> inputs;
    const FixtureChannelLevels sky{
        .base = 0.08, .perColumn = 0.0003, .perRow = 0.0002, .alternate = 1.0, .amplitude = 0.004};

    // A frame with a comet among the stars, the comet a wide profile centred on a pixel.
    FixtureImage field =
        describe("comet-field-mono-float32", 64u, 48u, FixtureChannels::Mono, FixtureSampleType::Float32,
                 FixtureDomain::NormalizedLinear, FixturePattern::StarField, {sky});
    field.seed = 41u;
    field.stars = fieldStars(1.0);
    field.stars.push_back({.x = 30.0, .y = 22.0, .width = 3.5, .peak = 0.6});
    inputs.push_back(std::move(field));

    // The same in sixteen-bit colour, read as a fraction of full scale.
    FixtureImage colour =
        describe("comet-field-rgb-uint16", 48u, 40u, FixtureChannels::Rgb, FixtureSampleType::UInt16,
                 FixtureDomain::NormalizedLinear, FixturePattern::StarField,
                 {{.base = 5200.0, .perColumn = 20.0, .perRow = 12.0, .alternate = 40000.0, .amplitude = 250.0},
                  {.base = 5900.0, .perColumn = 14.0, .perRow = 18.0, .alternate = 36000.0, .amplitude = 250.0},
                  {.base = 6400.0, .perColumn = 10.0, .perRow = 22.0, .alternate = 30000.0, .amplitude = 250.0}});
    colour.seed = 42u;
    colour.stars = {{.x = 6.0, .y = 5.0, .width = 1.4, .peak = 0.5},
                    {.x = 40.0, .y = 33.0, .width = 1.2, .peak = 0.45},
                    {.x = 24.0, .y = 20.0, .width = 3.0, .peak = 0.6}};
    inputs.push_back(std::move(colour));

    // A star stack and a comet stack, at the registration scale and enlarged by two; the comet stack's canvas is
    // its own, larger than the star stack's.
    for (const double scale : {1.0, 2.0})
    {
        const std::string suffix = scale == 1.0 ? "" : "-x2";
        const auto size = [scale](std::uint32_t pixels) { return static_cast<std::uint32_t>(pixels * scale); };
        FixtureImage stars =
            describe("comet-stack-stars-mono-float32" + suffix, size(64u), size(48u), FixtureChannels::Mono,
                     FixtureSampleType::Float32, FixtureDomain::NormalizedLinear, FixturePattern::StarField, {sky});
        stars.seed = 43u;
        stars.stars = fieldStars(scale);
        inputs.push_back(std::move(stars));
        FixtureImage comet =
            describe("comet-stack-comet-mono-float32" + suffix, size(72u), size(52u), FixtureChannels::Mono,
                     FixtureSampleType::Float32, FixtureDomain::NormalizedLinear, FixturePattern::StarField, {sky});
        comet.seed = 44u;
        comet.stars = {{.x = 33.0 * scale, .y = 20.0 * scale, .width = 3.0 * scale, .peak = 0.6}};
        inputs.push_back(std::move(comet));
    }

    // The planes a mask is drawn on; only their extent is read.
    inputs.push_back(describe("comet-plane-mono-float32", 64u, 48u, FixtureChannels::Mono, FixtureSampleType::Float32,
                              FixtureDomain::NormalizedLinear, FixturePattern::Constant, {{.base = 0.0}}));
    inputs.push_back(describe("comet-plane-mono-float32-x2", 128u, 96u, FixtureChannels::Mono,
                              FixtureSampleType::Float32, FixtureDomain::NormalizedLinear, FixturePattern::Constant,
                              {{.base = 0.0}}));

    // Rejection count maps: compact clusters, a thin diagonal, singles, and elements both maps rejected.
    std::vector<CountBlock> low{{.x = 4u, .y = 3u, .width = 4u, .height = 3u, .count = 2.0},
                                {.x = 28u, .y = 18u, .width = 3u, .height = 3u, .count = 1.0},
                                {.x = 20u, .y = 6u, .width = 1u, .height = 1u, .count = 3.0},
                                {.x = 35u, .y = 2u, .width = 2u, .height = 1u, .count = 1.0},
                                {.x = 12u, .y = 22u, .width = 5u, .height = 2u, .count = 1.0}};
    for (std::uint32_t step = 0u; step < 8u; ++step)
    {
        low.push_back({.x = 10u + step, .y = 10u + step, .count = 1.0});
    }
    inputs.push_back(countMap("rejection-low-mono-uint32", 40u, 30u, FixtureChannels::Mono, low));
    inputs.push_back(countMap("rejection-high-mono-uint32", 40u, 30u, FixtureChannels::Mono,
                              {{.x = 6u, .y = 4u, .width = 4u, .height = 4u, .count = 1.0},
                               {.x = 30u, .y = 20u, .width = 6u, .height = 5u, .count = 4.0},
                               {.x = 2u, .y = 26u, .width = 1u, .height = 3u, .count = 2.0},
                               {.x = 38u, .y = 0u, .width = 2u, .height = 2u, .count = 1.0}}));
    inputs.push_back(countMap("rejection-low-rgb-uint32", 24u, 18u, FixtureChannels::Rgb,
                              {{.x = 2u, .y = 2u, .width = 3u, .height = 3u, .channel = 0u, .count = 1.0},
                               {.x = 10u, .y = 5u, .width = 4u, .height = 2u, .channel = 1u, .count = 2.0},
                               {.x = 18u, .y = 12u, .width = 3u, .height = 4u, .channel = 2u, .count = 1.0},
                               {.x = 5u, .y = 14u, .width = 1u, .height = 1u, .channel = 2u, .count = 5.0}}));
    inputs.push_back(countMap("rejection-high-rgb-uint32", 24u, 18u, FixtureChannels::Rgb,
                              {{.x = 3u, .y = 3u, .width = 3u, .height = 3u, .channel = 0u, .count = 2.0},
                               {.x = 12u, .y = 9u, .width = 3u, .height = 3u, .channel = 1u, .count = 1.0},
                               {.x = 0u, .y = 0u, .width = 2u, .height = 1u, .channel = 2u, .count = 1.0}}));

    // Frames that state only an extent, for the canvas and the weights.
    inputs.push_back(describe("drizzle-frame-mono-float32", 120u, 90u, FixtureChannels::Mono,
                              FixtureSampleType::Float32, FixtureDomain::NormalizedLinear, FixturePattern::Constant,
                              {{.base = 0.1}}));
    inputs.push_back(describe("weighting-frame-mono-float32", 8u, 6u, FixtureChannels::Mono, FixtureSampleType::Float32,
                              FixtureDomain::NormalizedLinear, FixturePattern::Constant, {{.base = 0.1}}));
    return inputs;
}

StackingNumericsCase base(std::string operation, std::string input, StackingNumericsCaseKind kind, std::string text,
                          std::string source)
{
    StackingNumericsCase item;
    item.operation = std::move(operation);
    item.input = std::move(input);
    item.kind = kind;
    item.text = std::move(text);
    item.source = std::move(source);
    return item;
}

std::string at(const char* file, int line)
{
    return std::string(file) + ":" + std::to_string(line);
}

/// Every frame's aperture along the comet's track: four frames, the comet four pixels further each frame, each frame
/// registered onto the stars by a translation.
std::vector<CometCaseAperture> trackApertures()
{
    return {{.x = 22.5, .y = 24.5, .radius = 6.0, .dx = -2.0, .dy = 0.0},
            {.x = 26.5, .y = 25.5, .radius = 6.0, .dx = 2.0, .dy = -1.0},
            {.x = 31.25, .y = 23.75, .radius = 6.5, .dx = 5.25, .dy = 0.75},
            {.x = 30.5, .y = 22.5, .radius = 5.5, .dx = 12.0, .dy = 2.0}};
}

std::vector<StackingNumericsCase> build()
{
    std::vector<StackingNumericsCase> cases;
    constexpr auto kExact = ExpectationClass::Exact;
    constexpr auto kTolerance = ExpectationClass::Tolerance;
    constexpr auto kAccepted = ExpectationClass::Accepted;

    // ---- The comet split: the aperture's sky from its annulus, and both layers, at dyadic centres so the record's
    // integer-centred distances and the engine's agree to the bit.
    const std::string splitText = "the stars-plus-comet split at a comet aperture: the sky from the annulus median, "
                                  "the star layer with the aperture filled by it and the comet layer the sky plus "
                                  "what the star layer took";
    StackingNumericsCase starLayer =
        base("comet.split-star-layer", "comet-field-mono-float32", StackingNumericsCaseKind::CometSplit,
             splitText + "; the star layer", at(kCometSource, 558));
    starLayer.apertures = {{.x = 30.5, .y = 22.5, .radius = 6.0}};
    starLayer.classification = kExact;
    cases.push_back(starLayer);

    StackingNumericsCase cometLayer = starLayer;
    cometLayer.operation = "comet.split-comet-layer";
    cometLayer.text = splitText + "; the comet layer";
    cometLayer.cometLayer = true;
    cases.push_back(cometLayer);

    StackingNumericsCase offCentre = starLayer;
    offCentre.operation = "comet.split-star-layer-between-pixels";
    offCentre.text = splitText + "; the star layer, the aperture between pixel centres";
    offCentre.apertures = {{.x = 30.25, .y = 22.75, .radius = 5.5}};
    cases.push_back(offCentre);

    StackingNumericsCase colourSplit =
        base("comet.split-comet-layer", "comet-field-rgb-uint16", StackingNumericsCaseKind::CometSplit,
             splitText + "; the comet layer of a sixteen-bit colour frame, converted "
                         "to Float32 first",
             at(kCometSource, 558));
    colourSplit.apertures = {{.x = 24.5, .y = 20.5, .radius = 5.0}};
    colourSplit.cometLayer = true;
    colourSplit.classification = kExact;
    cases.push_back(colourSplit);

    StackingNumericsCase outside =
        base("comet.split-comet-layer-whole-sky", "comet-field-mono-float32", StackingNumericsCaseKind::CometSplit,
             splitText + "; the comet layer of an aperture beyond the frame's corner, "
                         "whose annulus holds no sample, so the sky is the whole frame's "
                         "median",
             at(kCometSource, 460));
    outside.apertures = {{.x = -20.0, .y = -20.0, .radius = 4.0}};
    outside.cometLayer = true;
    outside.classification = kExact;
    cases.push_back(outside);

    // ---- The comet mask over the track's projected apertures.
    StackingNumericsCase mask = base("comet.mask", "comet-plane-mono-float32", StackingNumericsCaseKind::CometMask,
                                     "the stars-plus-comet mask: every frame's aperture projected through its star "
                                     "registration with 48 boundary samples, its radius the mean polygon distance, "
                                     "feathered over the larger of two pixels and 0.45 radii, the largest weight kept",
                                     at(kCometSource, 855));
    mask.apertures = trackApertures();
    mask.classification = kTolerance;
    mask.absoluteTolerance = 1.0e-6;
    mask.note = "the two projections place their boundary samples at different angles, so a mean radius can part in "
                "its last bits and a feathered weight with it, well inside a Float32 step of the weight";
    cases.push_back(mask);

    StackingNumericsCase drizzledMask = mask;
    drizzledMask.operation = "comet.mask-drizzled";
    drizzledMask.input = "comet-plane-mono-float32-x2";
    drizzledMask.text = mask.text + ", on a plane drizzled by two";
    drizzledMask.scale = 2.0;
    drizzledMask.classification = kAccepted;
    drizzledMask.absoluteTolerance = 0.0;
    drizzledMask.acceptedReason =
        "the record enlarges an integer-centred aperture centre by the scale and the engine enlarges a "
        "half-centred one, so on a drizzled plane every circle sits half a pixel times one less than the scale away";
    drizzledMask.acceptedBound = 0.2;
    drizzledMask.note = "a half-pixel shift moves a weight by at most half a pixel over the feather, which on this "
                        "track is more than five drizzled pixels wide";
    cases.push_back(drizzledMask);

    // ---- The blend of a comet stack into a star stack through that mask.
    StackingNumericsCase blend =
        base("comet.blend", "comet-stack-stars-mono-float32", StackingNumericsCaseKind::CometBlend,
             "the stars-plus-comet blend: where the mask weighs a pixel, its position mapped "
             "back through the reference frame's star registration and forward through its "
             "comet registration, the comet stack read bilinearly there and blended by the "
             "mask",
             at(kCometSource, 987));
    blend.second = "comet-stack-comet-mono-float32";
    blend.apertures = trackApertures();
    blend.cometDx = 3.0;
    blend.cometDy = -2.0;
    blend.classification = kTolerance;
    blend.absoluteTolerance = 1.0e-6;
    blend.note = "the mask the blend reads parts from the record's in the last bits of a weight, which moves a "
                 "blended sample by less than a Float32 step of the stacks";
    cases.push_back(blend);

    StackingNumericsCase drizzledBlend = blend;
    drizzledBlend.operation = "comet.blend-drizzled";
    drizzledBlend.input = "comet-stack-stars-mono-float32-x2";
    drizzledBlend.second = "comet-stack-comet-mono-float32-x2";
    drizzledBlend.text = blend.text + ", both stacks drizzled by two";
    drizzledBlend.scale = 2.0;
    drizzledBlend.classification = kAccepted;
    drizzledBlend.absoluteTolerance = 0.0;
    drizzledBlend.acceptedReason =
        "the record divides an integer pixel position by the scale and the engine divides a half-centred one, so "
        "on drizzled stacks the comet stack is read half a pixel times one less than the scale away, and the mask "
        "moves with it";
    drizzledBlend.acceptedBound = 0.02;
    drizzledBlend.note = "a half-pixel shift of a bilinear read moves a sample by at most half the largest step "
                         "between neighbours, which on this comet's enlarged core is under a hundredth";
    cases.push_back(drizzledBlend);

    // ---- What the workflow counted from the rejection maps, and the spatial statistics beside it.
    const std::string mapsText = "the workflow's rejection-map counts: elements reaching the threshold on the low map, "
                                 "on the high map and on either, the union's fraction of the map, and the clusters "
                                 "both maps hold";
    StackingNumericsCase maps =
        base("rejection.counts", "rejection-low-mono-uint32", StackingNumericsCaseKind::RejectionMaps,
             mapsText + ", at the product's threshold of any rejection", at(kStackingSource, 1041));
    maps.second = "rejection-high-mono-uint32";
    maps.frames = 6u;
    maps.minimum = 1u;
    maps.classification = kExact;
    cases.push_back(maps);

    StackingNumericsCase twice = maps;
    twice.operation = "rejection.counts-at-two";
    twice.text = mapsText + ", counting elements rejected at least twice";
    twice.minimum = 2u;
    cases.push_back(twice);

    StackingNumericsCase colourMaps = maps;
    colourMaps.input = "rejection-low-rgb-uint32";
    colourMaps.second = "rejection-high-rgb-uint32";
    colourMaps.text = mapsText + ", over three channels, at the product's threshold";
    cases.push_back(colourMaps);

    // ---- The drizzle canvas the three workflow stages computed.
    const std::vector<CanvasCaseFrame> shifted{
        {.dx = 0.0, .dy = 0.0}, {.dx = 3.25, .dy = -1.5}, {.dx = -2.75, .dy = 4.0}};
    const std::string canvasText = "the drizzle stage's output canvas: the frames' mapped rectangles combined by the "
                                   "alignment mode, enlarged by the scale, an intersection's corners on whole pixels "
                                   "and its edges guarded";
    StackingNumericsCase standard =
        base("canvas.standard-x2", "drizzle-frame-mono-float32", StackingNumericsCaseKind::DrizzleCanvas,
             canvasText + "; the reference frame's rectangle at a scale of two", at(kDrizzleSource, 1241));
    standard.mode = "standard";
    standard.scale = 2.0;
    standard.pixelFraction = 0.9;
    standard.canvasFrames = shifted;
    standard.classification = kExact;
    cases.push_back(standard);

    StackingNumericsCase mosaic = standard;
    mosaic.operation = "canvas.mosaic-x1.5";
    mosaic.text = canvasText + "; the union of every frame's rectangle at a scale of one and a half";
    mosaic.mode = "mosaic";
    mosaic.scale = 1.5;
    cases.push_back(mosaic);

    StackingNumericsCase intersection = standard;
    intersection.operation = "canvas.intersection-x2";
    intersection.text = canvasText + "; the intersection at a scale of two, guarded";
    intersection.mode = "intersection";
    cases.push_back(intersection);

    StackingNumericsCase unitIntersection = intersection;
    unitIntersection.operation = "canvas.intersection-x1";
    unitIntersection.text = canvasText + "; the intersection at a scale of one, which loses a pixel and keeps no guard";
    unitIntersection.scale = 1.0;
    unitIntersection.pixelFraction = 1.0;
    cases.push_back(unitIntersection);

    StackingNumericsCase rotated = standard;
    rotated.operation = "canvas.mosaic-rotated-x2";
    rotated.text = canvasText + "; the union of rotated frames at a scale of two";
    rotated.mode = "mosaic";
    rotated.canvasFrames = {
        {.dx = 0.0, .dy = 0.0}, {.dx = 6.5, .dy = -3.25, .degrees = 2.0}, {.dx = -4.0, .dy = 5.5, .degrees = -5.0}};
    rotated.classification = kAccepted;
    rotated.acceptedReason = "the record maps a frame's edge as integer-centred numbers and the engine as "
                             "half-centred ones, so a rotated frame's rectangle moves by half a pixel through the "
                             "rotation and an extent can round to the next pixel";
    rotated.acceptedBound = 0.1;
    rotated.note = "a rotation of five degrees moves a corner by under a tenth of a pixel; on these frames no extent "
                   "crosses a whole output pixel, where another set of frames could";
    cases.push_back(rotated);

    StackingNumericsCase plane =
        base("canvas.comet-plane-x2", "drizzle-frame-mono-float32", StackingNumericsCaseKind::DrizzleCanvas,
             "the drizzle stage's canvas on a comet-aligned output plane: its extent the "
             "nearest whole number of pixels to the plane times the scale, unguarded",
             at(kDrizzleSource, 1357));
    plane.mode = "standard";
    plane.scale = 2.0;
    plane.pixelFraction = 0.9;
    plane.canvasFrames = shifted;
    plane.plane = std::array<double, 4>{-12.25, 4.5, 99.25, 66.25};
    plane.classification = kExact;
    cases.push_back(plane);

    // ---- The standalone stacking's exposure and signal-to-noise weights.
    StackingNumericsCase exposure =
        base("weighting.exposure", "weighting-frame-mono-float32", StackingNumericsCaseKind::Weighting,
             "the standalone stacking's exposure weights: each frame's exposure in seconds "
             "over the mean",
             at(kStandaloneSource, 772));
    exposure.weighting = "exposure";
    exposure.figures = {30.0, 60.0, 120.0, 45.0, 90.0, 300.0};
    exposure.classification = kTolerance;
    exposure.absoluteTolerance = 1.0e-14;
    exposure.note = "the record multiplies by the count over the sum and the engine divides by the mean, which can "
                    "part in the last bit of a weight";
    cases.push_back(exposure);

    StackingNumericsCase snr = exposure;
    snr.operation = "weighting.snr";
    snr.text = "the standalone stacking's signal-to-noise weights: each frame's measured ratio over the mean";
    snr.source = at(kStandaloneSource, 879);
    snr.weighting = "snr";
    snr.figures = {12.5, 20.0, 8.0, 15.25, 30.0, 9.75};
    cases.push_back(snr);

    return cases;
}

} // namespace

const std::vector<FixtureImage>& stackingNumericsInputs()
{
    static const std::vector<FixtureImage> inputs = buildInputs();
    return inputs;
}

const FixtureImage* stackingNumericsInput(std::string_view name)
{
    for (const FixtureImage& image : stackingNumericsInputs())
    {
        if (image.name == name)
        {
            return &image;
        }
    }
    return nullptr;
}

const std::vector<StackingNumericsCase>& stackingNumericsCases()
{
    static const std::vector<StackingNumericsCase> cases = build();
    return cases;
}

const StackingNumericsCase* stackingNumericsCase(std::string_view id)
{
    for (const StackingNumericsCase& item : stackingNumericsCases())
    {
        if (item.id() == id)
        {
            return &item;
        }
    }
    return nullptr;
}

const char* stackingNumericsExpectationFileOf(const StackingNumericsCase& item)
{
    switch (item.kind)
    {
    case StackingNumericsCaseKind::CometSplit:
    case StackingNumericsCaseKind::CometMask:
    case StackingNumericsCaseKind::CometBlend:
        return kCometCompositeExpectationFile;
    case StackingNumericsCaseKind::RejectionMaps:
    case StackingNumericsCaseKind::DrizzleCanvas:
    case StackingNumericsCaseKind::Weighting:
        break;
    }
    return kStackingNumericsExpectationFile;
}

bool stackingNumericsCanvasModeKnown(std::string_view mode) noexcept
{
    return mode == "standard" || mode == "mosaic" || mode == "intersection";
}

} // namespace an::tests::fixtures
