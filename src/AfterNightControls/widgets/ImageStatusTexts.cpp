// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "widgets/ImageStatusTexts.h"

#include "controls/ImageViewControl.h"
#include "widgets/ImageInfoStatusBarWidget.h"

#include <cstdint>
#include <optional>
#include <string>

namespace an::ui
{

namespace
{

/// The status bar's name for a representation.
[[nodiscard]] std::string statusBarSampleTypeLabel(const an::SampleType type)
{
    switch (type)
    {
    case an::SampleType::Int8:
        return "8 bit signed integer";
    case an::SampleType::UInt8:
        return "8 bit unsigned integer";
    case an::SampleType::Int16:
        return "16 bit signed integer";
    case an::SampleType::UInt16:
        return "16 bit unsigned integer";
    case an::SampleType::Int32:
        return "32 bit signed integer";
    case an::SampleType::UInt32:
        return "32 bit unsigned integer";
    case an::SampleType::Float32:
        return "32 bit floating point";
    case an::SampleType::Float64:
        return "64 bit floating point";
    }
    return std::string();
}

/// The status bar's name for a sequence's container.
[[nodiscard]] std::string statusBarSequenceLabel(const an::SequenceFormat format)
{
    switch (format)
    {
    case an::SequenceFormat::Ser:
        return "SER SEQUENCE";
    case an::SequenceFormat::ImageList:
        return "IMAGE LIST SEQUENCE";
    case an::SequenceFormat::Fits:
        return "FITS SEQUENCE";
    case an::SequenceFormat::Avi:
    case an::SequenceFormat::Mp4:
    case an::SequenceFormat::Video:
        return "VIDEO SEQUENCE";
    case an::SequenceFormat::Dummy:
        break;
    }
    return std::string();
}

} // namespace

QString imageFactsStatusText(ImageViewControl* view)
{
    if (view == nullptr)
    {
        return {};
    }
    const std::optional<an::ImageDescriptor> described = view->imageDescriptor();
    if (!described.has_value())
    {
        return {};
    }
    const std::uint32_t width = described->extent().width();
    const std::uint32_t height = described->extent().height();
    const std::uint32_t channels = described->extent().channelCount();
    const std::string mode = channels < 3u ? "Grayscale" : "RGB";
    const std::string format = statusBarSampleTypeLabel(described->sampleType());
    const double frameBytes =
        static_cast<double>(width) * height * channels * an::sampleTypeSizeBytes(described->sampleType());

    std::string description;
    if (view->isSequence())
    {
        const std::optional<an::ui::DocumentSequence> sequence = view->document().sequence();
        const an::SequenceDescriptor descriptor =
            sequence.has_value() ? sequence->descriptor : an::SequenceDescriptor{};
        const std::uint64_t frames = view->getSequenceNumOfFrames();
        double fps = 0.0;
        if (descriptor.nominalFrameRate.has_value() && descriptor.nominalFrameRate->denominator != 0u)
        {
            fps = static_cast<double>(descriptor.nominalFrameRate->numerator) /
                  static_cast<double>(descriptor.nominalFrameRate->denominator);
        }

        description = fmt::format("  [{}] - [{}x{} ({})] - [{}/{}] - [{:.2f} fps] - [{}]",
                                  statusBarSequenceLabel(descriptor.format), width, height, mode,
                                  view->getSequenceCurrentFrame(), frames, fps, format);

        double sequenceSize = (frameBytes * static_cast<double>(frames) / 1024.0) / 1024.0;
        if (sequenceSize > 1024.0)
        {
            sequenceSize = sequenceSize / 1024.0;
            description += fmt::format(" - [{:.2f} GB] ", sequenceSize);
        }
        else
        {
            description += fmt::format(" - [{:.2f} MB] ", sequenceSize);
        }
    }
    else
    {
        double imageSize = (frameBytes / 1024.0) / 1024.0;
        if (imageSize > 1024.0)
        {
            imageSize = imageSize / 1024.0;
            description = fmt::format("  [{}x{} ({})] - [{}] - [{:.2f} GB]  ", width, height, mode, format, imageSize);
        }
        else
        {
            description = fmt::format("  [{}x{} ({})] - [{}] - [{:.2f} MB]  ", width, height, mode, format, imageSize);
        }
    }
    return QString::fromUtf8(description);
}

QString zoomStatusText(ImageViewControl* view)
{
    if (view == nullptr || !view->hasImage())
    {
        return {};
    }
    return QString::fromUtf8(fmt::format("  {:.2f} %  ", view->getCurrentZoom()));
}

QString rightAscensionStatusText(const double raDegrees)
{
    return QStringLiteral("RA %1").arg(formatRightAscensionSexagesimal(raDegrees));
}

QString rightAscensionStatusToolTip(const double raDegrees)
{
    return QStringLiteral("RA %1°").arg(raDegrees, 0, 'f', 6);
}

QString declinationStatusText(const double decDegrees)
{
    return QStringLiteral("Dec %1").arg(formatDeclinationSexagesimal(decDegrees));
}

QString declinationStatusToolTip(const double decDegrees)
{
    return QStringLiteral("Dec %1°").arg(decDegrees, 0, 'f', 6);
}

} // namespace an::ui
