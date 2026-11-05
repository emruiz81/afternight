// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// The product's display paths, as neutral cases: what each one does to an image between the samples it holds and
// the bytes a screen or a file receives.
//
// The product shows an image in several places - the viewer, the real-time preview, the thumbnails, an applied
// stretch, an exported sequence, the command line - each with its own promotion, statistics, linking and conversion
// to bytes. A case states one of those paths over one neutral input, with the decisions the path takes written as
// plain values: whether it stretches, whether colour is linked or balanced, the saturation and the brightness and
// contrast levels it applies, what it hands on, and the product source whose calls it follows. Its record is the
// reference result; the engine fixture executable presents the same input through the engine's presentation with
// the same decisions, and compares with the record by its classification.
//
// A record's id is `<operation>/<input>`, and every record lives in `presentation.expectations`.

#include "FixtureExpectation.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace an::tests::fixtures
{

/// Which product display path a case follows.
enum class PresentationPath : std::uint8_t
{
    /// The viewer in linear mode: the widget's conversion of the shown image to bytes, and nothing else.
    ViewerLinear,
    /// The viewer in automatic-stretch mode: its promotion, optional linked balance, statistics, stretch,
    /// saturation and brightness and contrast, then its conversion to bytes.
    ViewerAutostretch,
    /// The real-time preview: statistics taken once from the whole promoted source and handed to the viewer, which
    /// in its performance mode converts to packed 32-bit pixels.
    PreviewRgba,
    /// The viewer's subsampled fast path below full zoom: every n-th sample of every n-th row, straight to bytes.
    PreviewSubsampled,
    /// The file manager's thumbnail stretch.
    Thumbnail,
    /// The integration workflow's preview thumbnail: the stretch and the conversion to bytes.
    ThumbnailGenerator,
    /// Apply Stretch: the viewer's stretch written into a copy in the source's representation.
    Apply,
    /// The sequence exporter's per-frame stretch, in the frame's representation.
    ExportSequence,
    /// The command-line tool's stretch, in the source's representation.
    CommandLine,
    /// The lens: a region cropped and converted to bytes.
    Lens
};

/// What a case's record holds.
enum class PresentationProduct : std::uint8_t
{
    /// One byte per sample with the source's channels.
    Bytes,
    /// Four bytes per pixel: red, green, blue and alpha.
    Rgba,
    /// The stretched image in the source's own representation.
    Native
};

/// One product display path over one input.
struct PresentationCase
{
    /// The operation key, `presentation.<path>`; the record's id is `<operation>/<input>`.
    std::string operation;
    /// The behavior or standard fixture the case runs over.
    std::string input;
    /// What the product does, in words; the record states it.
    std::string text;
    /// The product source whose calls the case follows, as `<library path>:<line>`.
    std::string source;
    PresentationPath path = PresentationPath::ViewerLinear;
    PresentationProduct product = PresentationProduct::Bytes;
    /// Whether the path stretches automatically.
    bool autostretch = true;
    /// The viewer's high-quality gradient: keep floats and sixteen bits as they are rather than converting to
    /// sixteen-bit codes first.
    bool highQualityGradient = true;
    /// Whether colour channels take one transfer.
    bool linked = false;
    /// Whether colour is neutralized before the stretch; the viewer does it only when linked.
    bool neutralize = false;
    /// Whether colour is calibrated against its own white and background; likewise only when linked.
    bool calibrate = false;
    /// The saturation coefficient; zero skips the step.
    double saturation = 0.0;
    /// The saturation background factor.
    double saturationBackground = 1.0;
    /// Whether the saturation background starts from the green channel's mean rather than its median.
    bool saturationFromMean = false;
    /// The brightness level, on a scale of 255 to a full step.
    int brightness = 0;
    /// The contrast level, on the brightness level's scale.
    int contrast = 0;
    /// Every n-th sample of every n-th row, for the subsampled path.
    std::uint32_t subsample = 1u;
    /// The lens's region.
    std::uint32_t cropX = 0u;
    std::uint32_t cropY = 0u;
    std::uint32_t cropWidth = 0u;
    std::uint32_t cropHeight = 0u;
    /// How the engine's answer is held to the record.
    ExpectationClass classification = ExpectationClass::Tolerance;
    double absoluteTolerance = 1.0;
    double relativeTolerance = 0.0;
    /// Why the answers differ, for an accepted case.
    std::string acceptedReason;
    /// Why a tolerance is what it is, for a case whose bound is wider than one code.
    std::string note;

    [[nodiscard]] std::string id() const
    {
        return operation + "/" + input;
    }
};

/// The record file every presentation record lives in.
inline constexpr const char* kPresentationExpectationFile = "presentation.expectations";

/// Every case, in a stable order; ids are unique.
[[nodiscard]] const std::vector<PresentationCase>& presentationCases();

/// The case of this id, or null.
[[nodiscard]] const PresentationCase* presentationCase(std::string_view id);

/// The offset the engine adds for a brightness level: the level over 255.
[[nodiscard]] double presentationBrightnessOffset(int level) noexcept;

/// The factor the engine scales by about one half for a contrast level: the eight-bit editing convention
/// `259 (level + 255) / (255 (259 - level))`, which is one at level zero.
[[nodiscard]] double presentationContrastFactor(int level) noexcept;

} // namespace an::tests::fixtures
