// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "ImageSelectorTooltipBuilder.h"
#include "ImageViewControl.h"
#include "common.h"

#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <QFileInfo>
#include <QString>

#include <cstdint>
#include <optional>

namespace
{

QString formatMemorySize(std::uint64_t bytes)
{
    constexpr double KB = 1024.0;
    constexpr double MB = KB * 1024.0;
    constexpr double GB = MB * 1024.0;

    if (static_cast<double>(bytes) >= GB)
        return QString::number(static_cast<double>(bytes) / GB, 'f', 2) + QStringLiteral(" GB");
    if (static_cast<double>(bytes) >= MB)
        return QString::number(static_cast<double>(bytes) / MB, 'f', 1) + QStringLiteral(" MB");
    if (static_cast<double>(bytes) >= KB)
        return QString::number(static_cast<double>(bytes) / KB, 'f', 1) + QStringLiteral(" KB");

    return QString::number(bytes) + QStringLiteral(" bytes");
}

QString imageDisplayNameWithoutExtension(const ImageViewControl* vc)
{
    if (!vc)
        return {};

    const QString uniqueName = QString::fromStdString(vc->getUniqueName()).trimmed();
    if (uniqueName.isEmpty())
        return {};

    const QFileInfo info(uniqueName);
    const QString baseName = info.completeBaseName().trimmed();
    return baseName.isEmpty() ? uniqueName : baseName;
}

bool isTemporaryDmpPath(const QString& path)
{
    const QFileInfo info(path);
    return info.suffix().compare(QStringLiteral("dmp"), Qt::CaseInsensitive) == 0;
}

/// The file a view's document was read from or last written to, or "In-memory file" for an image that has never been
/// one, or whose only file is a temporary dump.
QString resolveSourcePathOrInMemory(const ImageViewControl* vc)
{
    const QString path = vc->document().names().sourcePath.trimmed();
    if (path.isEmpty() || isTemporaryDmpPath(path))
    {
        return QStringLiteral("In-memory file");
    }

    const QFileInfo fileInfo(path);
    return fileInfo.absoluteFilePath();
}

} // anonymous namespace

namespace an::ui
{

QString buildImageSelectorTooltip(const ImageViewControl* vc)
{
    if (!vc || !vc->hasImage())
        return {};

    const std::optional<an::ImageDescriptor> described = vc->imageDescriptor();
    if (!described.has_value())
        return {};

    const auto displayName = imageDisplayNameWithoutExtension(vc);
    const auto sourcePathOrInMemory = resolveSourcePathOrInMemory(vc);
    const std::uint32_t width = described->extent().width();
    const std::uint32_t height = described->extent().height();
    const std::uint32_t channels = described->extent().channelCount();
    const auto pixType = QString::fromStdString(sampleTypeReportLabel(described->sampleType()));
    const auto pixFmt = QString::fromStdString(colorReportLabel(described->channelSchema()));
    const auto memSize = formatMemorySize(static_cast<std::uint64_t>(width) * height * channels *
                                          an::sampleTypeSizeBytes(described->sampleType()));

    // Build a compact rich-text tooltip
    QString tip;
    tip.reserve(512);

    tip += QStringLiteral("<div style='max-width:520px;'><table>");

    if (!displayName.isEmpty())
    {
        tip += QStringLiteral("<tr><td><b>Image:&nbsp;</b></td><td><b>") + displayName.toHtmlEscaped() +
               QStringLiteral("</b></td></tr>");
    }

    tip += QStringLiteral("<tr><td><b>Source:&nbsp;</b></td><td style='white-space:normal; word-break:break-word;'>") +
           sourcePathOrInMemory.toHtmlEscaped() + QStringLiteral("</td></tr>");

    tip += QStringLiteral("<tr><td><b>Size:&nbsp;</b></td><td>") + QString::number(width) + QStringLiteral(" × ") +
           QString::number(height) + QStringLiteral(" px</td></tr>");

    tip += QStringLiteral("<tr><td><b>Channels:&nbsp;</b></td><td>") + QString::number(channels) +
           QStringLiteral(" (") + pixFmt.toHtmlEscaped() + QStringLiteral(")") + QStringLiteral("</td></tr>");

    tip += QStringLiteral("<tr><td><b>Pixel type:&nbsp;</b></td><td>") + pixType.toHtmlEscaped() +
           QStringLiteral("</td></tr>");

    tip += QStringLiteral("<tr><td><b>Memory:&nbsp;</b></td><td>") + memSize + QStringLiteral("</td></tr>");

    tip += QStringLiteral("</table></div>");

    return tip;
}

} // namespace an::ui
