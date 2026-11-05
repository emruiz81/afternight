// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The measurement-preservation invariant for a demosaic benchmark, and VNG4's definition.
//
// There is no closed form for a demosaiced picture, and taking one implementation's answer as the
// oracle would make the comparison say only that another one differs. What the engine can be held to
// instead is the measurement they started from: every photosite recorded one colour, and a
// reconstruction that changed that number changed a measurement rather than estimating the two it
// did not have.
//
// So the residual below is a re-mosaic. It samples the reconstructed picture at every site a method
// keeps, in the channel that site's filter measured, and reports the root-mean-square difference
// against the mosaic. It is engine-independent and needs no fixture beyond the input. It is not an
// oracle for the two interpolated channels: a reconstruction can preserve every measured site and
// still make poor estimates elsewhere.
//
// VNG4 does not keep every measured site, by definition: it estimates both green phases of a
// four-colour working image and reports their mean, so the green it reports at a green site mixes in
// its estimate of the other phase. Its red and blue sites are kept. Its output is therefore also held
// to the method's definition, evaluated here in double precision for every interpolated colour.
//
// The probe text beside it is the digest source: a sparse, deterministic sample of the reconstructed
// picture, so two runs of one engine compare byte for byte while the text stays small enough to hash
// cheaply.

#pragma once

#include "Results.h"
#include "Scenario.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace an::bench
{

/// Which filter covers each pixel of a repeating block, and where the block starts.
///
/// The block is carried as its entries rather than as a pattern name, so one description serves a
/// two-by-two arrangement and a six-by-six table alike and the oracle needs no knowledge of either
/// engine's vocabulary. Entries are zero for red, one for green and two for blue.
struct MosaicBlock
{
    /// The block's entries, row-major, `size * size` of them.
    std::span<const std::uint8_t> entries;

    /// How many pixels the block covers in each direction.
    std::uint32_t size = 2u;

    /// Column, within the block, that the picture's first column sits at.
    std::uint32_t originX = 0u;

    /// Row, within the block, that the picture's first row sits at.
    std::uint32_t originY = 0u;

    /// Which filter covers one pixel.
    ///
    /// @param x the picture column.
    /// @param y the picture row.
    /// @return zero for red, one for green, two for blue; zero for a malformed block.
    [[nodiscard]] std::uint8_t colourAt(std::uint32_t x, std::uint32_t y) const noexcept;
};

/// The band of the picture's edge every demosaic residual and definition check leaves out, in pixels.
///
/// A border pixel is filled by a method's own border rule rather than by its estimate, and the widest
/// such band a method fills sets this width, so no method is measured on a ring it filled by another
/// rule.
inline constexpr std::uint32_t kDemosaicResidualBorder = 16u;

/// Which measured sites a method's output keeps.
enum class KeptMeasurements
{
    /// Every site reports the sample its filter measured.
    Every,

    /// Red and blue sites report their samples; a green site reports a mix of both green phases.
    RedAndBlue
};

/// The measured sites the method a demosaic row names keeps.
///
/// @param method the method as the rows name it, for example "Vng4".
/// @return `RedAndBlue` for VNG4, `Every` for every other method.
[[nodiscard]] KeptMeasurements keptMeasurements(std::string_view method) noexcept;

/// How far a reconstruction moved the measurements it was given.
///
/// The band the shared border rule fills is excluded, because a border pixel is an average of its
/// neighbourhood by design rather than a reconstruction of a measurement, and including it would
/// make the residual a measure of the picture's edge rather than of the estimate.
///
/// @param red the reconstructed red plane, tightly packed.
/// @param green the reconstructed green plane.
/// @param blue the reconstructed blue plane.
/// @param mosaic the samples the reconstruction started from.
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param block which filter covers each pixel.
/// @param border how many pixels of the picture edge to leave out.
/// @param kept which measured sites the method keeps; the others are left out.
/// @return the root-mean-square difference, or zero when nothing is left to measure.
[[nodiscard]] double demosaicResidual(std::span<const float> red, std::span<const float> green,
                                      std::span<const float> blue, std::span<const float> mosaic, std::uint32_t width,
                                      std::uint32_t height, const MosaicBlock& block, std::uint32_t border,
                                      KeptMeasurements kept = KeptMeasurements::Every);

/// The relative distance from VNG4's threshold inside which a direction's choice is a tie.
///
/// VNG4 averages the directions whose gradient is at most the smallest gradient plus half the largest.
/// The comparison is discontinuous, so where a direction's gradient lies within rounding of that
/// threshold the arithmetic that evaluates it decides the choice - Float32 in the unit interval and
/// exact sixteen-bit codes choose differently on a captured frame - and the output can move by a large
/// fraction of the local contrast. A direction within this fraction of the largest gradient of its
/// threshold is undecided; one hundred times the spread single-precision arithmetic leaves.
inline constexpr double kVng4TieMargin = 1e-4;

/// The largest difference a decided VNG4 sample may have from the definition, in units of the unit interval.
inline constexpr double kVng4DecidedTolerance = 1e-6;

/// What a VNG4 output was held to.
struct Vng4DefinitionEvidence
{
    /// The largest difference from the definition over the decided samples, both held to the unit interval.
    double largestDecidedError = 0.0;

    /// Interior samples, three to a pixel, whose value the definition decides.
    std::uint64_t decidedSamples = 0u;

    /// Interior samples compared, three to a pixel.
    std::uint64_t interiorSamples = 0u;

    /// Interior pixels whose own direction choice is a tie. Their green and the red and blue of every
    /// pixel that reads them are undecided.
    std::uint64_t tiedPixels = 0u;

    /// Root-mean-square difference between the output's green and the measurement at the interior
    /// green sites: how far the method's mix of the two green phases moved them. Reported, not judged.
    double greenSiteAdjustmentRms = 0.0;
};

/// VNG4's definition of a picture, evaluated in double precision.
struct Vng4Definition
{
    /// Red, row-major; zero outside the interior.
    std::vector<double> red;

    /// Green, row-major; zero outside the interior.
    std::vector<double> green;

    /// Blue, row-major; zero outside the interior.
    std::vector<double> blue;

    /// Per pixel, a bit for each colour whose value the definition decides: one red, two green, four blue.
    std::vector<std::uint8_t> decided;
};

/// Evaluates VNG4's definition over the interior of a picture, as `vng4DefinitionEvidence` judges it.
///
/// It holds whole planes, so it is for a small picture; the evidence is the bounded form.
///
/// @param mosaic the samples the method evaluated.
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param block which filter covers each pixel; it must be a two-by-two Bayer block.
/// @param border how many pixels of the picture edge to leave out; at least four.
/// @return the definition, or nothing for a block that is not Bayer, a short mosaic or a picture too small.
[[nodiscard]] std::optional<Vng4Definition> vng4Definition(std::span<const float> mosaic, std::uint32_t width,
                                                           std::uint32_t height, const MosaicBlock& block,
                                                           std::uint32_t border);

/// Holds a VNG4 reconstruction to the method's definition.
///
/// The definition is dcraw's four-colour variable-number-of-gradients interpolation with its two green
/// estimates averaged, then red and blue rebuilt from that green by colour differences: the first
/// interpolation weighs edge neighbours two and diagonal neighbours one; 64 gradient terms score eight
/// directions; the directions at most the smallest score plus half the largest are averaged; at a green
/// site the colour differences along the measured axis and at a red or blue site the four diagonal
/// ones give the missing colours. It is evaluated in double precision over bands of rows, so its
/// memory is bounded. Both outputs are held to the unit interval before they are compared, which is
/// the domain the rows state.
///
/// @param red the reconstructed red plane, tightly packed.
/// @param green the reconstructed green plane.
/// @param blue the reconstructed blue plane.
/// @param mosaic the samples the reconstruction started from.
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param block which filter covers each pixel; it must be a two-by-two Bayer block.
/// @param border how many pixels of the picture edge to leave out; at least four.
/// @return the evidence, or nothing for a block that is not Bayer, short planes or a picture too small.
[[nodiscard]] std::optional<Vng4DefinitionEvidence>
vng4DefinitionEvidence(std::span<const float> red, std::span<const float> green, std::span<const float> blue,
                       std::span<const float> mosaic, std::uint32_t width, std::uint32_t height,
                       const MosaicBlock& block, std::uint32_t border);

/// The parameter naming the definition a demosaic row's output is also held to.
inline constexpr const char* kDemosaicQualityOracleParameter = "qualityOracle";
/// The value that holds a VNG4 row to `vng4DefinitionEvidence`.
inline constexpr const char* kVng4DefinitionOracle = "vng4-definition";

/// Records a demosaic row's correctness evidence, which every run computes alike.
///
/// Sets the correctness metric to the residual over the sites the row's method keeps, outside
/// `kDemosaicResidualBorder`, and for a row naming `kVng4DefinitionOracle` writes the definition's
/// evidence as the `vng4*` counters. A two-by-two row that names the oracle but cannot be checked
/// writes no counters, which `demosaicEvidenceValid` refuses.
///
/// @param scenario the row.
/// @param red the reconstructed red plane, tightly packed.
/// @param green the reconstructed green plane.
/// @param blue the reconstructed blue plane.
/// @param mosaic the samples the reconstruction started from, in the units the planes are in.
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param block which filter covers each pixel.
/// @param out the record the evidence is written to.
/// @param evaluated the samples the method evaluated, when an engine states that it evaluates something
///     other than the mosaic it was given - an input rounded to sixteen-bit codes, say - or
///     empty for the mosaic itself. The residual is always taken against the mosaic.
void recordDemosaicEvidence(const ScenarioSpec& scenario, std::span<const float> red, std::span<const float> green,
                            std::span<const float> blue, std::span<const float> mosaic, std::uint32_t width,
                            std::uint32_t height, const MosaicBlock& block, ResultRecord& out,
                            std::span<const float> evaluated = {});

/// True for a demosaic row naming a method, whose residual is judged whatever its correctness class says.
[[nodiscard]] bool requiresDemosaicEvidence(const ScenarioSpec& scenario);

/// Whether a record meets a demosaic row's own rules.
///
/// The residual must be present and within the row's tolerance - nothing excuses a reconstruction
/// that moved a measurement it keeps - and a row naming
/// `kVng4DefinitionOracle` must carry the definition's evidence with every decided sample within
/// `kVng4DecidedTolerance` and at least half the interior samples decided.
///
/// @param scenario the row.
/// @param record the result for it.
/// @return true when the record meets every rule.
[[nodiscard]] bool demosaicEvidenceValid(const ScenarioSpec& scenario, const ResultRecord& record);

/// A sparse deterministic sample of a reconstructed picture, for the output digest.
///
/// @param red the reconstructed red plane, tightly packed.
/// @param green the reconstructed green plane.
/// @param blue the reconstructed blue plane.
/// @param width how many pixels one row holds.
/// @param height how many rows.
/// @param stride how many pixels to step between probes.
/// @return the text, one probe per line.
[[nodiscard]] std::string demosaicProbeText(std::span<const float> red, std::span<const float> green,
                                            std::span<const float> blue, std::uint32_t width, std::uint32_t height,
                                            std::uint32_t stride);

/// A digest over every byte of all three reconstructed planes.
///
/// The planes are hashed in red, green, blue order. Unlike the sparse benchmark digest, this is
/// suitable for an exact claim: a changed sample anywhere changes the result.
///
/// @param red the reconstructed red plane.
/// @param green the reconstructed green plane.
/// @param blue the reconstructed blue plane.
/// @return the lowercase SHA-256 digest, or an empty string when the plane lengths disagree.
[[nodiscard]] std::string demosaicExactDigest(std::span<const float> red, std::span<const float> green,
                                              std::span<const float> blue);

/// How many pixels to step between digest probes.
inline constexpr std::uint32_t kDemosaicProbeStride = 37u;

} // namespace an::bench
