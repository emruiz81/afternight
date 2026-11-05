// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "PresentationCases.h"

#include <utility>

namespace an::tests::fixtures
{

namespace
{

constexpr const char* kViewer = "src/AfterNightControls/widgets/ImageViewWidget.cpp";

PresentationCase base(std::string operation, std::string input, PresentationPath path, PresentationProduct product,
                      std::string text, std::string source)
{
    PresentationCase made;
    made.operation = std::move(operation);
    made.input = std::move(input);
    made.path = path;
    made.product = product;
    made.text = std::move(text);
    made.source = std::move(source);
    return made;
}

/// A case whose answer differs on purpose, compared and reported but never failing.
PresentationCase accepted(PresentationCase made, std::string reason)
{
    made.classification = ExpectationClass::Accepted;
    made.acceptedReason = std::move(reason);
    return made;
}

/// An answer held to a bound wider than the conversion's, for the reason stated.
PresentationCase bounded(PresentationCase made, double absolute, std::string note)
{
    made.absoluteTolerance = absolute;
    made.note = std::move(note);
    return made;
}

const std::string kSignedOffset =
    "the record shows a signed sample shifted by half its range, so zero is mid-grey; the presentation converts "
    "it as the engine converts every signed sample, so a negative one is below the black point";
const std::string kInfinity =
    "the record clamps a positive infinity to white; the presentation treats every non-finite sample as invalid "
    "and shows it black";
const std::string kHistogramMedian =
    "the record's median and median deviation are histogram estimates with one bin per sample on these inputs, "
    "so its stretch sits up to a bin away from the presentation's exact one";

std::vector<PresentationCase> build()
{
    std::vector<PresentationCase> cases;

    // The viewer in linear mode: the widget converts the shown image to eight bits with the engine's per-sample
    // conversion, which truncates.
    for (const char* input : {"scene-mono-uint8", "scene-rgb-uint16", "scene-rgb-float32", "gradient-mono-uint32"})
    {
        PresentationCase made =
            base("presentation.viewer-linear", input, PresentationPath::ViewerLinear, PresentationProduct::Bytes,
                 "the viewer in linear mode: the shown image converted to bytes", std::string(kViewer) + ":1939");
        made.autostretch = false;
        cases.push_back(std::move(made));
    }
    {
        PresentationCase made =
            base("presentation.viewer-linear", "scene-mono-int16", PresentationPath::ViewerLinear,
                 PresentationProduct::Bytes, "the viewer in linear mode: the shown image converted to bytes",
                 std::string(kViewer) + ":1939");
        made.autostretch = false;
        cases.push_back(accepted(std::move(made), kSignedOffset));
    }
    {
        PresentationCase made =
            base("presentation.viewer-linear", "nonfinite-mono-float32", PresentationPath::ViewerLinear,
                 PresentationProduct::Bytes, "the viewer in linear mode: the shown image converted to bytes",
                 std::string(kViewer) + ":1939");
        made.autostretch = false;
        cases.push_back(accepted(std::move(made), kInfinity));
    }

    // The viewer in automatic-stretch mode with its defaults: a high-quality gradient, channels unlinked.
    for (const char* input : {"scene-rgb-float32", "scene-rgb-uint16", "scene-mono-float32", "scene-mono-uint8"})
    {
        cases.push_back(base("presentation.viewer-autostretch", input, PresentationPath::ViewerAutostretch,
                             PresentationProduct::Bytes,
                             "the viewer's automatic stretch with its defaults, then its conversion to bytes",
                             std::string(kViewer) + ":1665"));
    }

    // The same without the high-quality gradient: a float image becomes sixteen-bit codes before the stretch.
    {
        PresentationCase made =
            base("presentation.viewer-compact", "scene-rgb-float32", PresentationPath::ViewerAutostretch,
                 PresentationProduct::Bytes, "the viewer's automatic stretch over sixteen-bit codes, then bytes",
                 std::string(kViewer) + ":1699");
        made.highQualityGradient = false;
        cases.push_back(std::move(made));
    }

    // The viewer linked, with both balances, saturation and brightness and contrast, over a star field whose every
    // channel has a background inside the balance's interval.
    {
        PresentationCase made = base("presentation.viewer-balanced", "stars-rgb-float32",
                                     PresentationPath::ViewerAutostretch, PresentationProduct::Bytes,
                                     "the viewer's linked stretch after neutralization and calibration, with "
                                     "saturation 0.5 and brightness 12 and contrast 18, then bytes",
                                     std::string(kViewer) + ":1709");
        made.linked = true;
        made.neutralize = true;
        made.calibrate = true;
        made.saturation = 0.5;
        made.brightness = 12;
        made.contrast = 18;
        cases.push_back(std::move(made));
    }

    // The real-time preview: statistics from the whole promoted source, without the high-quality gradient the
    // preview forces off, and the performance mode's packed pixels.
    for (const char* input : {"scene-rgb-float32", "scene-rgb-uint16"})
    {
        PresentationCase made =
            base("presentation.preview-rgba", input, PresentationPath::PreviewRgba, PresentationProduct::Rgba,
                 "the real-time preview's whole-image statistics and stretch, packed as 32-bit "
                 "pixels",
                 "src/AfterNightControls/controls/ImageRTPreviewControl.cpp:400");
        made.highQualityGradient = false;
        cases.push_back(std::move(made));
    }

    // The subsampled fast path below full zoom, in linear mode.
    for (const std::pair<const char*, std::uint32_t> input :
         {std::pair<const char*, std::uint32_t>{"scene-rgb-uint16", 2u},
          {"scene-mono-float32", 3u},
          {"scene-mono-uint8", 2u}})
    {
        PresentationCase made = base("presentation.preview-subsampled", input.first,
                                     PresentationPath::PreviewSubsampled, PresentationProduct::Bytes,
                                     "the viewer's subsampled fast path: every n-th sample of every n-th row to bytes",
                                     std::string(kViewer) + ":74");
        made.autostretch = false;
        made.subsample = input.second;
        cases.push_back(std::move(made));
    }

    // The file manager's thumbnail: its stretch, each colour channel on its own, in the source's representation.
    for (const char* input : {"scene-rgb-uint16", "scene-rgb-float32", "scene-mono-float32"})
    {
        cases.push_back(base("presentation.thumbnail", input, PresentationPath::Thumbnail, PresentationProduct::Native,
                             "the file manager's thumbnail stretch", "src/AfterNightControls/app/FileManager.cpp:500"));
    }

    // The integration workflow's preview thumbnail: unlinked colour on purpose, then bytes.
    for (const char* input : {"scene-rgb-float32", "stars-rgb-float32"})
    {
        cases.push_back(
            base("presentation.thumbnail-generator", input, PresentationPath::ThumbnailGenerator,
                 PresentationProduct::Bytes, "the workflow preview thumbnail's unlinked stretch, then bytes",
                 "src/AfterNightProcesses/processes/ImageIntegration/workflow/PreviewThumbnailGenerator.cpp:95"));
    }

    // Apply Stretch with its defaults, written in the source's representation.
    for (const char* input : {"scene-rgb-uint16", "scene-mono-float32"})
    {
        cases.push_back(base("presentation.apply", input, PresentationPath::Apply, PresentationProduct::Native,
                             "Apply Stretch with its defaults, in the source's representation",
                             "src/AfterNightControls/controls/AutostretchApply.cpp:246"));
    }

    // The sequence exporter, linked, with saturation from the mean and brightness and contrast.
    {
        PresentationCase made = base("presentation.export", "scene-rgb-float32", PresentationPath::ExportSequence,
                                     PresentationProduct::Native,
                                     "the sequence exporter's linked stretch with saturation 0.4 from the mean and "
                                     "brightness 10 and contrast 15, in the frame's representation",
                                     "src/AfterNightControls/controls/ExportSequenceControl.cpp:457");
        made.linked = true;
        made.saturation = 0.4;
        made.saturationFromMean = true;
        made.brightness = 10;
        made.contrast = 15;
        cases.push_back(std::move(made));
    }

    // The command-line tool's stretch.
    for (const char* input : {"scene-rgb-uint16", "scene-mono-float32"})
    {
        cases.push_back(base("presentation.cli", input, PresentationPath::CommandLine, PresentationProduct::Native,
                             "the command-line tool's unlinked stretch, in the source's representation",
                             "src/AfterNightCLI/main.cpp:51"));
    }

    // The lens: a region cropped and converted to bytes.
    for (const char* input : {"scene-rgb-float32", "scene-rgb-uint16", "scene-mono-int16"})
    {
        PresentationCase made =
            base("presentation.lens", input, PresentationPath::Lens, PresentationProduct::Bytes,
                 "the lens's region cropped and converted to bytes", std::string(kViewer) + ":2206");
        made.autostretch = false;
        made.cropX = 3u;
        made.cropY = 2u;
        made.cropWidth = 6u;
        made.cropHeight = 4u;
        if (std::string_view(input) == "scene-mono-int16")
        {
            made = accepted(std::move(made), kSignedOffset);
        }
        cases.push_back(std::move(made));
    }

    // A stretched answer is held to the bound the record's statistics allow. Its median and median deviation are
    // estimates from a histogram with as many bins as the image has samples, so on these small inputs its black point
    // and balance sit up to a bin away from the exact ones the presentation uses, and every stretched sample moves
    // with them: by up to two codes in bytes, by up to 167 codes of a sixteen-bit result, and by up to 1.0e-4 of a
    // floating one, or 4.2e-3 where saturation and contrast amplify it, or 7.7e-3 - two display levels - where each
    // colour channel takes its own, steeper stretch. A linear answer is held to one code, the difference between the
    // record's truncation and the engine's rounding.
    for (PresentationCase& made : cases)
    {
        if (made.classification != ExpectationClass::Tolerance || !made.autostretch)
        {
            continue;
        }
        const bool floatInput = made.input.find("float32") != std::string::npos;
        if (made.product != PresentationProduct::Native)
        {
            made = bounded(std::move(made), 2.0, kHistogramMedian);
        }
        else if (!floatInput)
        {
            made = bounded(std::move(made), 200.0, kHistogramMedian);
        }
        else
        {
            const bool amplified = made.saturation != 0.0 || made.contrast != 0;
            const bool ownChannels = !made.linked && made.input.find("rgb") != std::string::npos;
            double bound = 3.0e-4;
            if (amplified)
            {
                bound = 5.0e-3;
            }
            else if (ownChannels)
            {
                bound = 8.0e-3;
            }
            made = bounded(std::move(made), bound, kHistogramMedian);
        }
    }
    return cases;
}

} // namespace

const std::vector<PresentationCase>& presentationCases()
{
    static const std::vector<PresentationCase> cases = build();
    return cases;
}

const PresentationCase* presentationCase(std::string_view id)
{
    for (const PresentationCase& candidate : presentationCases())
    {
        if (candidate.id() == id)
        {
            return &candidate;
        }
    }
    return nullptr;
}

double presentationBrightnessOffset(int level) noexcept
{
    return static_cast<double>(level) / 255.0;
}

double presentationContrastFactor(int level) noexcept
{
    const double value = static_cast<double>(level);
    return 259.0 * (value + 255.0) / (255.0 * (259.0 - value));
}

} // namespace an::tests::fixtures
