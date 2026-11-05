// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageInfoStatusBarWidget.h"
#include "controls/ImageViewControl.h"

ImageInfoStatusBarWidget::ImageInfoStatusBarWidget()
{
    m_ImageResolution = std::make_unique<QLabel>();
    m_ImageZoom = std::make_unique<QLabel>();
    m_ra_label = std::make_unique<QLabel>();
    m_dec_label = std::make_unique<QLabel>();

    m_ra_label->setObjectName(QStringLiteral("imageStatusRaLabel"));
    m_dec_label->setObjectName(QStringLiteral("imageStatusDecLabel"));

    clearCelestialCoords();
}

void ImageInfoStatusBarWidget::initialize(QStatusBar* bar)
{
    bar->addWidget(m_ImageResolution.get());
    bar->addWidget(m_ImageZoom.get());
    bar->addPermanentWidget(m_ra_label.get());
    bar->addPermanentWidget(m_dec_label.get());
}

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

void ImageInfoStatusBarWidget::updateImageInfo(ImageViewControl* control)
{
    if (!control)
    {
        clearImageInfo();
    }
    else
    {
        const std::optional<an::ImageDescriptor> described = control->imageDescriptor();
        if (!described.has_value())
        {
            clearImageInfo();
        }
        else
        {
            const std::uint32_t width = described->extent().width();
            const std::uint32_t height = described->extent().height();
            const std::uint32_t channels = described->extent().channelCount();
            const std::string mode = channels < 3u ? "Grayscale" : "RGB";
            const std::string format = statusBarSampleTypeLabel(described->sampleType());
            const double frameBytes =
                static_cast<double>(width) * height * channels * an::sampleTypeSizeBytes(described->sampleType());

            std::string imgDesc;

            if (control->isSequence())
            {
                const std::optional<an::ui::DocumentSequence> sequence = control->document().sequence();
                const an::SequenceDescriptor descriptor =
                    sequence.has_value() ? sequence->descriptor : an::SequenceDescriptor{};
                const std::uint64_t frames = control->getSequenceNumOfFrames();
                double fps = 0.0;
                if (descriptor.nominalFrameRate.has_value() && descriptor.nominalFrameRate->denominator != 0u)
                {
                    fps = static_cast<double>(descriptor.nominalFrameRate->numerator) /
                          static_cast<double>(descriptor.nominalFrameRate->denominator);
                }

                imgDesc = fmt::format("  [{}] - [{}x{} ({})] - [{}/{}] - [{:.2f} fps] - [{}]",
                                      statusBarSequenceLabel(descriptor.format), width, height, mode,
                                      control->getSequenceCurrentFrame(), frames, fps, format);

                double seqSize = (frameBytes * static_cast<double>(frames) / 1024.0) / 1024.0;
                if (seqSize > 1024.0)
                {
                    seqSize = seqSize / 1024.0;
                    imgDesc += fmt::format(" - [{:.2f} GB] ", seqSize);
                }
                else
                {
                    imgDesc += fmt::format(" - [{:.2f} MB] ", seqSize);
                }
            }
            else
            {
                double imgSize = (frameBytes / 1024.0) / 1024.0;
                if (imgSize > 1024.0)
                {
                    imgSize = imgSize / 1024.0;
                    imgDesc =
                        fmt::format("  [{}x{} ({})] - [{}] - [{:.2f} GB]  ", width, height, mode, format, imgSize);
                }
                else
                {
                    imgDesc =
                        fmt::format("  [{}x{} ({})] - [{}] - [{:.2f} MB]  ", width, height, mode, format, imgSize);
                }
            }

            m_ImageResolution->setText(QString::fromUtf8(imgDesc));

            updateImageZoomInfo(control);
        }
    }
}

void ImageInfoStatusBarWidget::updateImageZoomInfo(ImageViewControl* control)
{
    if (!control)
    {
        clearImageInfo();
    }
    else
    {
        if (!control->hasImage())
        {
            clearImageInfo();
        }
        else
        {
            std::string imgZoom = fmt::format("  {:.2f} %  ", control->getCurrentZoom());

            m_ImageZoom->setText(QString::fromUtf8(imgZoom));
        }
    }
}

void ImageInfoStatusBarWidget::updateCelestialCoords(const double ra_deg, const double dec_deg)
{
    const QString ra_text = QStringLiteral("RA %1").arg(an::ui::formatRightAscensionSexagesimal(ra_deg));
    const QString dec_text = QStringLiteral("Dec %1").arg(an::ui::formatDeclinationSexagesimal(dec_deg));
    m_ra_label->setText(ra_text);
    m_dec_label->setText(dec_text);
    m_ra_label->setToolTip(QStringLiteral("RA %1°").arg(ra_deg, 0, 'f', 6));
    m_dec_label->setToolTip(QStringLiteral("Dec %1°").arg(dec_deg, 0, 'f', 6));
    m_ra_label->show();
    m_dec_label->show();
}

void ImageInfoStatusBarWidget::clearCelestialCoords()
{
    m_ra_label->clear();
    m_dec_label->clear();
    m_ra_label->setToolTip(QString());
    m_dec_label->setToolTip(QString());
    m_ra_label->hide();
    m_dec_label->hide();
}

void ImageInfoStatusBarWidget::clearImageInfo()
{
    m_ImageResolution->clear();
    m_ImageZoom->clear();
    clearCelestialCoords();
}

ImageInfoStatusBarWidget::~ImageInfoStatusBarWidget() {}
