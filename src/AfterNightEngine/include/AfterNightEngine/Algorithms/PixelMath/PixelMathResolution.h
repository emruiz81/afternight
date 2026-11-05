// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Deciding what an expression's names mean, and whether it means anything.
///
/// Parsing says what was written. This says whether it can be computed: names are bound, arities are
/// checked and the images an expression combines are matched here, from what their bindings state
/// rather than by reading them. Without this stage an expression that names a hundred-by-hundred image
/// beside a full frame would parse, compile, run, and read past the end of the smaller one.
///
/// Two things are decided here that nothing downstream may revisit.
///
/// **What an image is, is stated by whoever bound it.** Extent, representation and numeric domain
/// come from the bindings, not from a pointer. That is what lets a resolution be checked with no
/// image data present, and it is what makes "these two images mean different kinds of number" a
/// refusal with a name rather than a rescaling nobody asked for.
///
/// **A per-pixel read has to line up with what is being written.** An image read at the pixel being
/// written either has the target's extent or is a single-channel image of the target's size, which is
/// broadcast. Anything else is refused, and the expression's author uses the sampling function, which
/// states its coordinates and is therefore free of the assumption.

#pragma once

#include <AfterNightEngine/Algorithms/PixelMath/PixelMathDiagnostic.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathFunctions.h>
#include <AfterNightEngine/Algorithms/PixelMath/PixelMathSyntax.h>
#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace an
{

/// The most images one expression may name, counting the target.
///
/// A limit rather than a guess: every bound image is a plane pointer the executor holds in a fixed
/// array, and a statistic of every one of them is a pass over a frame. An expression that needs more
/// than this is a sequence of expressions, which is a thing the caller can write.
inline constexpr std::uint32_t kMaxPixelMathImages = 16u;

/// What one bound image is, said by whoever bound it.
///
/// A plain aggregate. Nothing here is derived from sample data: an expression is resolved against
/// these statements, so the same expression can be checked before an image is loaded and executed
/// after.
struct PixelMathImageDeclaration
{
    /// The image's width, height and channel count. There is no default: an extent that nobody
    /// stated is the thing this type exists to make impossible, so a declaration cannot be
    /// value-initialised and left half described.
    ImageExtent extent;

    /// What its samples are.
    SampleType sampleType = SampleType::Float32;

    /// What its samples mean. Two images combined per pixel must agree on this.
    NumericDomain numericDomain = NumericDomain::NormalizedLinear;
};

/// What every name in an expression may refer to.
///
/// Built by the caller before an expression is resolved, and read-only afterwards. Not thread-safe
/// while it is being built; safe to read from any number of threads once it is.
///
/// The target is not the first image bound but whichever image `bindMetaSymbol` names as `$T`, and
/// a resolution against bindings that name none is refused.
class PixelMathBindings
{
public:
    /// Bindings that name nothing.
    PixelMathBindings() = default;

    /// Gives an image a name.
    ///
    /// @param name the spelling the expression will use. Must be a name the language does not
    ///        already own and one no other binding claims.
    /// @param image what the image is.
    /// @return success; `ErrorCode::InvalidArgument` for an empty or already-claimed name or a name
    ///         the language reserves, and `ErrorCode::LimitExceeded` past `kMaxPixelMathImages`.
    AFTERNIGHTENGINE_API Status bindImage(std::string_view name, const PixelMathImageDeclaration& image);

    /// Says which bound image a meta symbol stands for.
    ///
    /// @param symbol which meta symbol.
    /// @param imageName the name of an image already bound.
    /// @return success; `ErrorCode::InvalidArgument` for a symbol outside the enumeration or a name
    ///         no image was bound to.
    AFTERNIGHTENGINE_API Status bindMetaSymbol(PixelMathMetaSymbol symbol, std::string_view imageName);

    /// Declares a name whose value is fixed for the whole run.
    ///
    /// @param name the spelling. Must be free, as `bindImage` requires.
    /// @param value the value. Must be finite.
    /// @return success, or `ErrorCode::InvalidArgument` for an empty, claimed or reserved name or a
    ///         value that is not finite.
    AFTERNIGHTENGINE_API Status declareConstant(std::string_view name, double value);

    /// Declares a name the expression may assign to.
    ///
    /// A variable belongs to one pixel: it is set to its declared initial value before each pixel is
    /// evaluated, and what one pixel left in it is never read by another. That is the only reading
    /// under which an expression means the same thing at every thread count, which is why a
    /// run-scoped mutable symbol is not offered: what one computed would depend on how the rows
    /// happened to be scheduled.
    ///
    /// @param name the spelling. Must be free, as `bindImage` requires.
    /// @param initialValue what it holds at the start of every pixel. Must be finite.
    /// @return success, or `ErrorCode::InvalidArgument` for an empty, claimed or reserved name or a
    ///         value that is not finite.
    AFTERNIGHTENGINE_API Status declareVariable(std::string_view name, double initialValue);

    /// How many images are bound.
    ///
    /// @return the count.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t imageCount() const noexcept;

    /// One bound image's name.
    ///
    /// Borrowed for as long as these bindings live.
    ///
    /// @param slot which image, below `imageCount()`.
    /// @return the name, or an empty view when the slot names none.
    [[nodiscard]] AFTERNIGHTENGINE_API std::string_view imageName(std::uint32_t slot) const noexcept;

    /// One bound image's declaration.
    ///
    /// Borrowed for as long as these bindings live.
    ///
    /// @param slot which image, below `imageCount()`.
    /// @return the declaration, or null when the slot names none.
    [[nodiscard]] AFTERNIGHTENGINE_API const PixelMathImageDeclaration* image(std::uint32_t slot) const noexcept;

    /// Which slot an image name refers to.
    ///
    /// @param name the spelling, compared exactly.
    /// @return the slot, or no value when no image carries that name.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<std::uint32_t> findImage(std::string_view name) const noexcept;

    /// Which image slot a meta symbol stands for.
    ///
    /// @param symbol which meta symbol.
    /// @return the slot, or no value when the symbol is unbound.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<std::uint32_t>
    metaSymbolImage(PixelMathMetaSymbol symbol) const noexcept;

    /// How many constants and variables are declared together.
    ///
    /// @return the count.
    [[nodiscard]] AFTERNIGHTENGINE_API std::uint32_t symbolCount() const noexcept;

    /// One declared symbol's name.
    ///
    /// Borrowed for as long as these bindings live.
    ///
    /// @param index which symbol, below `symbolCount()`.
    /// @return the name, or an empty view when the index names none.
    [[nodiscard]] AFTERNIGHTENGINE_API std::string_view symbolName(std::uint32_t index) const noexcept;

    /// One declared symbol's value, and whether the expression may assign to it.
    ///
    /// @param index which symbol, below `symbolCount()`.
    /// @param assignable set to true when the symbol is a variable, false when it is a constant, and
    ///        left alone when the index names no symbol.
    /// @return the value, or no value when the index names none.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<double> symbolValue(std::uint32_t index,
                                                                         bool& assignable) const noexcept;

    /// Whether these bindings are complete enough to resolve anything against.
    ///
    /// @return `PixelMathDiagnosticCode::None` when `$T` has been bound to an image, and
    ///         `PixelMathDiagnosticCode::NoTargetImage` otherwise.
    [[nodiscard]] AFTERNIGHTENGINE_API PixelMathDiagnostic check() const noexcept;

private:
    struct Symbol
    {
        std::string name;
        double value = 0.0;
        bool assignable = false;
    };

    struct Image
    {
        std::string name;
        PixelMathImageDeclaration declaration;
    };

    std::vector<Image> m_images;
    std::vector<Symbol> m_symbols;
    std::optional<std::uint32_t> m_target;
    std::optional<std::uint32_t> m_active;
    std::optional<std::uint32_t> m_mask;
};

/// What a syntax node produces.
enum class PixelMathValueKind : std::uint8_t
{
    /// One number.
    Scalar = 0u,

    /// A name that stands for a whole image. Only a parameter that asks for one accepts it; used
    /// anywhere else it is the image's sample at the pixel being written.
    ImageReference = 1u
};

/// What a resolved node refers to.
enum class PixelMathReference : std::uint8_t
{
    /// Nothing outside the expression - an operator, a literal, a sequence.
    None = 0u,

    /// One of the expression's images, by its index in `PixelMathResolution::imageSlots`.
    Image = 1u,

    /// One of the expression's constants, by its index in `PixelMathResolution::constants`.
    Constant = 2u,

    /// One of the expression's variables, by its index in
    /// `PixelMathResolution::variableInitialValues`.
    Variable = 3u,

    /// A function of the registry, named by the node's `function`.
    Function = 4u,

    /// A statistic of a whole image, by its index in `PixelMathResolution::statistics`.
    ImageStatistic = 5u
};

/// One syntax node's meaning.
///
/// There is exactly one of these per syntax node, at the same index, so a consumer walks the tree
/// and reads the meaning beside it rather than carrying a second map.
struct PixelMathResolvedNode
{
    /// What the node produces.
    PixelMathValueKind valueKind = PixelMathValueKind::Scalar;

    /// What it refers to.
    PixelMathReference reference = PixelMathReference::None;

    /// Which image, constant, variable or statistic, as `reference` says. Zero elsewhere.
    std::uint32_t slot = 0u;

    /// Which function a `Function` node calls. `AbsoluteValue` elsewhere.
    PixelMathFunctionId function = PixelMathFunctionId::AbsoluteValue;

    /// Which channel an image reference selected. Zero when it selected none.
    std::uint32_t channel = 0u;

    /// Whether a channel was selected at all.
    bool hasChannel = false;
};

/// A statistic of a whole image that an expression needs.
enum class PixelMathStatistic : std::uint8_t
{
    /// The smallest participating sample.
    Minimum = 0u,

    /// The largest participating sample.
    Maximum = 1u,

    /// The arithmetic mean.
    Mean = 2u,

    /// The median.
    Median = 3u,

    /// The population standard deviation.
    StandardDeviation = 4u,

    /// The population variance.
    Variance = 5u,

    /// The median of the absolute deviations from the median.
    MedianAbsoluteDeviation = 6u,

    /// The mean absolute distance from the arithmetic mean.
    AverageAbsoluteDeviation = 7u,

    /// The arithmetic mean of the squared samples.
    MeanOfSquares = 8u,

    /// The sum of the squared samples.
    SumOfSquares = 9u,

    /// Tukey's biweight midvariance with tuning constant nine.
    BiweightMidvariance = 10u,

    /// The robust row-wise first-difference background-noise estimate.
    BackgroundNoise = 11u
};

/// Whether a value names a statistic this build computes.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param statistic the value to inspect.
/// @return true for every supported statistic.
[[nodiscard]] constexpr bool isPixelMathStatistic(PixelMathStatistic statistic) noexcept
{
    return static_cast<std::uint8_t>(statistic) <= static_cast<std::uint8_t>(PixelMathStatistic::BackgroundNoise);
}

/// One statistic of one image, computed once before any pixel is written.
struct PixelMathStatisticRequest
{
    /// Which of the expression's images, by its index in `PixelMathResolution::imageSlots`.
    std::uint32_t image = 0u;

    /// Which statistic.
    PixelMathStatistic statistic = PixelMathStatistic::Mean;

    /// Whether two requests are the same request.
    ///
    /// @param left one request.
    /// @param right the other.
    /// @return true when both the image and the statistic agree.
    [[nodiscard]] friend constexpr bool operator==(const PixelMathStatisticRequest& left,
                                                   const PixelMathStatisticRequest& right) noexcept = default;
};

/// What resolving one expression against one set of bindings produced.
///
/// Returned by value for the reason a parse is: a front end's answer is every problem the expression
/// has, and a caller that only wants the returned failure asks for `status()`. A refused resolution
/// keeps its diagnostics and, when the target was found, the target's extent and numeric domain;
/// its tree, nodes, images, constants, variables and statistics are empty.
///
/// Copyable and movable, and safe to read from several threads at once because it cannot be changed
/// once returned.
class PixelMathResolution
{
public:
    /// A resolution that resolved nothing.
    PixelMathResolution() = default;

    /// Copies a resolution, including its nodes and requests.
    PixelMathResolution(const PixelMathResolution&) = default;

    /// Takes over a resolution's nodes and requests.
    PixelMathResolution(PixelMathResolution&&) noexcept = default;

    /// Copies a resolution over this one.
    ///
    /// @return this resolution.
    PixelMathResolution& operator=(const PixelMathResolution&) = default;

    /// Takes over a resolution's nodes and requests, leaving the other empty.
    ///
    /// @return this resolution.
    PixelMathResolution& operator=(PixelMathResolution&&) noexcept = default;

    /// Releases the nodes and requests.
    ~PixelMathResolution() = default;

    /// Whether the expression means something.
    ///
    /// @return true exactly when no diagnostic was raised.
    [[nodiscard]] bool succeeded() const noexcept
    {
        return m_diagnostics.empty();
    }

    /// Every reason the expression was refused, in the order the resolver found them.
    ///
    /// Nodes are resolved in the tree's node order, which puts a call after its arguments, so this
    /// is close to source order but not the same. The checks on per-pixel reads - extent, channel
    /// and numeric domain - run only when nothing else was refused. Borrowed for as long as this
    /// resolution lives.
    ///
    /// @return the diagnostics, empty on success.
    [[nodiscard]] std::span<const PixelMathDiagnostic> diagnostics() const noexcept
    {
        return std::span<const PixelMathDiagnostic>(m_diagnostics);
    }

    /// The tree this resolution is about.
    ///
    /// Borrowed for as long as this resolution lives; a copy of the tree it was resolved from, so
    /// the resolution outlives the parse.
    ///
    /// @return the tree, or an empty tree when the expression was refused.
    [[nodiscard]] const PixelMathSyntaxTree& tree() const noexcept
    {
        return m_tree;
    }

    /// One meaning per syntax node, at the same index.
    ///
    /// Borrowed for as long as this resolution lives.
    ///
    /// @return the resolved nodes, empty when the expression was refused.
    [[nodiscard]] std::span<const PixelMathResolvedNode> nodes() const noexcept
    {
        return std::span<const PixelMathResolvedNode>(m_nodes);
    }

    /// Which binding each of the expression's images is, in the order an execution binds them.
    ///
    /// Index zero is always the target image, whether or not the expression reads it, so an
    /// execution always knows what it is writing into.
    ///
    /// Borrowed for as long as this resolution lives.
    ///
    /// @return the binding slots.
    [[nodiscard]] std::span<const std::uint32_t> imageSlots() const noexcept
    {
        return std::span<const std::uint32_t>(m_imageSlots);
    }

    /// What each of the expression's images is.
    ///
    /// The same order as `imageSlots`, copied from the bindings so the resolution stands on its own.
    ///
    /// Borrowed for as long as this resolution lives.
    ///
    /// @return the declarations.
    [[nodiscard]] std::span<const PixelMathImageDeclaration> images() const noexcept
    {
        return std::span<const PixelMathImageDeclaration>(m_images);
    }

    /// The values of the constants the expression used.
    ///
    /// Borrowed for as long as this resolution lives.
    ///
    /// @return the constants.
    [[nodiscard]] std::span<const double> constants() const noexcept
    {
        return std::span<const double>(m_constants);
    }

    /// What each variable the expression used holds at the start of every pixel.
    ///
    /// Borrowed for as long as this resolution lives.
    ///
    /// @return the initial values, one per variable.
    [[nodiscard]] std::span<const double> variableInitialValues() const noexcept
    {
        return std::span<const double>(m_variableInitialValues);
    }

    /// Every whole-image statistic the expression needs.
    ///
    /// Distinct: two calls asking for one image's median are one request. Borrowed for as long as
    /// this resolution lives.
    ///
    /// @return the requests.
    [[nodiscard]] std::span<const PixelMathStatisticRequest> statistics() const noexcept
    {
        return std::span<const PixelMathStatisticRequest>(m_statistics);
    }

    /// What the expression's images agree their samples mean.
    ///
    /// Every image read per pixel shares this, which is what the domain check establishes.
    ///
    /// @return the numeric domain.
    [[nodiscard]] NumericDomain numericDomain() const noexcept
    {
        return m_numericDomain;
    }

    /// The extent of the image being written.
    ///
    /// @return the target's extent, or no value for a resolution that was refused before a target
    ///         was read.
    [[nodiscard]] std::optional<ImageExtent> targetExtent() const noexcept
    {
        return m_targetExtent;
    }

    /// Whether the expression's value depends on where in the image it is being evaluated.
    ///
    /// @return true when it calls a position function or the sampling function.
    [[nodiscard]] bool usesPixelPosition() const noexcept
    {
        return m_usesPixelPosition;
    }

    /// Whether the expression draws from the execution's random stream.
    ///
    /// @return true when it calls the random function.
    [[nodiscard]] bool usesRandom() const noexcept
    {
        return m_usesRandom;
    }

    /// Whether the expression reads an image at coordinates it states.
    ///
    /// @return true when it calls the sampling function.
    [[nodiscard]] bool usesImageSample() const noexcept
    {
        return m_usesImageSample;
    }

    /// Whether the expression assigns to a variable.
    ///
    /// @return true when it holds an assignment.
    [[nodiscard]] bool usesAssignment() const noexcept
    {
        return m_usesAssignment;
    }

    /// Whether the expression converts a pixel's colour.
    ///
    /// @return true when it calls a colour-component function.
    [[nodiscard]] bool usesColorComponent() const noexcept
    {
        return m_usesColorComponent;
    }

    /// The returned failure this resolution stands for.
    ///
    /// Built from the first diagnostic in `diagnostics()` order.
    ///
    /// @return success when the expression resolved, and otherwise whatever
    ///         `pixelMathDiagnosticStatus` makes of the first diagnostic.
    [[nodiscard]] AFTERNIGHTENGINE_API Status status() const;

private:
    friend class PixelMathResolver;

    PixelMathSyntaxTree m_tree;
    std::vector<PixelMathDiagnostic> m_diagnostics;
    std::vector<PixelMathResolvedNode> m_nodes;
    std::vector<std::uint32_t> m_imageSlots;
    std::vector<PixelMathImageDeclaration> m_images;
    std::vector<double> m_constants;
    std::vector<double> m_variableInitialValues;
    std::vector<PixelMathStatisticRequest> m_statistics;
    NumericDomain m_numericDomain = NumericDomain::NormalizedLinear;
    std::optional<ImageExtent> m_targetExtent{};
    bool m_usesPixelPosition = false;
    bool m_usesRandom = false;
    bool m_usesImageSample = false;
    bool m_usesAssignment = false;
    bool m_usesColorComponent = false;
};

/// The one thing that resolves a tree. Defined in the engine's implementation.
///
/// Named here rather than published for the reason the parser is: it exists so that the type above
/// has exactly one producer.
class PixelMathResolver;

/// Decides what a parsed expression's names mean, and whether it means anything.
///
/// The tree and the bindings are borrowed for the call; the returned resolution copies what it needs
/// from both, so it outlives either.
///
/// Every refusal is a diagnostic rather than an exception. The resolution reports every problem it
/// finds, and a node whose meaning could not be decided is left unresolved rather than guessed at,
/// so one unknown name does not become one diagnostic per use of it.
///
/// Thread-safe and reentrant.
///
/// @param tree the parsed expression. An empty tree is refused as an empty expression.
/// @param bindings what every name may refer to. Bindings with no target image are refused.
/// @return the resolved expression, or the diagnostics that refused it.
[[nodiscard]] AFTERNIGHTENGINE_API PixelMathResolution resolvePixelMath(const PixelMathSyntaxTree& tree,
                                                                        const PixelMathBindings& bindings);

} // namespace an
