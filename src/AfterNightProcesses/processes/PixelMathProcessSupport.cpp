// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The PixelMath window's run: the dialog's symbols and settings file, the binding policy that turns the images an
/// expression names into the engine's bindings, and each expression parsed, resolved, compiled and run as the engine's
/// fused pointwise program when it lowers and through the engine's evaluator when it does not.

#include "stdafx.hpp"

#include "processes/PixelMathProcessSupport.h"

#include "processes/GeometryProcessSupport.h"
#include "processes/StretchProcessSupport.h"
#include "session/ImageReads.h"

#include <AfterNightEngine/Algorithms/Masks/MaskOperations.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathDiagnostic.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathExecution.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathParser.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathProgram.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathResolution.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/ImageTransfer.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QFile>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <fmt/format.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>

namespace an::ui::processes
{

namespace
{

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, an::StatusDetail{.message = std::move(message)});
}

[[nodiscard]] std::string_view trimmedView(std::string_view text) noexcept
{
    constexpr std::string_view kBlank = " \t\r\n";
    const std::size_t first = text.find_first_not_of(kBlank);
    if (first == std::string_view::npos)
    {
        return {};
    }
    const std::size_t last = text.find_last_not_of(kBlank);
    return text.substr(first, last - first + 1u);
}

/// A list split at a separator, every piece kept, empty ones included.
[[nodiscard]] std::vector<std::string_view> splitAt(std::string_view text, char separator)
{
    std::vector<std::string_view> pieces;
    std::size_t start = 0u;
    while (true)
    {
        const std::size_t at = text.find(separator, start);
        if (at == std::string_view::npos)
        {
            pieces.push_back(text.substr(start));
            return pieces;
        }
        pieces.push_back(text.substr(start, at - start));
        start = at + 1u;
    }
}

/// The number a text starts with, read in the invariant locale, or nothing.
[[nodiscard]] std::optional<double> leadingNumber(std::string_view text) noexcept
{
    if (!text.empty() && text.front() == '+')
    {
        text.remove_prefix(1u);
    }
    double value = 0.0;
    const std::from_chars_result read = std::from_chars(text.data(), text.data() + text.size(), value);
    if (read.ec != std::errc() || read.ptr == text.data())
    {
        return std::nullopt;
    }
    return value;
}

/// Whether a numeric domain counts fractions of full scale, linear or encoded.
[[nodiscard]] constexpr bool countsFractions(an::NumericDomain domain) noexcept
{
    return domain == an::NumericDomain::NormalizedLinear || domain == an::NumericDomain::EncodedNonlinear;
}

/// Whether a representation needs Float64 to be read as fractions without losing a code.
[[nodiscard]] constexpr bool needsDouble(an::SampleType type) noexcept
{
    return type == an::SampleType::Float64 || type == an::SampleType::UInt32 || type == an::SampleType::Int32;
}

[[nodiscard]] constexpr bool isFloating(an::SampleType type) noexcept
{
    return type == an::SampleType::Float32 || type == an::SampleType::Float64;
}

[[nodiscard]] constexpr bool sameSize(const an::ImageDescriptor& left, const an::ImageDescriptor& right) noexcept
{
    return left.extent().width() == right.extent().width() && left.extent().height() == right.extent().height();
}

/// What an image holds, whichever its representation.
[[nodiscard]] std::optional<an::ImageDescriptor> descriptorOf(const an::consumer::AnyImage& image) noexcept
{
    return std::visit([](const auto& typed) { return typed.descriptor(); }, image);
}

/// Where an image's samples start and how they sit, whichever its representation.
struct SamplesOf
{
    const void* data = nullptr;
    std::optional<an::ImageLayout> layout;
};

[[nodiscard]] SamplesOf samplesOf(const an::consumer::AnyImage& image) noexcept
{
    return std::visit(
        [](const auto& typed) -> SamplesOf {
            const auto view = typed.view();
            if (!view.has_value())
            {
                return {};
            }
            return SamplesOf{.data = view->data(), .layout = view->layout()};
        },
        image);
}

// ---- The images a run binds.

/// The names the target, `$A` and `$M` are bound under when no view's name reads them: spellings an expression cannot
/// write, so only the meta symbols reach them.
constexpr std::string_view kTargetBinding = "\001target";
constexpr const char* kActiveBinding = "\001active";
constexpr const char* kMaskBinding = "\001mask";

/// One image a run reads, held for the run: its samples as fractions of full scale in the working representation,
/// planar, at the output's width and height.
struct BoundWorkingImage
{
    /// The name an expression reads it by; empty for the target, which every expression reads as `$T`.
    std::string name;

    /// The fractions.
    an::consumer::AnyImage image;

    /// What the engine is told it is.
    an::PixelMathImageDeclaration declaration;
};

/// What a run reads an image from: a source the caller leases, and the leases and copies made on the way to the
/// working image, which must live while it is read.
struct LeasedSource
{
    std::optional<an::consumer::AssetReadLease> lease;
    std::optional<an::consumer::ImageAsset> resampled;
    std::optional<an::consumer::AssetReadLease> resampledLease;
    std::optional<an::ui::ProcessDestination> planar;
};

/// Reads an image as fractions of full scale in the working representation, planar: its samples as they are, or a
/// planar copy of them first when they are not planar, converted as `readAsFractions` converts.
template <typename WorkT>
[[nodiscard]] an::Result<an::consumer::AnyImage> fractionsOf(const an::ui::ExecutionScope& scope,
                                                             const an::ui::ProcessSource& source,
                                                             an::NumericDomain declaredDomain, LeasedSource& held)
{
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    SamplesOf samples = samplesOf(source.image());
    if (!described.has_value() || samples.data == nullptr || !samples.layout.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "an image the expression names holds nothing");
    }
    if (an::storageArrangementOf(*samples.layout) != an::StorageArrangement::Planar)
    {
        an::Result<an::ui::ProcessDestination> planar =
            an::ui::ProcessDestination::copyOf(scope.session(), scope.engine(), scope.execution(), source);
        if (!planar.hasValue())
        {
            return planar.status();
        }
        held.planar.emplace(std::move(*planar));
        samples = samplesOf(held.planar->asset().image());
        if (samples.data == nullptr || !samples.layout.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the planar copy of an image holds nothing");
        }
    }
    an::ImageDescriptorRequest request = described->toRequest();
    request.sampleType = an::Image<WorkT>::kSampleType;
    request.numericDomain = declaredDomain;
    const std::optional<an::ImageDescriptor> working = an::ImageDescriptor::create(request);
    if (!working.has_value())
    {
        return refusal(an::ErrorCode::IncompatibleArguments,
                       "an image the expression names cannot be described as fractions of full scale");
    }
    an::Result<an::Image<WorkT>> fractions = an::Image<WorkT>::create(
        scope.engine(), *working, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized);
    if (!fractions.hasValue())
    {
        return fractions.status();
    }
    {
        const std::optional<an::ImageWriteAccess<WorkT>> access = fractions->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the working copy holds nothing");
        }
        const an::Status read =
            readAsFractions(scope.execution(), described->sampleType(), samples.data, *samples.layout,
                            an::Image<WorkT>::kSampleType, access->view().data(), access->view().layout());
        if (read.failed())
        {
            return read;
        }
    }
    return an::consumer::AnyImage(std::move(*fractions));
}

/// Resamples an image an expression names to a width and height with Lanczos-4, in its own representation; an image of
/// that size is read as it is.
[[nodiscard]] an::Result<an::ui::ProcessSource> atOutputSize(const an::ui::ExecutionScope& scope,
                                                             const an::ui::ProcessSource& source, std::uint32_t width,
                                                             std::uint32_t height, LeasedSource& held)
{
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "an image the expression names holds nothing");
    }
    if (described->extent().width() == width && described->extent().height() == height)
    {
        return source;
    }
    const std::array<GeometryStep, 1u> steps{resizeRequest(width, height, an::KernelInterpolation::Lanczos4)};
    an::Result<an::consumer::ImageAsset> resampled = transformImage(scope, source, steps, "PixelMath", true);
    if (!resampled.hasValue())
    {
        return resampled.status();
    }
    held.resampled.emplace(std::move(*resampled));
    an::Result<an::consumer::AssetReadLease> lease = held.resampled->read(scope.execution().stopToken());
    if (!lease.hasValue())
    {
        return lease.status();
    }
    held.resampledLease.emplace(std::move(*lease));
    return an::ui::ProcessSource::of(*held.resampledLease);
}

/// The working image of a target with other channels: grey from colour by the luminance weights 0.2989, 0.5870 and
/// 0.1140, colour from grey by repeating it, an alpha channel added at zero or dropped.
template <typename WorkT>
[[nodiscard]] an::Result<an::consumer::AnyImage>
reshapedChannels(const an::ui::ExecutionScope& scope, const an::Image<WorkT>& source, const an::ImageDescriptor& wanted)
{
    const auto from = source.view();
    if (!from.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the target holds nothing");
    }
    an::Result<an::Image<WorkT>> made = an::Image<WorkT>::create(scope.engine(), wanted, an::StorageArrangement::Planar,
                                                                 an::StorageInitialization::Zeroed);
    if (!made.hasValue())
    {
        return made.status();
    }
    {
        const std::optional<an::ImageWriteAccess<WorkT>> access = made->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the reshaped target holds nothing");
        }
        const an::ImageView<WorkT>& to = access->view();
        const std::uint32_t fromChannels = from->channelCount();
        const std::uint32_t toChannels = to.channelCount();
        for (std::uint32_t y = 0u; y < to.height(); ++y)
        {
            if (an::Status stopped = scope.execution().checkCancellation(); stopped.failed())
            {
                return stopped;
            }
            const auto plane = [&](std::uint32_t channel) {
                return from->row((std::min)(channel, fromChannels - 1u), y);
            };
            if (toChannels == 1u && fromChannels >= 3u)
            {
                const std::optional<std::span<const WorkT>> red = plane(0u);
                const std::optional<std::span<const WorkT>> green = plane(1u);
                const std::optional<std::span<const WorkT>> blue = plane(2u);
                const std::optional<std::span<WorkT>> grey = to.row(0u, y);
                if (!red || !green || !blue || !grey)
                {
                    return refusal(an::ErrorCode::Internal, "a row of the target cannot be read");
                }
                for (std::uint32_t x = 0u; x < to.width(); ++x)
                {
                    (*grey)[x] = static_cast<WorkT>(0.2989 * static_cast<double>((*red)[x]) +
                                                    0.5870 * static_cast<double>((*green)[x]) +
                                                    0.1140 * static_cast<double>((*blue)[x]));
                }
                continue;
            }
            for (std::uint32_t channel = 0u; channel < toChannels; ++channel)
            {
                const std::optional<std::span<WorkT>> written = to.row(channel, y);
                if (!written)
                {
                    return refusal(an::ErrorCode::Internal, "a row of the reshaped target cannot be written");
                }
                // An alpha channel the target did not have starts at zero.
                if (channel == 3u && fromChannels < 4u)
                {
                    continue;
                }
                const std::uint32_t read = fromChannels == 1u ? 0u : channel;
                const std::optional<std::span<const WorkT>> source_row = from->row(read, y);
                if (!source_row)
                {
                    return refusal(an::ErrorCode::Internal, "a row of the target cannot be read");
                }
                std::copy(source_row->begin(), source_row->begin() + to.width(), written->begin());
            }
        }
    }
    return an::consumer::AnyImage(std::move(*made));
}

/// A copy of a working image, the destination every expression writes into: whatever no expression writes is the
/// target's.
template <typename WorkT>
[[nodiscard]] an::Result<an::Image<WorkT>> copyOfWorking(const an::ui::ExecutionScope& scope,
                                                         const an::Image<WorkT>& source)
{
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    const auto from = source.view();
    if (!described.has_value() || !from.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the target holds nothing");
    }
    an::Result<an::Image<WorkT>> copy = an::Image<WorkT>::create(
        scope.engine(), *described, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized);
    if (!copy.hasValue())
    {
        return copy.status();
    }
    {
        const std::optional<an::ImageWriteAccess<WorkT>> access = copy->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the destination holds nothing");
        }
        if (an::Status copied = an::copyImage(scope.execution(), *from, access->view()); copied.failed())
        {
            return copied;
        }
    }
    return copy;
}

// ---- The expressions.

/// One distinct expression, parsed, with the channels it writes.
struct ParsedExpression
{
    PixelMathExpressionTab tab = PixelMathExpressionTab::RedOrAll;
    std::string text;
    std::vector<std::uint32_t> channels;
    an::PixelMathParse parse;
};

/// Every diagnostic a front end raised, said for a person, under the tab the expression was written in.
[[nodiscard]] std::string describedDiagnostics(std::span<const an::PixelMathDiagnostic> diagnostics,
                                               const std::string& tabText, std::string_view source)
{
    std::string said;
    for (const an::PixelMathDiagnostic& diagnostic : diagnostics)
    {
        if (!said.empty())
        {
            said += '\n';
        }
        std::string at;
        if (diagnostic.span.length > 0u && diagnostic.span.offset < source.size())
        {
            at = fmt::format(" at '{}'", source.substr(diagnostic.span.offset, diagnostic.span.length));
        }
        said += fmt::format("{} expression{}: {}", tabText, at, an::describePixelMathDiagnostic(diagnostic));
    }
    return said;
}

/// Every name an expression reads an image or a symbol by, and which meta symbols it reads.
struct ReadNames
{
    std::set<std::string> names;
    bool active = false;
    bool mask = false;
};

void collectNames(const an::PixelMathSyntaxTree& tree, ReadNames& read)
{
    for (const an::PixelMathNode& node : tree.nodes())
    {
        if (node.kind == an::PixelMathSyntaxKind::Name)
        {
            read.names.emplace(tree.name(node.nameIndex));
        }
        else if (node.kind == an::PixelMathSyntaxKind::MetaSymbol)
        {
            read.active = read.active || node.metaSymbol == an::PixelMathMetaSymbol::Active;
            read.mask = read.mask || node.metaSymbol == an::PixelMathMetaSymbol::Mask;
        }
    }
}

/// The bindings every expression of a run is resolved against: the bound images, `$T` the first, with `$A` and `$M`
/// bound to the images they read; the local symbols as variables, the global ones as constants. With `plane`, every
/// image is declared as one channel - the plane a fused chain reads of it.
[[nodiscard]] an::Result<an::PixelMathBindings>
bindingsFor(const std::vector<BoundWorkingImage>& bound, std::optional<std::size_t> activeSlot,
            std::optional<std::size_t> maskSlot, const std::vector<PixelMathSymbol>& locals,
            const std::vector<PixelMathSymbol>& globals, const an::EngineLimits& limits, bool plane)
{
    an::PixelMathBindings bindings;
    const std::string_view targetBinding = bound.front().name.empty() ? kTargetBinding : bound.front().name;
    for (std::size_t slot = 0u; slot < bound.size(); ++slot)
    {
        an::PixelMathImageDeclaration declaration = bound[slot].declaration;
        if (plane)
        {
            const std::optional<an::ImageExtent> one =
                an::ImageExtent::create(declaration.extent.width(), declaration.extent.height(), 1u, limits);
            if (!one.has_value())
            {
                return refusal(an::ErrorCode::InvalidArgument, "a plane of an image cannot be described");
            }
            declaration.extent = *one;
        }
        const std::string_view name = slot == 0u ? targetBinding : std::string_view(bound[slot].name);
        if (an::Status bound_ = bindings.bindImage(name, declaration); bound_.failed())
        {
            return refusal(an::ErrorCode::InvalidArgument,
                           fmt::format("the image '{}' cannot be named in an expression: {}", name, bound_.message()));
        }
    }
    if (an::Status target = bindings.bindMetaSymbol(an::PixelMathMetaSymbol::Target, targetBinding); target.failed())
    {
        return target;
    }
    const auto bindMeta = [&](an::PixelMathMetaSymbol symbol, std::optional<std::size_t> slot) -> an::Status {
        if (!slot.has_value())
        {
            return an::Status::success();
        }
        return bindings.bindMetaSymbol(symbol, *slot == 0u ? targetBinding : std::string_view(bound[*slot].name));
    };
    if (an::Status active = bindMeta(an::PixelMathMetaSymbol::Active, activeSlot); active.failed())
    {
        return active;
    }
    if (an::Status mask = bindMeta(an::PixelMathMetaSymbol::Mask, maskSlot); mask.failed())
    {
        return mask;
    }
    for (const PixelMathSymbol& local : locals)
    {
        if (an::Status declared = bindings.declareVariable(local.name, local.value); declared.failed())
        {
            return refusal(an::ErrorCode::InvalidArgument,
                           fmt::format("the local symbol '{}' cannot be declared: {}", local.name, declared.message()));
        }
    }
    for (const PixelMathSymbol& global : globals)
    {
        if (an::Status declared = bindings.declareConstant(global.name, global.value); declared.failed())
        {
            return refusal(an::ErrorCode::InvalidArgument, fmt::format("the global symbol '{}' cannot be declared: {}",
                                                                       global.name, declared.message()));
        }
    }
    return bindings;
}

/// What an expression resolved and compiled to.
struct CompiledExpression
{
    an::PixelMathResolution resolution;
    an::PixelMathProgram program;
};

[[nodiscard]] an::Result<CompiledExpression> compiled(const an::PixelMathSyntaxTree& tree,
                                                      const an::PixelMathBindings& bindings, const std::string& tabText,
                                                      std::string_view source)
{
    CompiledExpression made;
    made.resolution = an::resolvePixelMath(tree, bindings);
    if (!made.resolution.succeeded())
    {
        return refusal(an::ErrorCode::InvalidArgument,
                       describedDiagnostics(made.resolution.diagnostics(), tabText, source));
    }
    an::Result<an::PixelMathProgram> program = an::compilePixelMath(made.resolution);
    if (!program.hasValue())
    {
        return refusal(program.status().code(), fmt::format("{} expression: {}", tabText, program.status().message()));
    }
    made.program = std::move(*program);
    return made;
}

/// The arithmetic a chain is lowered under: the working representation's width.
[[nodiscard]] an::PixelMathLoweringOptions loweringOptions(an::SampleType working) noexcept
{
    an::PixelMathLoweringOptions options;
    if (working == an::SampleType::Float64)
    {
        options.precision = an::ComputePrecision::Float64;
        options.precisionJustification = an::Float64Justification::StoredInFloat64;
    }
    return options;
}

/// A planar working image's samples and layout, or of one of its planes.
struct PlaneOf
{
    const void* data = nullptr;
    std::optional<an::ImageLayout> layout;
};

template <typename WorkT>
[[nodiscard]] PlaneOf planeOf(const an::Image<WorkT>& image, std::optional<std::uint32_t> plane)
{
    const auto view = image.view();
    if (!view.has_value())
    {
        return {};
    }
    if (!plane.has_value())
    {
        return PlaneOf{.data = view->data(), .layout = view->layout()};
    }
    const std::uint32_t index = view->channelCount() == 1u ? 0u : *plane;
    const auto picked = view->plane(index);
    if (!picked.has_value())
    {
        return {};
    }
    return PlaneOf{.data = picked->data(), .layout = picked->layout()};
}

template <typename WorkT>
[[nodiscard]] PlaneOf writablePlaneOf(an::Image<WorkT>& image, std::optional<std::uint32_t> plane)
{
    const std::optional<an::ImageWriteAccess<WorkT>> access = image.write();
    if (!access.has_value())
    {
        return {};
    }
    const an::ImageView<WorkT>& view = access->view();
    if (!plane.has_value())
    {
        return PlaneOf{.data = view.data(), .layout = view.layout()};
    }
    const auto picked = view.plane(*plane);
    if (!picked.has_value())
    {
        return {};
    }
    return PlaneOf{.data = picked->data(), .layout = picked->layout()};
}

/// Runs a lowered chain: its sources the bound images the lowering names, or one plane of each, into the
/// destination or one plane of it.
template <typename WorkT>
[[nodiscard]] an::Status runFused(const an::ui::ExecutionScope& scope, const an::PixelMathLowering& lowering,
                                  const an::PixelMathResolution& resolution,
                                  const std::vector<BoundWorkingImage>& bound, an::Image<WorkT>& destination,
                                  std::optional<std::uint32_t> channel, const an::KernelValidityView& validity)
{
    const std::span<const std::uint32_t> imageSlots = resolution.imageSlots();
    std::vector<PlaneOf> planes;
    planes.reserve(lowering.sourceImages().size());
    for (const std::uint32_t image : lowering.sourceImages())
    {
        if (image >= imageSlots.size() || imageSlots[image] >= bound.size())
        {
            return refusal(an::ErrorCode::Internal, "the chain names an image the run did not bind");
        }
        const auto* typed = std::get_if<an::Image<WorkT>>(&bound[imageSlots[image]].image);
        if (typed == nullptr)
        {
            return refusal(an::ErrorCode::Internal, "an image the chain reads is not in the working representation");
        }
        planes.push_back(planeOf(*typed, channel));
        if (planes.back().data == nullptr)
        {
            return refusal(an::ErrorCode::NotInitialized, "an image the chain reads holds nothing");
        }
    }
    std::vector<an::PointwiseImageView> sources;
    sources.reserve(planes.size());
    for (const PlaneOf& plane : planes)
    {
        sources.push_back(an::PointwiseImageView{.samples = plane.data, .layout = &*plane.layout});
    }
    const PlaneOf written = writablePlaneOf(destination, channel);
    if (written.data == nullptr)
    {
        return refusal(an::ErrorCode::NotInitialized, "the destination holds nothing");
    }
    an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
        scope.execution(), *lowering.program(), sources,
        an::PointwiseMutableImageView{.samples = const_cast<void*>(written.data), .layout = &*written.layout},
        validity);
    return ran.hasValue() ? an::Status::success() : ran.status();
}

/// Runs a compiled expression through the engine's evaluator, over every channel or one.
template <typename WorkT>
[[nodiscard]] an::Status runEvaluated(const an::ui::ExecutionScope& scope, const CompiledExpression& expression,
                                      const std::vector<BoundWorkingImage>& bound, an::Image<WorkT>& destination,
                                      std::optional<std::uint32_t> channel, const an::KernelValidityView& validity,
                                      std::uint64_t randomSeed)
{
    const std::span<const std::uint32_t> imageSlots = expression.resolution.imageSlots();
    std::vector<PlaneOf> planes;
    planes.reserve(imageSlots.size());
    for (const std::uint32_t slot : imageSlots)
    {
        if (slot >= bound.size())
        {
            return refusal(an::ErrorCode::Internal, "the expression names an image the run did not bind");
        }
        const auto* typed = std::get_if<an::Image<WorkT>>(&bound[slot].image);
        if (typed == nullptr)
        {
            return refusal(an::ErrorCode::Internal,
                           "an image the expression reads is not in the working representation");
        }
        planes.push_back(planeOf(*typed, std::nullopt));
        if (planes.back().data == nullptr)
        {
            return refusal(an::ErrorCode::NotInitialized, "an image the expression reads holds nothing");
        }
    }
    std::vector<an::PixelMathImageView> images;
    images.reserve(planes.size());
    for (const PlaneOf& plane : planes)
    {
        images.push_back(an::PixelMathImageView{
            .sampleType = an::Image<WorkT>::kSampleType, .samples = plane.data, .layout = &*plane.layout});
    }
    const PlaneOf written = writablePlaneOf(destination, std::nullopt);
    if (written.data == nullptr)
    {
        return refusal(an::ErrorCode::NotInitialized, "the destination holds nothing");
    }
    an::PixelMathExecutionRequest request;
    request.images = images;
    request.destination = an::PixelMathMutableImageView{.sampleType = an::Image<WorkT>::kSampleType,
                                                        .samples = const_cast<void*>(written.data),
                                                        .layout = &*written.layout};
    request.validity = validity;
    request.destinationChannel = channel;
    request.randomSeed = randomSeed;
    an::Result<an::PixelMathExecutionResult> ran = an::executePixelMath(scope.execution(), expression.program, request);
    return ran.hasValue() ? an::Status::success() : ran.status();
}

/// Maps the smallest and largest finite value of every channel to the bounds, as "Rescale result" asks.
template <typename WorkT>
[[nodiscard]] an::Status rescaled(const an::ui::ExecutionScope& scope, an::Image<WorkT>& image, double low, double high)
{
    const std::optional<an::ImageWriteAccess<WorkT>> access = image.write();
    if (!access.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the result holds nothing");
    }
    const an::ImageView<WorkT>& view = access->view();
    double smallest = std::numeric_limits<double>::infinity();
    double largest = -std::numeric_limits<double>::infinity();
    for (std::uint32_t channel = 0u; channel < view.channelCount(); ++channel)
    {
        for (std::uint32_t y = 0u; y < view.height(); ++y)
        {
            const std::optional<std::span<WorkT>> row = view.row(channel, y);
            if (!row)
            {
                return refusal(an::ErrorCode::Internal, "a row of the result cannot be read");
            }
            for (std::uint32_t x = 0u; x < view.width(); ++x)
            {
                const double value = static_cast<double>((*row)[x]);
                if (std::isfinite(value))
                {
                    smallest = (std::min)(smallest, value);
                    largest = (std::max)(largest, value);
                }
            }
        }
        if (an::Status stopped = scope.execution().checkCancellation(); stopped.failed())
        {
            return stopped;
        }
    }
    // Nothing finite, or one value everywhere, has no range to map; the result is left as it is.
    if (!(largest > smallest))
    {
        return an::Status::success();
    }
    const double scale = (high - low) / (largest - smallest);
    for (std::uint32_t channel = 0u; channel < view.channelCount(); ++channel)
    {
        for (std::uint32_t y = 0u; y < view.height(); ++y)
        {
            const std::optional<std::span<WorkT>> row = view.row(channel, y);
            if (!row)
            {
                return refusal(an::ErrorCode::Internal, "a row of the result cannot be written");
            }
            for (std::uint32_t x = 0u; x < view.width(); ++x)
            {
                const double value = static_cast<double>((*row)[x]);
                if (std::isfinite(value))
                {
                    (*row)[x] = static_cast<WorkT>((value - smallest) * scale + low);
                }
            }
        }
    }
    return an::Status::success();
}

/// What the result's metadata is: the target's, recording the process, when it replaces the target; for a new image,
/// the target's acquisition record and lineage, its solution only at the target's size, and the processing-state cards
/// alone.
[[nodiscard]] an::Result<an::ImageMetadata> resultMetadata(const an::ImageMetadata& target,
                                                           const an::ImageDescriptor& targetDescribed,
                                                           const an::ImageDescriptor& described, bool replaces,
                                                           bool keepsValidity)
{
    const an::CodecMetadata& kept = replaces ? target.codec() : an::CodecMetadata{};
    const an::ProcessingStateRecord state =
        an::nonlinearState(an::readProcessingStateCards(kept), described, kPixelMathStateOperation);
    an::Result<an::CodecMetadata> cards = an::withProcessingStateCards(kept, state);
    if (!cards.hasValue())
    {
        return cards.status();
    }
    an::ImageSemanticsRequest meaning = replaces ? target.semantics().toRequest() : an::ImageSemanticsRequest{};
    // A result is no longer the linear encoding its target may have claimed; an encoding it was in, it still is.
    if (meaning.transferFunction == an::TransferFunction::Linear)
    {
        meaning.transferFunction = an::TransferFunction::Unspecified;
        meaning.gammaExponent.reset();
    }
    meaning.validity = keepsValidity ? target.semantics().validity() : an::ValidityDescriptor::allValid();
    an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
    if (!semantics.hasValue())
    {
        return semantics.status();
    }
    const bool solutionHolds = replaces || sameSize(described, targetDescribed);
    return an::ImageMetadata::create(
        target.acquisition(), std::move(*semantics), solutionHolds ? target.astrometry() : an::AstrometricSolution{},
        target.provenance(), std::move(*cards), replaces ? target.outputIntent() : std::nullopt);
}

/// What the lineage says of a run: every expression and the path it took.
[[nodiscard]] std::string lineageDetail(const PixelMathRunReport& report, const PixelMathSettings& settings,
                                        std::uint64_t randomSeed)
{
    std::string detail;
    for (const PixelMathChannelRun& run : report.runs)
    {
        if (!detail.empty())
        {
            detail += "; ";
        }
        std::string channels;
        for (const std::uint32_t channel : run.channels)
        {
            channels += (channels.empty() ? "" : ",") + std::to_string(channel);
        }
        detail += fmt::format("channels {} '{}' {}", channels,
                              std::string(trimmedView(settings.expressions[static_cast<std::size_t>(run.tab)])),
                              run.fused ? "fused" : "evaluated");
    }
    detail += fmt::format("; seed {}", randomSeed);
    if (settings.rescale)
    {
        detail += fmt::format("; rescaled to [{}, {}]", settings.rescaleLow, settings.rescaleHigh);
    }
    return detail;
}

/// The run itself, in one working representation.
template <typename WorkT>
[[nodiscard]] an::Result<an::consumer::ImageAsset>
runIn(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& target,
      const an::ImageDescriptor& targetDescribed, const an::ImageDescriptor& output, const PixelMathSettings& settings,
      const PixelMathImageSet& images, std::vector<ParsedExpression>& expressions, const ReadNames& read,
      std::uint64_t randomSeed, PixelMathRunReport& report)
{
    const an::EngineLimits& limits = scope.engine().limits();
    const std::uint32_t width = output.extent().width();
    const std::uint32_t height = output.extent().height();
    const std::vector<PixelMathSymbol> locals = parsePixelMathSymbols(settings.localSymbols);
    const std::vector<PixelMathSymbol> globals = parsePixelMathSymbols(settings.globalSymbols);
    std::set<std::string> symbolNames;
    for (const PixelMathSymbol& symbol : locals)
    {
        symbolNames.insert(symbol.name);
    }
    for (const PixelMathSymbol& symbol : globals)
    {
        symbolNames.insert(symbol.name);
    }

    // The target, at the output's size and with its channels.
    std::vector<LeasedSource> held;
    held.reserve(3u + images.others.size());
    std::vector<BoundWorkingImage> bound;
    {
        held.emplace_back();
        an::Result<an::ui::ProcessSource> sized = atOutputSize(scope, target, width, height, held.back());
        if (!sized.hasValue())
        {
            return sized.status();
        }
        an::Result<an::consumer::AnyImage> fractions =
            fractionsOf<WorkT>(scope, *sized, targetDescribed.numericDomain(), held.back());
        if (!fractions.hasValue())
        {
            return fractions.status();
        }
        if (descriptorOf(*fractions)->extent().channelCount() != output.extent().channelCount())
        {
            an::ImageDescriptorRequest request = output.toRequest();
            request.sampleType = an::Image<WorkT>::kSampleType;
            const std::optional<an::ImageDescriptor> wanted = an::ImageDescriptor::create(request);
            if (!wanted.has_value())
            {
                return refusal(an::ErrorCode::IncompatibleArguments, "the target cannot have the output's channels");
            }
            fractions = reshapedChannels<WorkT>(scope, std::get<an::Image<WorkT>>(*fractions), *wanted);
            if (!fractions.hasValue())
            {
                return fractions.status();
            }
        }
        const an::ImageExtent extent = descriptorOf(*fractions)->extent();
        bound.push_back(BoundWorkingImage{
            .name = std::string(),
            .image = std::move(*fractions),
            .declaration = an::PixelMathImageDeclaration{.extent = extent,
                                                         .sampleType = an::Image<WorkT>::kSampleType,
                                                         .numericDomain = targetDescribed.numericDomain()}});
        report.boundImages.emplace_back("$T");
    }

    // Every other image an expression reads, by its name, `$A` and `$M`; the target's own name reads `$T`.
    const auto bindAsset = [&](const std::string& name, const std::string& reported,
                               const an::consumer::ImageAsset& asset) -> an::Status {
        held.emplace_back();
        LeasedSource& lease = held.back();
        an::Result<an::consumer::AssetReadLease> taken =
            an::ui::readBeside(scope, asset, scope.execution().stopToken());
        if (!taken.hasValue())
        {
            return taken.status();
        }
        lease.lease.emplace(std::move(*taken));
        const an::ui::ProcessSource source = an::ui::ProcessSource::of(*lease.lease);
        const std::optional<an::ImageDescriptor> described = source.descriptor();
        if (!described.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, fmt::format("the image '{}' holds nothing", name));
        }
        // Fractions of full scale are read together whatever their encoding; any other domain is declared as it is,
        // and the engine refuses it beside one it does not match.
        const an::NumericDomain domain =
            countsFractions(described->numericDomain()) && countsFractions(targetDescribed.numericDomain())
                ? targetDescribed.numericDomain()
                : described->numericDomain();
        an::Result<an::ui::ProcessSource> sized = atOutputSize(scope, source, width, height, lease);
        if (!sized.hasValue())
        {
            return sized.status();
        }
        an::Result<an::consumer::AnyImage> fractions = fractionsOf<WorkT>(scope, *sized, domain, lease);
        if (!fractions.hasValue())
        {
            return fractions.status();
        }
        const an::ImageExtent extent = descriptorOf(*fractions)->extent();
        bound.push_back(BoundWorkingImage{
            .name = name,
            .image = std::move(*fractions),
            .declaration = an::PixelMathImageDeclaration{
                .extent = extent, .sampleType = an::Image<WorkT>::kSampleType, .numericDomain = domain}});
        report.boundImages.push_back(reported);
        return an::Status::success();
    };
    for (const std::string& name : read.names)
    {
        if (name == images.targetName || symbolNames.contains(name))
        {
            continue;
        }
        const auto other = std::find_if(images.others.begin(), images.others.end(),
                                        [&name](const PixelMathNamedImage& image) { return image.name == name; });
        if (other == images.others.end() || other->asset == nullptr)
        {
            continue;
        }
        if (an::Status readOther = bindAsset(name, name, *other->asset); readOther.failed())
        {
            return readOther;
        }
    }
    std::optional<std::size_t> activeSlot;
    if (read.active)
    {
        activeSlot = 0u;
        if (images.active != nullptr)
        {
            if (an::Status readActive = bindAsset(kActiveBinding, "$A", *images.active); readActive.failed())
            {
                return readActive;
            }
            activeSlot = bound.size() - 1u;
        }
    }
    std::optional<std::size_t> maskSlot;
    if (read.mask && images.mask != nullptr)
    {
        if (an::Status readMask = bindAsset(kMaskBinding, "$M", *images.mask); readMask.failed())
        {
            return readMask;
        }
        maskSlot = bound.size() - 1u;
    }
    // The target's own name reads `$T`: the target is bound under it, so an expression that names the view reads the
    // image being written.
    if (read.names.contains(images.targetName) && !images.targetName.empty() &&
        !symbolNames.contains(images.targetName))
    {
        bound.front().name = images.targetName;
        report.boundImages.push_back(images.targetName);
    }

    an::Result<an::PixelMathBindings> bindings =
        bindingsFor(bound, activeSlot, maskSlot, locals, globals, limits, false);
    if (!bindings.hasValue())
    {
        return bindings.status();
    }

    // The destination starts as the target, so a channel no expression writes and a pixel the validity rejects keep
    // the target's samples.
    an::Result<an::Image<WorkT>> destination =
        copyOfWorking<WorkT>(scope, std::get<an::Image<WorkT>>(bound.front().image));
    if (!destination.hasValue())
    {
        return destination.status();
    }

    // The target's validity selects the pixels written when the output is the target's own size and channels; a rule
    // an integer's value decides is judged on its stored samples, and carried as a mask, except the all-valid and
    // finite-only rules, which admit every integer: a mask of every pixel only sends the engine pixel by pixel.
    const bool keepsValidity =
        sameSize(output, targetDescribed) && output.extent().channelCount() == targetDescribed.extent().channelCount();
    an::KernelValidityView validity;
    std::optional<an::ValidityMaskView<const std::uint8_t>> maskView;
    std::optional<an::ValidityMask> judged;
    std::optional<an::ValidityMaskView<const std::uint8_t>> judgedView;
    if (keepsValidity)
    {
        validity = stretchValidity(target.metadata(), target.validity(), maskView);
        const an::ValidityRule rule = validity.descriptor.rule();
        if (!isFloating(targetDescribed.sampleType()) && an::validityRuleIsDecidedByValue(rule) &&
            rule != an::ValidityRule::FiniteOnly && rule != an::ValidityRule::AllValid)
        {
            const SamplesOf stored = samplesOf(target.image());
            const std::optional<an::ImageExtent> extent = an::ImageExtent::create(
                targetDescribed.extent().width(), targetDescribed.extent().height(), 1u, limits);
            if (stored.data == nullptr || !stored.layout.has_value() || !extent.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the target's validity cannot be judged");
            }
            an::Result<an::ValidityMask> mask = an::ValidityMask::create(scope.engine(), *extent);
            if (!mask.hasValue())
            {
                return mask.status();
            }
            std::optional<an::ValidityMaskView<std::uint8_t>> writable = mask->view();
            if (!writable.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the target's validity mask holds nothing");
            }
            if (an::Status marked = an::markImageValidity(scope.execution(), targetDescribed.sampleType(), stored.data,
                                                          *stored.layout, validity.descriptor, *writable);
                marked.failed())
            {
                return marked;
            }
            const std::optional<an::ValidityDescriptor> explicitMask =
                an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ExplicitMask});
            if (!explicitMask.has_value())
            {
                return refusal(an::ErrorCode::Internal, "an explicit mask rule cannot be stated");
            }
            judged.emplace(std::move(*mask));
            judgedView = std::as_const(*judged).view();
            validity.descriptor = *explicitMask;
            validity.mask = judgedView.has_value() ? &*judgedView : nullptr;
            validity.maskPlane = 0u;
        }
    }
    report.selectedBy = validity.descriptor.rule();

    const an::PixelMathLoweringOptions lowering = loweringOptions(an::Image<WorkT>::kSampleType);
    const std::uint32_t outputChannels = output.extent().channelCount();
    for (ParsedExpression& expression : expressions)
    {
        const std::string tabText = pixelMathTabText(expression.tab, settings.singleExpression);
        an::Result<CompiledExpression> whole = compiled(expression.parse.tree(), *bindings, tabText, expression.text);
        if (!whole.hasValue())
        {
            return whole.status();
        }
        an::Result<an::PixelMathLowering> lowered = an::lowerPixelMathToPointwise(whole->program, lowering);
        if (!lowered.hasValue())
        {
            return lowered.status();
        }
        const bool everyChannel = expression.channels.size() == outputChannels;
        PixelMathChannelRun run{
            .channels = expression.channels, .tab = expression.tab, .fused = false, .blocker = lowered->blocker()};
        if (lowered->lowered() && everyChannel)
        {
            if (an::Status ran =
                    runFused<WorkT>(scope, *lowered, whole->resolution, bound, *destination, std::nullopt, validity);
                ran.failed())
            {
                return ran;
            }
            run.fused = true;
            report.runs.push_back(std::move(run));
            continue;
        }
        if (lowered->lowered())
        {
            // A chain over some of the channels runs over one plane of each image at a time: a chain reads every
            // image at the channel being written, so each plane is its own one-channel chain.
            an::Result<an::PixelMathBindings> planes =
                bindingsFor(bound, activeSlot, maskSlot, locals, globals, limits, true);
            std::optional<CompiledExpression> perPlane;
            std::optional<an::PixelMathLowering> planeLowered;
            if (planes.hasValue())
            {
                an::Result<CompiledExpression> recompiled =
                    compiled(expression.parse.tree(), *planes, tabText, expression.text);
                if (recompiled.hasValue())
                {
                    an::Result<an::PixelMathLowering> again =
                        an::lowerPixelMathToPointwise(recompiled->program, lowering);
                    if (again.hasValue() && again->lowered())
                    {
                        perPlane.emplace(std::move(*recompiled));
                        planeLowered.emplace(std::move(*again));
                    }
                }
            }
            if (planeLowered.has_value())
            {
                for (const std::uint32_t channel : expression.channels)
                {
                    if (an::Status ran = runFused<WorkT>(scope, *planeLowered, perPlane->resolution, bound,
                                                         *destination, channel, validity);
                        ran.failed())
                    {
                        return ran;
                    }
                }
                run.fused = true;
                report.runs.push_back(std::move(run));
                continue;
            }
        }
        if (everyChannel)
        {
            if (an::Status ran =
                    runEvaluated<WorkT>(scope, *whole, bound, *destination, std::nullopt, validity, randomSeed);
                ran.failed())
            {
                return ran;
            }
        }
        else
        {
            for (const std::uint32_t channel : expression.channels)
            {
                if (an::Status ran =
                        runEvaluated<WorkT>(scope, *whole, bound, *destination, channel, validity, randomSeed);
                    ran.failed())
                {
                    return ran;
                }
            }
        }
        report.runs.push_back(std::move(run));
    }
    std::sort(report.runs.begin(), report.runs.end(),
              [](const PixelMathChannelRun& left, const PixelMathChannelRun& right) {
                  return left.channels.front() < right.channels.front();
              });

    if (settings.rescale)
    {
        if (an::Status mapped = rescaled<WorkT>(scope, *destination, settings.rescaleLow, settings.rescaleHigh);
            mapped.failed())
        {
            return mapped;
        }
    }
    if (an::Status stopped = scope.execution().checkCancellation(); stopped.failed())
    {
        return stopped;
    }

    // The fractions, narrowed once to the output's representation.
    const bool replaces = settings.outputMode == PixelMathOutputMode::ReplaceTarget;
    an::Result<an::ImageMetadata> metadata =
        resultMetadata(target.metadata(), targetDescribed, output, replaces, keepsValidity);
    if (!metadata.hasValue())
    {
        return metadata.status();
    }
    an::Result<an::ProvenanceEntry> entry = an::ProvenanceEntry::create(
        report.runs.size() == 1u && report.runs.front().fused ? "an::executePointwiseProgram" : "an::executePixelMath",
        lineageDetail(report, settings, randomSeed));
    if (!entry.hasValue())
    {
        return entry.status();
    }
    const PlaneOf fractions = planeOf(*destination, std::nullopt);
    if (fractions.data == nullptr)
    {
        return refusal(an::ErrorCode::NotInitialized, "the result holds nothing");
    }
    std::optional<an::Result<an::consumer::AnyImage>> made;
    (void)an::dispatchSampleType(output.sampleType(), [&]<typename OutT>() {
        made.emplace([&]() -> an::Result<an::consumer::AnyImage> {
            an::Result<an::Image<OutT>> created =
                an::Image<OutT>::create(scope.engine(), output, an::StorageArrangement::Planar,
                                        an::StorageInitialization::Uninitialized, std::move(*metadata));
            if (!created.hasValue())
            {
                return created.status();
            }
            {
                const std::optional<an::ImageWriteAccess<OutT>> access = created->write();
                if (!access.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "the result holds nothing");
                }
                if (an::Status narrowed = writeFromFractions(
                        scope.engine(), scope.execution(), output, an::Image<WorkT>::kSampleType, fractions.data,
                        *fractions.layout, access->view().data(), access->view().layout());
                    narrowed.failed())
                {
                    return narrowed;
                }
            }
            if (an::Status recorded = created->transition(an::MetadataTransition{.record = *entry}); recorded.failed())
            {
                return recorded;
            }
            return an::consumer::AnyImage(std::move(*created));
        }());
    });
    if (!made.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the output's representation is not one an image holds");
    }
    if (!made->hasValue())
    {
        return made->status();
    }
    std::optional<an::ValidityMask> mask;
    if (keepsValidity && target.validity() != nullptr &&
        target.metadata().semantics().validity().rule() == an::ValidityRule::ExplicitMask)
    {
        an::Result<an::ValidityMask> cloned = target.validity()->clone();
        if (!cloned.hasValue())
        {
            return cloned.status();
        }
        mask.emplace(std::move(*cloned));
    }
    report.working = an::Image<WorkT>::kSampleType;
    return an::consumer::ImageAsset::adopt(scope.session(), std::move(**made), std::move(mask));
}

} // namespace

// ---- The symbols.

std::vector<PixelMathSymbol> parsePixelMathSymbols(std::string_view text)
{
    std::map<std::string, double> declared;
    if (!text.empty())
    {
        for (const std::string_view entry : splitAt(text, ','))
        {
            const std::vector<std::string_view> pair = splitAt(entry, '=');
            const std::string name(trimmedView(pair.front()));
            if (name.empty())
            {
                continue;
            }
            double value = 0.0;
            if (pair.size() == 2u)
            {
                value = leadingNumber(trimmedView(pair[1])).value_or(0.0);
            }
            declared[name] = value;
        }
    }
    std::vector<PixelMathSymbol> symbols;
    symbols.reserve(declared.size());
    for (const auto& [name, value] : declared)
    {
        symbols.push_back(PixelMathSymbol{.name = name, .value = value});
    }
    return symbols;
}

// ---- The settings file.

namespace
{

/// A boolean attribute: true when it starts with 1, t, T, y or Y.
[[nodiscard]] bool attributeBool(const QXmlStreamAttributes& attributes, const QString& name, bool fallback)
{
    if (!attributes.hasAttribute(name))
    {
        return fallback;
    }
    const QStringView value = attributes.value(name);
    if (value.isEmpty())
    {
        return false;
    }
    const QChar first = value.front();
    return first == u'1' || first == u't' || first == u'T' || first == u'y' || first == u'Y';
}

[[nodiscard]] int attributeInt(const QXmlStreamAttributes& attributes, const QString& name, int fallback)
{
    if (!attributes.hasAttribute(name))
    {
        return fallback;
    }
    bool ok = false;
    const int value = attributes.value(name).trimmed().toInt(&ok);
    return ok ? value : 0;
}

[[nodiscard]] double attributeDouble(const QXmlStreamAttributes& attributes, const QString& name, double fallback)
{
    if (!attributes.hasAttribute(name))
    {
        return fallback;
    }
    return leadingNumber(attributes.value(name).trimmed().toString().toStdString()).value_or(0.0);
}

template <typename EnumT> [[nodiscard]] EnumT enumFromIndex(int index, int count, EnumT fallback) noexcept
{
    return index >= 0 && index < count ? static_cast<EnumT>(index) : fallback;
}

} // namespace

an::Result<PixelMathSettings> readPixelMathSettingsFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return refusal(an::ErrorCode::ExternalFailure,
                       fmt::format("Could NOT parse pixel math file: {}. Error: '{}'", path.toStdString(),
                                   file.errorString().toStdString()));
    }
    const std::string where = path.toStdString();
    PixelMathSettings settings;
    QXmlStreamReader reader(&file);
    bool sawRoot = false;
    bool sawGlobal = false;
    bool sawOutput = false;
    std::array<QString, 4u> expressions;
    QString localSymbols;
    QString globalSymbols;
    while (!reader.atEnd())
    {
        if (reader.readNext() != QXmlStreamReader::StartElement)
        {
            continue;
        }
        const QStringView element = reader.name();
        const QXmlStreamAttributes attributes = reader.attributes();
        if (!sawRoot)
        {
            if (element != u"pixel_math")
            {
                return refusal(
                    an::ErrorCode::InvalidData,
                    fmt::format("Could NOT parse pixel math file: {}. Error: 'No pixel_math node found'", where));
            }
            if (attributeInt(attributes, QStringLiteral("version"), 0) != 1)
            {
                return refusal(an::ErrorCode::InvalidData,
                               fmt::format("Could NOT parse pixel math file: {}. Version is not compatible", where));
            }
            sawRoot = true;
            continue;
        }
        if (element == u"global_settings")
        {
            sawGlobal = true;
            // An attribute left out reads as the window's default, which the settings start as.
            settings.singleExpression =
                attributeBool(attributes, QStringLiteral("single_expr"), settings.singleExpression);
            settings.singleThreaded =
                attributeBool(attributes, QStringLiteral("single_threaded"), settings.singleThreaded);
            settings.codeOptimization =
                attributeBool(attributes, QStringLiteral("optimize"), settings.codeOptimization);
            settings.rescale = attributeBool(attributes, QStringLiteral("rescale"), settings.rescale);
            settings.rescaleLow = attributeDouble(attributes, QStringLiteral("rescale_low"), settings.rescaleLow);
            settings.rescaleHigh = attributeDouble(attributes, QStringLiteral("rescale_high"), settings.rescaleHigh);
            // The file's process_64_bits is not read: every expression is evaluated in 64 bits.
            settings.processIn64Bits = true;
        }
        else if (element == u"output_settings")
        {
            sawOutput = true;
            settings.outputMode = enumFromIndex(attributeInt(attributes, QStringLiteral("output_mode"), 0), 2,
                                                PixelMathOutputMode::ReplaceTarget);
            settings.newImageId = attributes.hasAttribute(QStringLiteral("new_image_id"))
                                      ? attributes.value(QStringLiteral("new_image_id")).toString().toStdString()
                                      : std::string(kPixelMathDefaultImageName);
            settings.width =
                static_cast<std::uint32_t>((std::max)(0, attributeInt(attributes, QStringLiteral("width"), 0)));
            settings.height =
                static_cast<std::uint32_t>((std::max)(0, attributeInt(attributes, QStringLiteral("height"), 0)));
            settings.colorSpace = enumFromIndex(attributeInt(attributes, QStringLiteral("color_space"), 0), 4,
                                                PixelMathColorSpace::AsTarget);
            settings.colorFormat = enumFromIndex(attributeInt(attributes, QStringLiteral("color_format"), 0), 6,
                                                 PixelMathColorFormat::AsTarget);
        }
        else if (element.startsWith(u"expression") && element.size() == 11)
        {
            const int index = element.back().digitValue();
            if (index >= 0 && index < 4)
            {
                expressions[static_cast<std::size_t>(index)] =
                    reader.readElementText(QXmlStreamReader::IncludeChildElements);
            }
        }
        else if (element == u"local_symbols")
        {
            localSymbols = reader.readElementText(QXmlStreamReader::IncludeChildElements);
        }
        else if (element == u"global_symbols")
        {
            globalSymbols = reader.readElementText(QXmlStreamReader::IncludeChildElements);
        }
    }
    if (reader.hasError() && !sawRoot)
    {
        return refusal(an::ErrorCode::InvalidData, fmt::format("Could NOT parse pixel math file: {}. Error: '{}'",
                                                               where, reader.errorString().toStdString()));
    }
    if (!sawRoot)
    {
        return refusal(an::ErrorCode::InvalidData,
                       fmt::format("Could NOT parse pixel math file: {}. Error: 'No pixel_math node found'", where));
    }
    if (!sawGlobal)
    {
        return refusal(
            an::ErrorCode::InvalidData,
            fmt::format("Could NOT parse pixel math file: {}. Error: 'No global_settings node found'", where));
    }
    if (!sawOutput)
    {
        return refusal(
            an::ErrorCode::InvalidData,
            fmt::format("Could NOT parse pixel math file: {}. Error: 'No output_settings node found'", where));
    }
    for (std::size_t index = 0u; index < expressions.size(); ++index)
    {
        settings.expressions[index] = expressions[index].toStdString();
    }
    settings.localSymbols = localSymbols.toStdString();
    settings.globalSymbols = globalSymbols.toStdString();
    return settings;
}

an::Status writePixelMathSettingsFile(const QString& path, const PixelMathSettings& settings)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        return refusal(an::ErrorCode::ExternalFailure,
                       fmt::format("the pixel math file {} cannot be written: {}", path.toStdString(),
                                   file.errorString().toStdString()));
    }
    const auto flag = [](bool value) { return value ? QStringLiteral("true") : QStringLiteral("false"); };
    const auto number = [](double value) { return QString::number(value, 'g', 17); };
    QXmlStreamWriter writer(&file);
    writer.setAutoFormatting(true);
    writer.setAutoFormattingIndent(-1);
    writer.writeStartDocument();
    writer.writeStartElement(QStringLiteral("pixel_math"));
    writer.writeAttribute(QStringLiteral("version"), QStringLiteral("1"));
    writer.writeStartElement(QStringLiteral("global_settings"));
    writer.writeAttribute(QStringLiteral("single_expr"), flag(settings.singleExpression));
    writer.writeAttribute(QStringLiteral("single_threaded"), flag(settings.singleThreaded));
    writer.writeAttribute(QStringLiteral("process_64_bits"), flag(settings.processIn64Bits));
    writer.writeAttribute(QStringLiteral("optimize"), flag(settings.codeOptimization));
    writer.writeAttribute(QStringLiteral("rescale"), flag(settings.rescale));
    writer.writeAttribute(QStringLiteral("rescale_low"), number(settings.rescaleLow));
    writer.writeAttribute(QStringLiteral("rescale_high"), number(settings.rescaleHigh));
    writer.writeEndElement();
    writer.writeStartElement(QStringLiteral("output_settings"));
    writer.writeAttribute(QStringLiteral("output_mode"), QString::number(static_cast<int>(settings.outputMode)));
    writer.writeAttribute(QStringLiteral("new_image_id"), QString::fromStdString(settings.newImageId));
    writer.writeAttribute(QStringLiteral("width"), QString::number(settings.width));
    writer.writeAttribute(QStringLiteral("height"), QString::number(settings.height));
    writer.writeAttribute(QStringLiteral("color_space"), QString::number(static_cast<int>(settings.colorSpace)));
    writer.writeAttribute(QStringLiteral("color_format"), QString::number(static_cast<int>(settings.colorFormat)));
    writer.writeEndElement();
    for (std::size_t index = 0u; index < settings.expressions.size(); ++index)
    {
        writer.writeTextElement(QStringLiteral("expression%1").arg(index),
                                QString::fromStdString(settings.expressions[index]));
    }
    writer.writeTextElement(QStringLiteral("local_symbols"), QString::fromStdString(settings.localSymbols));
    writer.writeTextElement(QStringLiteral("global_symbols"), QString::fromStdString(settings.globalSymbols));
    writer.writeEndElement();
    writer.writeEndDocument();
    if (writer.hasError() || !file.commit())
    {
        return refusal(an::ErrorCode::ExternalFailure,
                       fmt::format("the pixel math file {} cannot be written: {}", path.toStdString(),
                                   file.errorString().toStdString()));
    }
    return an::Status::success();
}

// ---- The run.

std::vector<std::pair<PixelMathExpressionTab, std::string>>
pixelMathChannelExpressions(const PixelMathSettings& settings, std::uint32_t channels)
{
    std::vector<std::pair<PixelMathExpressionTab, std::string>> perChannel;
    perChannel.reserve(channels);
    for (std::uint32_t channel = 0u; channel < channels; ++channel)
    {
        PixelMathExpressionTab tab = PixelMathExpressionTab::RedOrAll;
        if (channel == 3u)
        {
            tab = PixelMathExpressionTab::Alpha;
        }
        else if (!settings.singleExpression && channels > 1u)
        {
            tab = static_cast<PixelMathExpressionTab>(channel);
        }
        perChannel.emplace_back(tab, std::string(trimmedView(settings.expressions[static_cast<std::size_t>(tab)])));
    }
    return perChannel;
}

std::string pixelMathTabText(PixelMathExpressionTab tab, bool singleExpression)
{
    switch (tab)
    {
    case PixelMathExpressionTab::RedOrAll:
        return singleExpression ? "RGB/K" : "R";
    case PixelMathExpressionTab::Green:
        return "G";
    case PixelMathExpressionTab::Blue:
        return "B";
    case PixelMathExpressionTab::Alpha:
        return "A";
    }
    return "RGB/K";
}

an::Result<an::ImageDescriptor> pixelMathOutputDescriptor(const PixelMathSettings& settings,
                                                          const an::ImageDescriptor& target,
                                                          const an::EngineLimits& limits)
{
    if (settings.outputMode == PixelMathOutputMode::ReplaceTarget)
    {
        return target;
    }
    const std::uint32_t width = settings.width != 0u ? settings.width : target.extent().width();
    const std::uint32_t height = settings.height != 0u ? settings.height : target.extent().height();
    std::uint32_t channels = target.extent().channelCount();
    switch (settings.colorSpace)
    {
    case PixelMathColorSpace::AsTarget:
        break;
    case PixelMathColorSpace::Grayscale:
        channels = 1u;
        break;
    case PixelMathColorSpace::Rgb:
        channels = 3u;
        break;
    case PixelMathColorSpace::Rgba:
        channels = 4u;
        break;
    }
    an::SampleType sampleType = target.sampleType();
    switch (settings.colorFormat)
    {
    case PixelMathColorFormat::AsTarget:
        break;
    case PixelMathColorFormat::UInt8:
        sampleType = an::SampleType::UInt8;
        break;
    case PixelMathColorFormat::UInt16:
        sampleType = an::SampleType::UInt16;
        break;
    case PixelMathColorFormat::UInt32:
        sampleType = an::SampleType::UInt32;
        break;
    case PixelMathColorFormat::Float32:
        sampleType = an::SampleType::Float32;
        break;
    case PixelMathColorFormat::Float64:
        sampleType = an::SampleType::Float64;
        break;
    }
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, channels, limits);
    if (!extent.has_value())
    {
        return refusal(
            an::ErrorCode::InvalidArgument,
            fmt::format("a new image of {} by {} pixels and {} channels cannot be made", width, height, channels));
    }
    an::ImageDescriptorRequest request = target.toRequest();
    request.extent = *extent;
    request.sampleType = sampleType;
    const bool sameShape = width == target.extent().width() && height == target.extent().height() &&
                           channels == target.extent().channelCount();
    if (!sameShape || channels != 1u)
    {
        request.cfa = an::CfaDescriptor{};
    }
    if (channels != target.extent().channelCount())
    {
        request.channelSchema = channels == 1u   ? an::ChannelSchema::Mono
                                : channels == 3u ? an::ChannelSchema::Rgb
                                                 : an::ChannelSchema::Rgba;
        if (channels == 1u)
        {
            request.colorSpace = an::ColorSpace::Unspecified;
        }
    }
    const std::optional<an::ImageDescriptor> described = an::ImageDescriptor::create(request);
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument,
                       fmt::format("a new image of {} samples cannot hold what the target's samples mean",
                                   an::sampleTypeName(sampleType)));
    }
    return *described;
}

std::uint64_t pixelMathFrameSeed(std::uint64_t runSeed, std::uint64_t frame) noexcept
{
    if (frame == 0u)
    {
        return runSeed;
    }
    // A SplitMix64 step over the run's seed and the frame, so neighbouring frames draw unrelated streams.
    std::uint64_t mixed = runSeed + frame * 0x9E3779B97F4A7C15ull;
    mixed = (mixed ^ (mixed >> 30u)) * 0xBF58476D1CE4E5B9ull;
    mixed = (mixed ^ (mixed >> 27u)) * 0x94D049BB133111EBull;
    return mixed ^ (mixed >> 31u);
}

an::Result<an::consumer::ImageAsset> runPixelMath(const an::ui::ExecutionScope& scope,
                                                  const an::ui::ProcessSource& target,
                                                  const PixelMathSettings& settings, const PixelMathImageSet& images,
                                                  std::uint64_t randomSeed, PixelMathRunReport* report)
{
    const std::optional<an::ImageDescriptor> targetDescribed = target.descriptor();
    if (!targetDescribed.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the target holds nothing");
    }
    an::Result<an::ImageDescriptor> output =
        pixelMathOutputDescriptor(settings, *targetDescribed, scope.engine().limits());
    if (!output.hasValue())
    {
        return output.status();
    }

    // Every distinct expression is parsed before any image is read, and every one the engine refuses is said.
    std::vector<ParsedExpression> expressions;
    const std::vector<std::pair<PixelMathExpressionTab, std::string>> perChannel =
        pixelMathChannelExpressions(settings, output->extent().channelCount());
    for (std::uint32_t channel = 0u; channel < perChannel.size(); ++channel)
    {
        const auto& [tab, text] = perChannel[channel];
        if (text.empty())
        {
            continue;
        }
        const auto same = std::find_if(expressions.begin(), expressions.end(),
                                       [&](const ParsedExpression& parsed) { return parsed.tab == tab; });
        if (same != expressions.end())
        {
            same->channels.push_back(channel);
            continue;
        }
        ParsedExpression parsed;
        parsed.tab = tab;
        parsed.text = text;
        parsed.channels.push_back(channel);
        expressions.push_back(std::move(parsed));
    }
    if (expressions.empty())
    {
        return refusal(an::ErrorCode::InvalidArgument, "The pixel math expression is empty");
    }
    std::string refused;
    ReadNames read;
    for (ParsedExpression& expression : expressions)
    {
        expression.parse = an::parsePixelMath(expression.text);
        if (!expression.parse.succeeded())
        {
            const std::string said =
                expression.parse.diagnostics().empty()
                    ? fmt::format("{} expression: {}", pixelMathTabText(expression.tab, settings.singleExpression),
                                  expression.parse.status().message())
                    : describedDiagnostics(expression.parse.diagnostics(),
                                           pixelMathTabText(expression.tab, settings.singleExpression),
                                           expression.text);
            refused += (refused.empty() ? "" : "\n") + said;
            continue;
        }
        collectNames(expression.parse.tree(), read);
    }
    if (!refused.empty())
    {
        return refusal(an::ErrorCode::InvalidArgument, refused);
    }
    if (read.mask && images.mask == nullptr)
    {
        return refusal(an::ErrorCode::InvalidArgument, "$M reads the target's mask, and the target has none");
    }

    // One working representation for every image the run reads and the output.
    bool wide = needsDouble(targetDescribed->sampleType()) || needsDouble(output->sampleType());
    const auto widens = [](const std::shared_ptr<const an::consumer::ImageAsset>& asset) {
        if (asset == nullptr)
        {
            return false;
        }
        const std::optional<an::ImageDescriptor> described = asset->descriptor();
        return described.has_value() && needsDouble(described->sampleType());
    };
    for (const PixelMathNamedImage& other : images.others)
    {
        wide = wide || (read.names.contains(other.name) && widens(other.asset));
    }
    wide = wide || (read.active && widens(images.active)) || (read.mask && widens(images.mask));

    PixelMathRunReport local;
    PixelMathRunReport& filled = report != nullptr ? *report : local;
    filled = PixelMathRunReport{};
    return wide ? runIn<double>(scope, target, *targetDescribed, *output, settings, images, expressions, read,
                                randomSeed, filled)
                : runIn<float>(scope, target, *targetDescribed, *output, settings, images, expressions, read,
                               randomSeed, filled);
}

std::vector<std::shared_ptr<const an::consumer::ImageAsset>> pixelMathImagesRead(const PixelMathSettings& settings,
                                                                                 const PixelMathImageSet& images)
{
    ReadNames read;
    std::set<std::string> parsed;
    for (const auto& [tab, text] : pixelMathChannelExpressions(settings, 4u))
    {
        if (text.empty() || !parsed.insert(text).second)
        {
            continue;
        }
        const an::PixelMathParse parse = an::parsePixelMath(text);
        if (parse.succeeded())
        {
            collectNames(parse.tree(), read);
        }
    }
    std::vector<std::shared_ptr<const an::consumer::ImageAsset>> named;
    for (const PixelMathNamedImage& other : images.others)
    {
        if (other.asset != nullptr && other.name != images.targetName && read.names.contains(other.name))
        {
            named.push_back(other.asset);
        }
    }
    if (read.active && images.active != nullptr)
    {
        named.push_back(images.active);
    }
    if (read.mask && images.mask != nullptr)
    {
        named.push_back(images.mask);
    }
    return named;
}

} // namespace an::ui::processes
