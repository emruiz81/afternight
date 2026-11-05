// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - what an expression's names refer to, and whether it computes anything.
//
// Resolution is the stage where an expression meets its images. The bindings refuse a name the
// language owns or one already taken; the resolver then gives every node its meaning in one pass.
// A name is an image before it is a declared constant or variable, and a meta symbol is whichever
// image the bindings pointed it at. A call with exactly one argument that names an image is looked
// up among the whole-image statistics first, so `med($T)` and `med(a, b, c)` reach different rows.
// The images an expression uses are numbered in first-use order after the target, which is always
// image zero; constants, variables and statistic requests are numbered once each however often
// they are written.
//
// Reads at the pixel being written are checked last, once every node is known: such an image has
// the target's width and height and, unless a channel is selected, either one channel, which is
// broadcast, or the target's channel count. Every image a sample, a statistic or a colour component
// is read from must mean the same kind of number as the target. Any diagnostic empties the
// resolution, so a refused expression carries nothing a compiler could use.

#include <AfterNightEngine/Algorithms/PixelMath/PixelMathResolution.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace an
{
namespace
{

constexpr const char* kBindingOperation = "an::PixelMathBindings";

[[nodiscard]] Status bindingFailure(ErrorCode code, const char* message, std::string_view name)
{
    StatusDetail detail{.message = message, .operation = kBindingOperation, .domain = ErrorDomain::Algorithms};
    detail.context.push_back(StatusContextField::ofText(ContextKey::Parameter, std::string(name)));
    return Status::failure(code, std::move(detail));
}

/// Which statistic a whole-image call asks for, or no value when the identity is not one.
[[nodiscard]] std::optional<PixelMathStatistic> statisticOf(PixelMathFunctionId id) noexcept
{
    switch (id)
    {
    case PixelMathFunctionId::MinimumOfImage:
        return PixelMathStatistic::Minimum;
    case PixelMathFunctionId::MaximumOfImage:
        return PixelMathStatistic::Maximum;
    case PixelMathFunctionId::MeanOfImage:
        return PixelMathStatistic::Mean;
    case PixelMathFunctionId::MedianOfImage:
        return PixelMathStatistic::Median;
    case PixelMathFunctionId::StandardDeviationOfImage:
        return PixelMathStatistic::StandardDeviation;
    case PixelMathFunctionId::VarianceOfImage:
        return PixelMathStatistic::Variance;
    case PixelMathFunctionId::MedianAbsoluteDeviationOfImage:
        return PixelMathStatistic::MedianAbsoluteDeviation;
    case PixelMathFunctionId::AverageAbsoluteDeviationOfImage:
        return PixelMathStatistic::AverageAbsoluteDeviation;
    case PixelMathFunctionId::MeanOfSquaresOfImage:
        return PixelMathStatistic::MeanOfSquares;
    case PixelMathFunctionId::SumOfSquaresOfImage:
        return PixelMathStatistic::SumOfSquares;
    case PixelMathFunctionId::BiweightMidvarianceOfImage:
        return PixelMathStatistic::BiweightMidvariance;
    case PixelMathFunctionId::BackgroundNoiseOfImage:
        return PixelMathStatistic::BackgroundNoise;
    default:
        break;
    }
    return std::nullopt;
}

} // namespace

Status PixelMathBindings::bindImage(std::string_view name, const PixelMathImageDeclaration& image)
{
    if (name.empty())
    {
        return bindingFailure(ErrorCode::InvalidArgument, "an image was bound to an empty name", name);
    }
    if (isPixelMathReservedName(name))
    {
        return bindingFailure(ErrorCode::InvalidArgument, "the name an image was bound to belongs to the language",
                              name);
    }
    if (findImage(name).has_value())
    {
        return bindingFailure(ErrorCode::InvalidArgument, "an image is already bound to that name", name);
    }
    for (const Symbol& symbol : m_symbols)
    {
        if (symbol.name == name)
        {
            return bindingFailure(ErrorCode::InvalidArgument, "a symbol is already declared with that name", name);
        }
    }
    if (m_images.size() >= kMaxPixelMathImages)
    {
        StatusDetail detail{.message = "more images were bound than one expression may name",
                            .operation = kBindingOperation,
                            .domain = ErrorDomain::Algorithms};
        detail.context.push_back(StatusContextField::ofCount(ContextKey::Limit, kMaxPixelMathImages));
        detail.context.push_back(StatusContextField::ofCount(ContextKey::Requested, m_images.size() + 1u));
        return Status::failure(ErrorCode::LimitExceeded, std::move(detail));
    }
    m_images.push_back(Image{std::string(name), image});
    return Status::success();
}

Status PixelMathBindings::bindMetaSymbol(PixelMathMetaSymbol symbol, std::string_view imageName)
{
    if (!isPixelMathMetaSymbol(symbol))
    {
        return bindingFailure(ErrorCode::InvalidArgument, "the meta symbol is not one this build knows", imageName);
    }
    const std::optional<std::uint32_t> slot = findImage(imageName);
    if (!slot.has_value())
    {
        return bindingFailure(ErrorCode::InvalidArgument, "a meta symbol was pointed at an image that is not bound",
                              imageName);
    }
    switch (symbol)
    {
    case PixelMathMetaSymbol::Target:
        m_target = slot;
        break;
    case PixelMathMetaSymbol::Active:
        m_active = slot;
        break;
    case PixelMathMetaSymbol::Mask:
        m_mask = slot;
        break;
    }
    return Status::success();
}

Status PixelMathBindings::declareConstant(std::string_view name, double value)
{
    if (name.empty() || isPixelMathReservedName(name))
    {
        return bindingFailure(ErrorCode::InvalidArgument,
                              "a constant was declared with an empty name or one the language owns", name);
    }
    if (findImage(name).has_value())
    {
        return bindingFailure(ErrorCode::InvalidArgument, "an image is already bound to that name", name);
    }
    for (const Symbol& symbol : m_symbols)
    {
        if (symbol.name == name)
        {
            return bindingFailure(ErrorCode::InvalidArgument, "a symbol is already declared with that name", name);
        }
    }
    if (!std::isfinite(value))
    {
        return bindingFailure(ErrorCode::InvalidArgument, "a constant was declared with a value that is not finite",
                              name);
    }
    m_symbols.push_back(Symbol{std::string(name), value, false});
    return Status::success();
}

Status PixelMathBindings::declareVariable(std::string_view name, double initialValue)
{
    const Status declared = declareConstant(name, initialValue);
    if (declared.failed())
    {
        return declared;
    }
    m_symbols.back().assignable = true;
    return Status::success();
}

std::uint32_t PixelMathBindings::imageCount() const noexcept
{
    return static_cast<std::uint32_t>(m_images.size());
}

std::string_view PixelMathBindings::imageName(std::uint32_t slot) const noexcept
{
    if (slot >= m_images.size())
    {
        return {};
    }
    return m_images[slot].name;
}

const PixelMathImageDeclaration* PixelMathBindings::image(std::uint32_t slot) const noexcept
{
    if (slot >= m_images.size())
    {
        return nullptr;
    }
    return &m_images[slot].declaration;
}

std::optional<std::uint32_t> PixelMathBindings::findImage(std::string_view name) const noexcept
{
    for (std::size_t index = 0u; index < m_images.size(); ++index)
    {
        if (m_images[index].name == name)
        {
            return static_cast<std::uint32_t>(index);
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t> PixelMathBindings::metaSymbolImage(PixelMathMetaSymbol symbol) const noexcept
{
    switch (symbol)
    {
    case PixelMathMetaSymbol::Target:
        return m_target;
    case PixelMathMetaSymbol::Active:
        return m_active;
    case PixelMathMetaSymbol::Mask:
        return m_mask;
    }
    return std::nullopt;
}

std::uint32_t PixelMathBindings::symbolCount() const noexcept
{
    return static_cast<std::uint32_t>(m_symbols.size());
}

std::string_view PixelMathBindings::symbolName(std::uint32_t index) const noexcept
{
    if (index >= m_symbols.size())
    {
        return {};
    }
    return m_symbols[index].name;
}

std::optional<double> PixelMathBindings::symbolValue(std::uint32_t index, bool& assignable) const noexcept
{
    if (index >= m_symbols.size())
    {
        return std::nullopt;
    }
    assignable = m_symbols[index].assignable;
    return m_symbols[index].value;
}

PixelMathDiagnostic PixelMathBindings::check() const noexcept
{
    PixelMathDiagnostic diagnostic;
    if (!m_target.has_value())
    {
        diagnostic.code = PixelMathDiagnosticCode::NoTargetImage;
    }
    return diagnostic;
}

/// Decides what one tree's names refer to.
///
/// The nodes are resolved in index order, which is possible because the parser builds a node only
/// after its children: every child of a node has a smaller index than the node itself. So the walk
/// needs no recursion of its own, and the depth limit the parse was checked under is not something
/// this stage has to re-establish.
class PixelMathResolver
{
public:
    PixelMathResolver(const PixelMathSyntaxTree& tree, const PixelMathBindings& bindings) noexcept
        : m_tree(tree), m_bindings(bindings)
    {
    }

    [[nodiscard]] PixelMathResolution run();

private:
    void refuse(PixelMathDiagnosticCode code, PixelMathSourceSpan span, std::string name, std::uint32_t expected = 0u,
                std::uint32_t actual = 0u)
    {
        PixelMathDiagnostic diagnostic;
        diagnostic.code = code;
        diagnostic.span = span;
        diagnostic.name = std::move(name);
        diagnostic.expected = expected;
        diagnostic.actual = actual;
        m_resolution.m_diagnostics.push_back(std::move(diagnostic));
    }

    /// Records that the expression names one of the bindings' images, and says which of the
    /// expression's images that is.
    [[nodiscard]] std::optional<std::uint32_t> useImage(std::uint32_t bindingSlot, PixelMathSourceSpan span,
                                                        std::string_view name);

    void resolveName(std::uint32_t nodeIndex);
    void resolveMetaSymbol(std::uint32_t nodeIndex);
    void resolveCall(std::uint32_t nodeIndex);
    void resolveAssignment(std::uint32_t nodeIndex);
    void checkSampleReads();

    const PixelMathSyntaxTree& m_tree;
    const PixelMathBindings& m_bindings;
    PixelMathResolution m_resolution;
    std::vector<bool> m_consumedAsImage;
    std::vector<std::uint32_t> m_constantIndices;
    std::vector<std::uint32_t> m_variableIndices;
    const PixelMathImageDeclaration* m_target = nullptr;
    NumericDomain m_targetDomain = NumericDomain::NormalizedLinear;
};

std::optional<std::uint32_t> PixelMathResolver::useImage(std::uint32_t bindingSlot, PixelMathSourceSpan span,
                                                         std::string_view name)
{
    for (std::size_t index = 0u; index < m_resolution.m_imageSlots.size(); ++index)
    {
        if (m_resolution.m_imageSlots[index] == bindingSlot)
        {
            return static_cast<std::uint32_t>(index);
        }
    }
    if (m_resolution.m_imageSlots.size() >= kMaxPixelMathImages)
    {
        refuse(PixelMathDiagnosticCode::TooManyImages, span, std::string(name), kMaxPixelMathImages,
               static_cast<std::uint32_t>(m_resolution.m_imageSlots.size() + 1u));
        return std::nullopt;
    }
    const PixelMathImageDeclaration* const declaration = m_bindings.image(bindingSlot);
    if (declaration == nullptr)
    {
        refuse(PixelMathDiagnosticCode::UnknownName, span, std::string(name));
        return std::nullopt;
    }
    m_resolution.m_imageSlots.push_back(bindingSlot);
    m_resolution.m_images.push_back(*declaration);
    return static_cast<std::uint32_t>(m_resolution.m_imageSlots.size() - 1u);
}

void PixelMathResolver::resolveName(std::uint32_t nodeIndex)
{
    const PixelMathNode& node = m_tree.nodes()[nodeIndex];
    PixelMathResolvedNode& resolved = m_resolution.m_nodes[nodeIndex];
    const std::string_view name = m_tree.name(node.nameIndex);

    const std::optional<std::uint32_t> imageSlot = m_bindings.findImage(name);
    if (imageSlot.has_value())
    {
        const std::optional<std::uint32_t> used = useImage(*imageSlot, node.span, name);
        if (!used.has_value())
        {
            return;
        }
        resolved.valueKind = PixelMathValueKind::ImageReference;
        resolved.reference = PixelMathReference::Image;
        resolved.slot = *used;
        resolved.channel = node.channel;
        resolved.hasChannel = node.hasChannel;
        return;
    }

    for (std::uint32_t index = 0u; index < m_bindings.symbolCount(); ++index)
    {
        if (m_bindings.symbolName(index) != name)
        {
            continue;
        }
        bool assignable = false;
        const std::optional<double> value = m_bindings.symbolValue(index, assignable);
        if (!value.has_value())
        {
            break;
        }
        if (node.hasChannel)
        {
            refuse(PixelMathDiagnosticCode::ChannelOutOfRange, node.span, std::string(name), 0u, node.channel);
            return;
        }
        std::vector<std::uint32_t>& table = assignable ? m_variableIndices : m_constantIndices;
        std::vector<double>& values = assignable ? m_resolution.m_variableInitialValues : m_resolution.m_constants;
        const auto found = std::find(table.begin(), table.end(), index);
        std::uint32_t slot = 0u;
        if (found != table.end())
        {
            slot = static_cast<std::uint32_t>(std::distance(table.begin(), found));
        }
        else
        {
            slot = static_cast<std::uint32_t>(table.size());
            table.push_back(index);
            values.push_back(*value);
        }
        resolved.valueKind = PixelMathValueKind::Scalar;
        resolved.reference = assignable ? PixelMathReference::Variable : PixelMathReference::Constant;
        resolved.slot = slot;
        return;
    }

    refuse(PixelMathDiagnosticCode::UnknownName, node.span, std::string(name));
}

void PixelMathResolver::resolveMetaSymbol(std::uint32_t nodeIndex)
{
    const PixelMathNode& node = m_tree.nodes()[nodeIndex];
    PixelMathResolvedNode& resolved = m_resolution.m_nodes[nodeIndex];
    const std::optional<std::uint32_t> slot = m_bindings.metaSymbolImage(node.metaSymbol);
    if (!slot.has_value())
    {
        refuse(PixelMathDiagnosticCode::UnboundMetaSymbol, node.span,
               std::string(pixelMathMetaSymbolName(node.metaSymbol)));
        return;
    }
    const std::optional<std::uint32_t> used = useImage(*slot, node.span, pixelMathMetaSymbolName(node.metaSymbol));
    if (!used.has_value())
    {
        return;
    }
    resolved.valueKind = PixelMathValueKind::ImageReference;
    resolved.reference = PixelMathReference::Image;
    resolved.slot = *used;
    resolved.channel = node.channel;
    resolved.hasChannel = node.hasChannel;
}

void PixelMathResolver::resolveAssignment(std::uint32_t nodeIndex)
{
    const std::span<const std::uint32_t> children = m_tree.children(nodeIndex);
    if (children.size() != 2u)
    {
        return;
    }
    const PixelMathResolvedNode& target = m_resolution.m_nodes[children[0]];
    if (target.reference != PixelMathReference::Variable)
    {
        const PixelMathNode& targetNode = m_tree.nodes()[children[0]];
        refuse(PixelMathDiagnosticCode::AssignmentTargetNotVariable, targetNode.span,
               std::string(m_tree.name(targetNode.nameIndex)));
        return;
    }
    m_resolution.m_usesAssignment = true;
}

void PixelMathResolver::resolveCall(std::uint32_t nodeIndex)
{
    const PixelMathNode& node = m_tree.nodes()[nodeIndex];
    PixelMathResolvedNode& resolved = m_resolution.m_nodes[nodeIndex];
    const std::string_view name = m_tree.name(node.nameIndex);
    const std::span<const std::uint32_t> arguments = m_tree.children(nodeIndex);
    const std::uint32_t argumentCount = static_cast<std::uint32_t>(arguments.size());

    const bool oneImageArgument =
        argumentCount == 1u && m_resolution.m_nodes[arguments[0]].valueKind == PixelMathValueKind::ImageReference;

    const PixelMathFunctionSignature* signature = nullptr;
    if (oneImageArgument)
    {
        signature = findPixelMathImageStatistic(name);
    }
    if (signature == nullptr)
    {
        signature = findPixelMathFunction(name);
    }
    if (signature == nullptr)
    {
        if (findPixelMathImageStatistic(name) != nullptr)
        {
            refuse(PixelMathDiagnosticCode::ImageArgumentRequired, node.span, std::string(name), 1u, argumentCount);
            return;
        }
        refuse(PixelMathDiagnosticCode::UnknownFunction, node.span, std::string(name));
        return;
    }

    if (argumentCount < signature->minimumArguments ||
        (signature->maximumArguments != kUnboundedPixelMathArgumentCount &&
         argumentCount > signature->maximumArguments))
    {
        refuse(PixelMathDiagnosticCode::ArgumentCountMismatch, node.span, std::string(name),
               argumentCount < signature->minimumArguments ? signature->minimumArguments : signature->maximumArguments,
               argumentCount);
        return;
    }
    if (!signature->available)
    {
        refuse(PixelMathDiagnosticCode::FunctionNotAvailable, node.span, std::string(name));
        return;
    }

    std::uint32_t imageIndex = 0u;
    if (signature->firstArgumentNamesImage)
    {
        if (argumentCount >= 1u)
        {
            const PixelMathResolvedNode& first = m_resolution.m_nodes[arguments[0]];
            if (first.valueKind != PixelMathValueKind::ImageReference || first.reference != PixelMathReference::Image)
            {
                refuse(PixelMathDiagnosticCode::ImageArgumentRequired, node.span, std::string(name));
                return;
            }
            imageIndex = first.slot;
            m_consumedAsImage[arguments[0]] = true;
        }
        else
        {
            // A nullary form of an image function is about the target, which is always image zero.
            imageIndex = 0u;
        }
    }

    // Every argument but an image one has to be a number. An image reference used as a number is a
    // sample of that image, which is legal everywhere a number is; what is refused is the other
    // direction, and that is what the check above does.
    resolved.reference = PixelMathReference::Function;
    resolved.function = signature->id;
    resolved.slot = imageIndex;
    resolved.valueKind = PixelMathValueKind::Scalar;

    switch (signature->functionClass)
    {
    case PixelMathFunctionClass::ImageStatistic: {
        const std::optional<PixelMathStatistic> statistic = statisticOf(signature->id);
        if (!statistic.has_value())
        {
            refuse(PixelMathDiagnosticCode::FunctionNotAvailable, node.span, std::string(name));
            return;
        }
        const PixelMathStatisticRequest request{imageIndex, *statistic};
        const auto found = std::find(m_resolution.m_statistics.begin(), m_resolution.m_statistics.end(), request);
        if (found != m_resolution.m_statistics.end())
        {
            resolved.slot = static_cast<std::uint32_t>(std::distance(m_resolution.m_statistics.begin(), found));
        }
        else
        {
            resolved.slot = static_cast<std::uint32_t>(m_resolution.m_statistics.size());
            m_resolution.m_statistics.push_back(request);
        }
        resolved.reference = PixelMathReference::ImageStatistic;
        if (m_resolution.m_images[imageIndex].numericDomain != m_targetDomain)
        {
            refuse(PixelMathDiagnosticCode::MixedNumericDomains, node.span, std::string(name));
        }
        break;
    }
    case PixelMathFunctionClass::ColorComponent: {
        const PixelMathImageDeclaration& declaration = m_resolution.m_images[imageIndex];
        if (declaration.extent.channelCount() < 3u)
        {
            refuse(PixelMathDiagnosticCode::ColorImageRequired, node.span, std::string(name), 3u,
                   declaration.extent.channelCount());
            return;
        }
        if (declaration.extent.width() != m_target->extent.width() ||
            declaration.extent.height() != m_target->extent.height())
        {
            refuse(PixelMathDiagnosticCode::MismatchedImageExtent, node.span, std::string(name));
            return;
        }
        if (declaration.numericDomain != m_targetDomain)
        {
            refuse(PixelMathDiagnosticCode::MixedNumericDomains, node.span, std::string(name));
            return;
        }
        m_resolution.m_usesColorComponent = true;
        break;
    }
    case PixelMathFunctionClass::ImageSample:
        if (m_resolution.m_images[imageIndex].numericDomain != m_targetDomain)
        {
            refuse(PixelMathDiagnosticCode::MixedNumericDomains, node.span, std::string(name));
            return;
        }
        m_resolution.m_usesImageSample = true;
        m_resolution.m_usesPixelPosition = true;
        break;
    case PixelMathFunctionClass::Position:
        m_resolution.m_usesPixelPosition = true;
        break;
    case PixelMathFunctionClass::Stochastic:
        m_resolution.m_usesRandom = true;
        break;
    case PixelMathFunctionClass::Constant:
    case PixelMathFunctionClass::Scalar:
    case PixelMathFunctionClass::Aggregate:
    case PixelMathFunctionClass::ImageProperty:
    case PixelMathFunctionClass::Branch:
        break;
    }
}

void PixelMathResolver::checkSampleReads()
{
    const std::span<const PixelMathNode> nodes = m_tree.nodes();
    for (std::uint32_t index = 0u; index < nodes.size(); ++index)
    {
        const PixelMathResolvedNode& resolved = m_resolution.m_nodes[index];
        if (resolved.reference != PixelMathReference::Image || m_consumedAsImage[index])
        {
            continue;
        }
        const PixelMathNode& node = nodes[index];
        const PixelMathImageDeclaration& declaration = m_resolution.m_images[resolved.slot];
        const std::string name(m_tree.name(node.nameIndex));

        if (declaration.extent.width() != m_target->extent.width() ||
            declaration.extent.height() != m_target->extent.height())
        {
            refuse(PixelMathDiagnosticCode::MismatchedImageExtent, node.span, name);
            continue;
        }
        if (resolved.hasChannel)
        {
            if (resolved.channel >= declaration.extent.channelCount())
            {
                refuse(PixelMathDiagnosticCode::ChannelOutOfRange, node.span, name, declaration.extent.channelCount(),
                       resolved.channel);
                continue;
            }
        }
        else if (declaration.extent.channelCount() != 1u &&
                 declaration.extent.channelCount() != m_target->extent.channelCount())
        {
            // A single-channel image is broadcast to every destination channel and an image with the
            // destination's channels lines up with it. Anything between the two would need a rule
            // about which channel goes where, and this language has not stated one.
            refuse(PixelMathDiagnosticCode::MismatchedImageExtent, node.span, name);
            continue;
        }
        if (declaration.numericDomain != m_targetDomain)
        {
            refuse(PixelMathDiagnosticCode::MixedNumericDomains, node.span, name);
        }
    }
}

PixelMathResolution PixelMathResolver::run()
{
    const PixelMathDiagnostic bindingsCheck = m_bindings.check();
    if (isPixelMathRefusal(bindingsCheck))
    {
        m_resolution.m_diagnostics.push_back(bindingsCheck);
        return std::move(m_resolution);
    }
    if (m_tree.empty())
    {
        PixelMathDiagnostic diagnostic;
        diagnostic.code = PixelMathDiagnosticCode::EmptyExpression;
        m_resolution.m_diagnostics.push_back(std::move(diagnostic));
        return std::move(m_resolution);
    }

    const std::uint32_t targetSlot = *m_bindings.metaSymbolImage(PixelMathMetaSymbol::Target);
    const PixelMathImageDeclaration* const target = m_bindings.image(targetSlot);
    if (target == nullptr)
    {
        PixelMathDiagnostic diagnostic;
        diagnostic.code = PixelMathDiagnosticCode::NoTargetImage;
        m_resolution.m_diagnostics.push_back(std::move(diagnostic));
        return std::move(m_resolution);
    }
    m_target = target;
    m_targetDomain = target->numericDomain;
    m_resolution.m_targetExtent = target->extent;
    m_resolution.m_numericDomain = m_targetDomain;
    m_resolution.m_imageSlots.push_back(targetSlot);
    m_resolution.m_images.push_back(*target);

    const std::span<const PixelMathNode> nodes = m_tree.nodes();
    m_resolution.m_nodes.assign(nodes.size(), PixelMathResolvedNode{});
    m_consumedAsImage.assign(nodes.size(), false);

    for (std::uint32_t index = 0u; index < nodes.size(); ++index)
    {
        switch (nodes[index].kind)
        {
        case PixelMathSyntaxKind::Name:
            resolveName(index);
            break;
        case PixelMathSyntaxKind::MetaSymbol:
            resolveMetaSymbol(index);
            break;
        case PixelMathSyntaxKind::Call:
            resolveCall(index);
            break;
        case PixelMathSyntaxKind::Assignment:
            resolveAssignment(index);
            break;
        case PixelMathSyntaxKind::Number:
        case PixelMathSyntaxKind::Unary:
        case PixelMathSyntaxKind::Binary:
        case PixelMathSyntaxKind::Sequence:
            break;
        }
    }

    if (m_resolution.m_diagnostics.empty())
    {
        checkSampleReads();
    }
    if (!m_resolution.m_diagnostics.empty())
    {
        m_resolution.m_nodes.clear();
        m_resolution.m_imageSlots.clear();
        m_resolution.m_images.clear();
        m_resolution.m_constants.clear();
        m_resolution.m_variableInitialValues.clear();
        m_resolution.m_statistics.clear();
        return std::move(m_resolution);
    }

    m_resolution.m_tree = m_tree;
    return std::move(m_resolution);
}

Status PixelMathResolution::status() const
{
    if (m_diagnostics.empty())
    {
        return Status::success();
    }
    return pixelMathDiagnosticStatus(m_diagnostics.front());
}

PixelMathResolution resolvePixelMath(const PixelMathSyntaxTree& tree, const PixelMathBindings& bindings)
{
    PixelMathResolver resolver(tree, bindings);
    return resolver.run();
}

} // namespace an
