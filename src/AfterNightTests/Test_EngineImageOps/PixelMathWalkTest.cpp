// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Differential tests holding the whole-image expression walk to the per-sample definition.
//
// Forcing `ComputeBackend::Reference` makes `executePixelMath` evaluate one sample at a time through
// the instruction loop `evaluatePixelMathPixel` runs. Every expression here is run over the same
// images on both backends, and the two have to agree bit for bit: every sample written, every sample
// left alone, and every count. The expressions reach every instruction and every function the
// language has; the images are planar, interleaved, single-channel and integer, wider than any run
// of samples the walk would take at once and not a multiple of one, and hold signed zeros, the
// infinities and not-a-numbers.

#include <AfterNightEngine/Algorithms/PixelMath/PixelMathEvaluator.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathExecution.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathParser.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathProgram.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathResolution.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

constexpr std::uint32_t kHeight = 3u;
constexpr std::uint32_t kChannels = 3u;

[[nodiscard]] an::EngineContext makeEngine(an::MemoryResource* resource = nullptr)
{
    an::EngineContextOptions options;
    options.maxConcurrency = 4u;
    options.memoryResource = resource;
    an::Result<an::EngineContext> made = an::EngineContext::create(options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::EngineContext{};
}

[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine, std::uint32_t concurrency,
                                                 an::ComputeBackend backend)
{
    an::ExecutionOptions options;
    options.maxConcurrency = concurrency;
    options.backend = backend;
    an::Result<an::ExecutionContext> made = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    return made.hasValue() ? std::move(*made) : an::ExecutionContext{};
}

/// One image the tests own, in any representation and arrangement, with its bytes zeroed.
struct Frame
{
    an::SampleType type = an::SampleType::Float32;
    an::ImageLayout layout;
    std::vector<std::uint8_t> bytes;

    [[nodiscard]] std::uint64_t offsetOf(std::uint32_t channel, std::uint32_t x, std::uint32_t y) const
    {
        const std::uint32_t perPixel = layout.samplesPerPixel();
        return static_cast<std::uint64_t>(channel / perPixel) * layout.planeStrideBytes() +
               static_cast<std::uint64_t>(y) * layout.rowStrideBytes() +
               (static_cast<std::uint64_t>(x) * perPixel + channel % perPixel) * layout.elementSizeBytes();
    }

    void set(std::uint32_t channel, std::uint32_t x, std::uint32_t y, double value)
    {
        std::uint8_t* const at = bytes.data() + offsetOf(channel, x, y);
        if (type == an::SampleType::Float32)
        {
            const float narrowed = static_cast<float>(value);
            std::memcpy(at, &narrowed, sizeof(narrowed));
        }
        else
        {
            const std::uint16_t code = static_cast<std::uint16_t>(value);
            std::memcpy(at, &code, sizeof(code));
        }
    }

    [[nodiscard]] an::PixelMathImageView view() const
    {
        return an::PixelMathImageView{type, bytes.data(), &layout};
    }

    [[nodiscard]] an::PixelMathMutableImageView mutableView()
    {
        return an::PixelMathMutableImageView{type, bytes.data(), &layout};
    }

    [[nodiscard]] an::PixelMathImageDeclaration declaration() const
    {
        return an::PixelMathImageDeclaration{layout.extent(), type, an::NumericDomain::NormalizedLinear};
    }
};

/// A frame of `channels` channels, interleaved when `interleaved` is set and otherwise one plane per
/// channel, with two elements of padding at the end of every row.
[[nodiscard]] Frame makeFrame(std::uint32_t width, std::uint32_t channels, an::SampleType type, bool interleaved)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, kHeight, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    const std::uint32_t element = type == an::SampleType::Float32 ? 4u : 2u;
    const std::uint32_t perPixel = interleaved ? channels : 1u;
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = element,
                                         .samplesPerPixel = perPixel,
                                         .planeCount = channels / perPixel,
                                         .rowAlignmentBytes = 1u};
    const std::uint64_t rowStride = (static_cast<std::uint64_t>(width) * perPixel + 2u) * element;
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(request, rowStride, rowStride * kHeight, an::defaultEngineLimits());
    EXPECT_TRUE(layout.has_value());
    return Frame{type, *layout, std::vector<std::uint8_t>(layout->totalBytes(), 0u)};
}

/// The four images every expression may read: a planar colour target holding the awkward values, an
/// interleaved colour image, a single-channel image and an integer one.
struct Images
{
    Frame target;
    Frame interleaved;
    Frame single;
    Frame codes;

    explicit Images(std::uint32_t width)
        : target(makeFrame(width, kChannels, an::SampleType::Float32, false)),
          interleaved(makeFrame(width, kChannels, an::SampleType::Float32, true)),
          single(makeFrame(width, 1u, an::SampleType::Float32, false)),
          codes(makeFrame(width, kChannels, an::SampleType::UInt16, false))
    {
        constexpr double kInfinity = std::numeric_limits<double>::infinity();
        constexpr double kNotANumber = std::numeric_limits<double>::quiet_NaN();
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
                {
                    const double phase = static_cast<double>(x * 7u + y * 131u + channel * 17u);
                    target.set(channel, x, y, std::fmod(phase * 0.0137, 2.0) - 0.5);
                    interleaved.set(channel, x, y, std::fmod(phase * 0.0071, 1.0) + 0.05);
                    codes.set(channel, x, y, std::fmod(phase * 97.0, 65536.0));
                }
                single.set(0u, x, y, std::fmod(static_cast<double>(x + y * 3u) * 0.0213, 0.9));
            }
        }
        const std::array<double, 6u> awkward{-0.0, 0.0, kInfinity, -kInfinity, kNotANumber, 3.0e38};
        for (std::size_t index = 0u; index < awkward.size(); ++index)
        {
            const std::uint32_t x = static_cast<std::uint32_t>((index * 53u + 11u) % width);
            target.set(static_cast<std::uint32_t>(index % kChannels), x, static_cast<std::uint32_t>(index % kHeight),
                       awkward[index]);
        }
    }

    [[nodiscard]] std::array<an::PixelMathImageView, 4u> views() const
    {
        return {target.view(), interleaved.view(), single.view(), codes.view()};
    }

    void bind(an::PixelMathBindings& bindings) const
    {
        ASSERT_TRUE(bindings.bindImage("T", target.declaration()).ok());
        ASSERT_TRUE(bindings.bindImage("B", interleaved.declaration()).ok());
        ASSERT_TRUE(bindings.bindImage("M", single.declaration()).ok());
        ASSERT_TRUE(bindings.bindImage("U", codes.declaration()).ok());
        ASSERT_TRUE(bindings.bindMetaSymbol(an::PixelMathMetaSymbol::Target, "T").ok());
        ASSERT_TRUE(bindings.declareVariable("k", 0.5).ok());
        ASSERT_TRUE(bindings.declareVariable("v", -2.0).ok());
    }
};

/// A compiled expression, and which bindings its images are, in the order an execution binds them.
struct Compiled
{
    an::PixelMathProgram program;
    std::vector<std::uint32_t> slots;

    /// The views an execution binds, with the target's replaced by `target`.
    [[nodiscard]] std::vector<an::PixelMathImageView> viewsOf(const std::array<an::PixelMathImageView, 4u>& bound,
                                                              const an::PixelMathImageView& target) const
    {
        std::vector<an::PixelMathImageView> views;
        for (const std::uint32_t slot : slots)
        {
            views.push_back(slot == 0u ? target : bound[slot]);
        }
        return views;
    }
};

/// Parse, resolve and compile, failing the case with the reason when any of them refuse.
[[nodiscard]] std::optional<Compiled> compile(std::string_view source, const an::PixelMathBindings& bindings)
{
    const an::PixelMathParse parsed = an::parsePixelMath(source);
    if (!parsed.succeeded())
    {
        ADD_FAILURE() << source << ": " << an::describePixelMathDiagnostic(parsed.diagnostics()[0]);
        return std::nullopt;
    }
    const an::PixelMathResolution resolved = an::resolvePixelMath(parsed.tree(), bindings);
    if (!resolved.succeeded())
    {
        ADD_FAILURE() << source << ": " << an::describePixelMathDiagnostic(resolved.diagnostics()[0]);
        return std::nullopt;
    }
    an::Result<an::PixelMathProgram> compiled = an::compilePixelMath(resolved);
    if (!compiled.hasValue())
    {
        ADD_FAILURE() << source << ": " << compiled.status().message();
        return std::nullopt;
    }
    return Compiled{std::move(*compiled),
                    std::vector<std::uint32_t>(resolved.imageSlots().begin(), resolved.imageSlots().end())};
}

/// Every instruction and every function the interpreter evaluates, in expressions whose operands come
/// from the images, so that each lane of a run sees a different value. The last two branch, so their
/// samples take different instructions from their neighbours. The region tests read stated bounds that
/// differ from lane to lane.
constexpr std::array<std::string_view, 21u> kExpressions{{
    "$T * 0.5 + 0.25",
    "B - $T / 3 + M * U / 65535",
    "-$T + ~B - !M + +U",
    "$T ^ 2 + B % 0.3 - (M - $T) ^ -1",
    "($T > 0.5) + ($T <= B) * 2 + ($T == B) * 4 + ($T != M) * 8 + ($T >= U) * 16 + ($T < 0) * 32 + "
    "($T && B) * 64 + (M || 0) * 128",
    "x() * 100 + y() * 10 + c() + X() - Y()",
    "abs($T) + acos(M) + acosh(B + 1) + asin(M) + asinh($T) + atan(B) + atan2($T, B) + atanh(M * 0.5)",
    "ceil($T * 7) + floor(B * 7) + trunc(M * 7) + sign($T - 0.5) + sqrt(B) + exp(M) + ln(B) + log(B) + log2(B)",
    "cos($T) + cosh(B) + sin(M) + sinh($T) + tan(B) + tanh(M)",
    "clamp($T, 0.2, 0.8) + range(B, 0.4, 0.1) + mtf(0.25, B) + fac(M * 10) + ncr(9, M * 9) + npr(8, B * 8)",
    "min($T, B, M) + max($T, B, M, U) + sum($T, B) + mean($T, B, M) + med($T, B, M) + med($T, B, M, U)",
    "var($T, B, M) + sdev($T, B, M, 0.5) + mad($T, B, M, U) + mdev($T, B) + asqr($T, B) + ssqr($T, B, M) + "
    "bwmv($T, B, M, U, 0.25)",
    "$T - med(B) + mean(M) * max(U) - sdev(B)",
    "$T + random() * 0.1 - random() * 0.01",
    "k = $T * 2; v += k * B; k + v * M",
    "pixel(B, x() + 1, y()) + pixel($T, x() - 1, y() + 1, 2) + p(M, x() * 0.5, 0) + pixel(U, x(), 9, 0)",
    "CIEL($T) + CIEa(B) + CIEc($T) + CIEX(B) + HSLh($T) + HSVs(B) + YCbCrCb($T)",
    "w() + h() * 10 + n() + iscolor() + pi() * e()",
    "inrect(1, B * 3, 4, 2) + inellipse(3, 2, 2.5 + M, 1.5) * 2",
    "iif($T > 0.5, $T, B) + iif(M < 0.2, random(), 0)",
    "k = iif(M > 0.4, $T, -$T); k * 3",
}};

/// What one variant of an execution asks for beyond the expression.
struct Variant
{
    const char* name;
    an::ValidityRule rule;
    std::optional<std::uint32_t> destinationChannel;
    an::SampleType destinationType;
    bool inPlace;
    bool interleavedDestination;
};

constexpr std::array<Variant, 7u> kVariants{{
    {"every pixel", an::ValidityRule::AllValid, std::nullopt, an::SampleType::Float32, false, false},
    {"finite pixels only", an::ValidityRule::FiniteOnly, std::nullopt, an::SampleType::Float32, false, false},
    {"a packed mask", an::ValidityRule::ExplicitMask, std::nullopt, an::SampleType::Float32, false, false},
    {"one stated channel", an::ValidityRule::AllValid, 2u, an::SampleType::Float32, false, false},
    {"sixteen-bit codes", an::ValidityRule::FiniteOnly, std::nullopt, an::SampleType::UInt16, false, false},
    {"an interleaved destination", an::ValidityRule::ExplicitMask, std::nullopt, an::SampleType::Float32, false, true},
    {"in place", an::ValidityRule::FiniteOnly, std::nullopt, an::SampleType::Float32, true, false},
}};

/// One execution's written image and its counts.
struct Outcome
{
    std::vector<std::uint8_t> written;
    an::PixelMathExecutionResult result;
};

/// Runs one expression over the images under one variant on one backend and width.
[[nodiscard]] std::optional<Outcome> execute(const Images& images, const Compiled& compiled, const Variant& variant,
                                             const an::EngineContext& engine, std::uint32_t concurrency,
                                             an::ComputeBackend backend)
{
    const std::uint32_t width = images.target.layout.extent().width();
    Frame target = images.target;
    Frame destination = makeFrame(width, kChannels, variant.destinationType, variant.interleavedDestination);
    Frame& written = variant.inPlace ? target : destination;
    if (!variant.inPlace)
    {
        // A marker in every sample, so a pixel the walk leaves alone is told from one it wrote.
        for (std::uint32_t y = 0u; y < kHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
                {
                    destination.set(channel, x, y, 4321.0);
                }
            }
        }
    }

    const std::vector<an::PixelMathImageView> views = compiled.viewsOf(images.views(), target.view());
    const std::optional<an::ImageExtent> maskExtent =
        an::ImageExtent::create(width, kHeight, 1u, an::defaultEngineLimits());
    std::vector<std::uint8_t> maskBytes(static_cast<std::size_t>((width + 7u) / 8u) * kHeight, 0u);
    const std::optional<an::ValidityMaskView<std::uint8_t>> mask =
        an::ValidityMaskView<std::uint8_t>::createTight(maskBytes, *maskExtent);
    EXPECT_TRUE(mask.has_value());
    for (std::uint32_t y = 0u; y < kHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            // Runs of set and clear bits of several lengths, so the accepted pixels of a row are
            // neither all nor none of any run the walk takes.
            static_cast<void>(mask->setValid(0u, x, y, (x / (1u + y * 3u)) % 3u != 1u && x % 29u != 5u));
        }
    }
    const an::ValidityMaskView<const std::uint8_t> constMask = mask->constView();

    an::PixelMathExecutionRequest request;
    request.images = views;
    request.destination = written.mutableView();
    request.destinationChannel = variant.destinationChannel;
    request.randomSeed = 97u;
    const std::optional<an::ValidityDescriptor> descriptor =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = variant.rule});
    EXPECT_TRUE(descriptor.has_value());
    request.validity.descriptor = *descriptor;
    if (variant.rule == an::ValidityRule::ExplicitMask)
    {
        request.validity.mask = &constMask;
    }

    const an::ExecutionContext execution = makeExecution(engine, concurrency, backend);
    const an::Result<an::PixelMathExecutionResult> ran = an::executePixelMath(execution, compiled.program, request);
    if (!ran.hasValue())
    {
        ADD_FAILURE() << variant.name << ": " << ran.status().message();
        return std::nullopt;
    }
    return Outcome{written.bytes, *ran};
}

void expectSameCounts(const an::PixelMathExecutionResult& produced, const an::PixelMathExecutionResult& expected)
{
    EXPECT_EQ(produced.run.selection.total, expected.run.selection.total);
    EXPECT_EQ(produced.run.selection.valid, expected.run.selection.valid);
    EXPECT_EQ(produced.run.outputsWritten, expected.run.outputsWritten);
    EXPECT_EQ(produced.conversion.samplesConverted, expected.conversion.samplesConverted);
    EXPECT_EQ(produced.conversion.nonFiniteInputs, expected.conversion.nonFiniteInputs);
    EXPECT_EQ(produced.conversion.saturatedSamples, expected.conversion.saturatedSamples);
    EXPECT_EQ(produced.statisticPasses, expected.statisticPasses);
}

/// A resource that hands out ordinary aligned memory until it is told to refuse, and counts what it
/// handed out.
class RefusingMemoryResource final : public an::MemoryResource
{
public:
    an::Result<void*> allocate(std::uint64_t bytes, std::uint64_t alignment) override
    {
        if (m_refusing)
        {
            ++m_refused;
            return an::Status::failure(an::ErrorCode::AllocationFailed,
                                       an::StatusDetail{.message = "the test resource refuses every allocation"});
        }
        ++m_allocations;
        m_liveBytes += bytes;
        return ::operator new(static_cast<std::size_t>(bytes), std::align_val_t{static_cast<std::size_t>(alignment)});
    }

    void deallocate(void* pointer, std::uint64_t bytes, std::uint64_t alignment) noexcept override
    {
        ::operator delete(pointer, std::align_val_t{static_cast<std::size_t>(alignment)});
        m_liveBytes -= bytes;
    }

    an::MemoryCounters counters() const noexcept override
    {
        an::MemoryCounters counters;
        counters.liveBytes = m_liveBytes;
        counters.totalAllocations = m_allocations;
        counters.deniedAllocations = m_refused;
        return counters;
    }

    void refuse(bool refusing) noexcept
    {
        m_refusing = refusing;
    }

private:
    bool m_refusing = false;
    std::uint64_t m_allocations = 0u;
    std::uint64_t m_refused = 0u;
    std::uint64_t m_liveBytes = 0u;
};

} // namespace

TEST(PixelMathWalkTest, EveryExpressionComputesOverAWholeImageWhatItComputesOneSampleAtATime)
{
    const an::EngineContext engine = makeEngine();
    // Wider than a run of samples of any length the walk might take, and a multiple of none of the
    // likely ones; then narrower than a run.
    for (const std::uint32_t width : {301u, 5u})
    {
        const Images images(width);
        an::PixelMathBindings bindings;
        images.bind(bindings);
        for (const std::string_view source : kExpressions)
        {
            const std::optional<Compiled> compiled = compile(source, bindings);
            if (!compiled.has_value())
            {
                continue;
            }
            for (const Variant& variant : kVariants)
            {
                if (variant.inPlace && !compiled->program.supportsInPlace())
                {
                    continue;
                }
                SCOPED_TRACE(std::string(source) + " | " + variant.name + " | width " + std::to_string(width));
                const std::optional<Outcome> expected =
                    execute(images, *compiled, variant, engine, 1u, an::ComputeBackend::Reference);
                ASSERT_TRUE(expected.has_value());
                for (const std::uint32_t concurrency : {1u, 4u})
                {
                    SCOPED_TRACE("workers " + std::to_string(concurrency));
                    const std::optional<Outcome> produced =
                        execute(images, *compiled, variant, engine, concurrency, an::ComputeBackend::Automatic);
                    ASSERT_TRUE(produced.has_value());
                    EXPECT_TRUE(produced->written == expected->written)
                        << "the written image differs from the per-sample definition's";
                    expectSameCounts(produced->result, expected->result);
                }
            }
        }
    }
}

TEST(PixelMathWalkTest, EveryWrittenSampleIsThePixelEvaluationNarrowedByTheConversionRule)
{
    // Held to the two published definitions the walks are built from, one sample at a time: what
    // `evaluatePixelMathPixel` computes there, narrowed by `convertSample` as a code value. Both
    // walks store through one narrowing, so comparing them with each other alone could not see it.
    const an::EngineContext engine = makeEngine();
    const std::uint32_t width = 301u;
    const Images images(width);
    an::PixelMathBindings bindings;
    images.bind(bindings);
    const an::ConversionPolicy codeValues{.domain = an::DomainMapping::CodeValue};
    for (const std::string_view source : kExpressions)
    {
        const std::optional<Compiled> compiled = compile(source, bindings);
        if (!compiled.has_value() || !compiled->program.statistics().empty())
        {
            continue;
        }
        const std::vector<an::PixelMathImageView> views = compiled->viewsOf(images.views(), images.target.view());
        const an::Result<an::PixelMathColorPlans> plans = an::planPixelMathColors(compiled->program);
        ASSERT_TRUE(plans.hasValue()) << plans.status().message();
        std::vector<double> stack((std::max)(compiled->program.stackDepth(), std::uint32_t{1}), 0.0);
        std::vector<double> variables((std::max)(compiled->program.variableInitialValues().size(), std::size_t{1}),
                                      0.0);
        const an::PixelMathEvaluationScratch scratch{.variables = variables, .stack = stack};

        for (const an::SampleType type : {an::SampleType::Float32, an::SampleType::UInt16})
        {
            for (const an::ComputeBackend backend : {an::ComputeBackend::Automatic, an::ComputeBackend::Reference})
            {
                SCOPED_TRACE(std::string(source) + (type == an::SampleType::Float32 ? " | Float32" : " | UInt16") +
                             (backend == an::ComputeBackend::Reference ? " | reference" : " | automatic"));
                const Variant variant{"every pixel", an::ValidityRule::AllValid, std::nullopt, type, false, false};
                const std::optional<Outcome> produced = execute(images, *compiled, variant, engine, 4u, backend);
                ASSERT_TRUE(produced.has_value());
                const Frame shape = makeFrame(width, kChannels, type, false);
                std::uint64_t differing = 0u;
                std::uint64_t nonFinite = 0u;
                std::uint64_t saturated = 0u;
                for (std::uint32_t y = 0u; y < kHeight; ++y)
                {
                    for (std::uint32_t x = 0u; x < width; ++x)
                    {
                        for (std::uint32_t channel = 0u; channel < kChannels; ++channel)
                        {
                            const an::PixelMathEvaluationSite site{x, y, channel, 97u};
                            const an::Result<double> value =
                                an::evaluatePixelMathPixel(compiled->program, views, {}, *plans, site, scratch);
                            ASSERT_TRUE(value.hasValue()) << value.status().message();
                            const std::uint8_t* const at = produced->written.data() + shape.offsetOf(channel, x, y);
                            if (type == an::SampleType::Float32)
                            {
                                const an::ConvertedSample<float> expected =
                                    an::convertSample<double, float>(*value, codeValues);
                                differing += std::memcmp(at, &expected.value, sizeof(expected.value)) != 0 ? 1u : 0u;
                                nonFinite += expected.sourceWasNonFinite ? 1u : 0u;
                                saturated += expected.saturated ? 1u : 0u;
                            }
                            else
                            {
                                const an::ConvertedSample<std::uint16_t> expected =
                                    an::convertSample<double, std::uint16_t>(*value, codeValues);
                                differing += std::memcmp(at, &expected.value, sizeof(expected.value)) != 0 ? 1u : 0u;
                                nonFinite += expected.sourceWasNonFinite ? 1u : 0u;
                                saturated += expected.saturated ? 1u : 0u;
                            }
                        }
                    }
                }
                EXPECT_EQ(differing, 0u) << "samples that are not the narrowed pixel evaluation";
                EXPECT_EQ(produced->result.conversion.nonFiniteInputs, nonFinite);
                EXPECT_EQ(produced->result.conversion.saturatedSamples, saturated);
            }
        }
    }
}

TEST(PixelMathWalkTest, TheWalksWorkingMemoryIsTakenFromTheEngineContext)
{
    RefusingMemoryResource resource;
    const an::EngineContext engine = makeEngine(&resource);
    const Images images(40u);
    an::PixelMathBindings bindings;
    images.bind(bindings);

    // One expression the walk evaluates a run of samples at a time and one it evaluates a sample at
    // a time, because each branch per pixel.
    for (const std::string_view source : {"$T * 2 + B", "iif($T > 0.5, $T, B)"})
    {
        SCOPED_TRACE(source);
        const std::optional<Compiled> compiled = compile(source, bindings);
        ASSERT_TRUE(compiled.has_value());
        Frame destination = makeFrame(40u, kChannels, an::SampleType::Float32, false);
        const std::vector<an::PixelMathImageView> views = compiled->viewsOf(images.views(), images.target.view());
        an::PixelMathExecutionRequest request;
        request.images = views;
        request.destination = destination.mutableView();

        for (const an::ComputeBackend backend : {an::ComputeBackend::Automatic, an::ComputeBackend::Reference})
        {
            const an::ExecutionContext execution = makeExecution(engine, 4u, backend);
            const std::uint64_t before = resource.counters().totalAllocations;
            ASSERT_TRUE(an::executePixelMath(execution, compiled->program, request).hasValue());
            EXPECT_GT(resource.counters().totalAllocations, before) << "the walk's scratch is the context's";

            resource.refuse(true);
            const an::Result<an::PixelMathExecutionResult> refused =
                an::executePixelMath(execution, compiled->program, request);
            resource.refuse(false);
            ASSERT_FALSE(refused.hasValue()) << "a refused scratch is a returned failure, not a smaller walk";
            EXPECT_EQ(refused.status().code(), an::ErrorCode::AllocationFailed);
        }
    }
    EXPECT_EQ(resource.counters().liveBytes, 0u) << "every block went back";
}
