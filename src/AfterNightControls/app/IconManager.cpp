// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "IconManager.h"

#include <QtSvg/qsvgrenderer.h>
#include <QtCore/QRegularExpression>

namespace
{
[[nodiscard]] IconManager::SemanticIconTone toneForIconName(const QString& iconName)
{
    if (iconName == QStringLiteral("info") || iconName == QStringLiteral("activity_info"))
    {
        return IconManager::SemanticIconTone::Info;
    }

    if (iconName == QStringLiteral("warning") || iconName == QStringLiteral("activity_warning"))
    {
        return IconManager::SemanticIconTone::Warning;
    }

    if (iconName == QStringLiteral("priority_high") || iconName == QStringLiteral("exclamation"))
    {
        return IconManager::SemanticIconTone::Error;
    }

    if (iconName == QStringLiteral("help"))
    {
        return IconManager::SemanticIconTone::Question;
    }

    return IconManager::SemanticIconTone::None;
}
} // anonymous namespace

IconManager::IconManager()
{
    // Default values
    m_ColorNormal.setRgb(33, 33, 33);
    m_ColorDisabled.setRgb(150, 150, 150);
    m_ColorInfo = m_ColorNormal;
    m_ColorWarning = m_ColorNormal;
    m_ColorError = m_ColorNormal;
    m_ColorQuestion = m_ColorNormal;
    m_ColorSuccess = m_ColorNormal;
}

void IconManager::setColors(const QColor& normal, const QColor& disabled)
{
    m_ColorNormal = normal;
    m_ColorDisabled = disabled;
}

void IconManager::setSemanticColors(const QColor& info, const QColor& warning, const QColor& error,
                                    const QColor& question, const QColor& success)
{
    m_ColorInfo = info;
    m_ColorWarning = warning;
    m_ColorError = error;
    m_ColorQuestion = question;
    m_ColorSuccess = success;
}

void IconManager::loadIcons(const QString& path)
{
    QDir dir(path);
    auto list = dir.entryInfoList(QDir::Files);

    for (auto& entry : list)
    {
        auto tt = entry.suffix();
        if (entry.suffix().compare("svg", Qt::CaseInsensitive) == 0)
        {
            const auto tone = toneForIconName(entry.baseName());
            QColor iconColor = m_ColorNormal;
            if (tone != SemanticIconTone::None)
            {
                iconColor = getSemanticColor(tone);
            }
            m_LoadedIcons[entry.baseName()] = loadSvgFileAsIcon(entry.absoluteFilePath(), iconColor);
        }
        else
        {
            m_LoadedIcons[entry.baseName()] = QIcon(entry.absoluteFilePath());
        }
    }
}

size_t IconManager::getNumOfLoadedIcons() const
{
    return m_LoadedIcons.size();
}

void IconManager::clearLoadedIcons()
{
    m_LoadedIcons.clear();
}

bool IconManager::hasIcon(const QString& icon) const
{
    return m_LoadedIcons.find(icon) != m_LoadedIcons.end();
}

QIcon IconManager::getIcon(const QString& icon)
{
    auto it = m_LoadedIcons.find(icon);
    if (it != m_LoadedIcons.end())
        return it->second;

    return QIcon();
}

static void modifySvgColor(QString& svgData, const QColor& color)
{
    if (svgData.contains("AN_UNPROCESS_COLORS"))
    {
        return;
    }

    // Regular expression to match existing fill color property
    static QRegularExpression fillRegex("fill\\s*=\\s*\"(#[0-9a-fA-F]+|none)\"");
    static QRegularExpression strokeRegex("stroke\\s*=\\s*\"#[0-9a-fA-F]+\"");

    QRegularExpressionMatch match;

    // Check if fill color property exists
    if (!svgData.contains(fillRegex, &match))
    {
        // If fill color property doesn't exist, add it
        svgData.replace("<svg ", "<svg fill=\"" + color.name(QColor::HexRgb) + "\" ");
    }
    else
    {
        auto cap = match.captured(1);
        if (cap == "none")
        {
            if (!svgData.contains(strokeRegex))
            {
                // If fill color property doesn't exist, add it
                svgData.replace("<svg ", "<svg stroke=\"" + color.name(QColor::HexRgb) + "\" ");
            }
            else
            {
                QString strokeProp = QStringLiteral("stroke=\"%1\"").arg(color.name(QColor::HexRgb));

                // If fill color property exists, perform the replacement
                svgData.replace(strokeRegex, strokeProp);
            }
        }
        else
        {
            // Replacement string with the new color
            QString fillProp = QStringLiteral("fill=\"%1\"").arg(color.name(QColor::HexRgb));
            // If fill color property exists, perform the replacement
            svgData.replace(fillRegex, fillProp);
        }
    }
}

static void renderModifiedSvg(QPainter* painter, const QString& svgData, const QRectF& bounds)
{
    QSvgRenderer renderer(svgData.toUtf8());
    renderer.render(painter, bounds);
}

static QPixmap createSvgPixmap(const QString& svgData, const QSize& size)
{
    QImage image(size, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    renderModifiedSvg(&painter, svgData, QRectF(0, 0, size.width(), size.height()));
    return QPixmap::fromImage(image);
}

QIcon IconManager::loadSvgFileAsIcon(const QString& fileName)
{
    return loadSvgFileAsIcon(fileName, m_ColorNormal);
}

QIcon IconManager::loadSvgFileAsIcon(const QString& fileName, const QColor& normalColor)
{
    QFile svgFile(fileName);

    if (!svgFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        throw std::runtime_error(fmt::format("Could NOT read SVG file {}", fileName.toStdString()).c_str());
    }

    QIcon icon;
    const QSize iconSize(64, 64);

    QString svgData = svgFile.readAll();
    svgFile.close();

    modifySvgColor(svgData, normalColor);
    auto pixmap = createSvgPixmap(svgData, iconSize);
    icon.addPixmap(pixmap, QIcon::Normal, QIcon::Off);

    if (!svgData.contains("AN_UNPROCESS_COLORS"))
    {
        modifySvgColor(svgData, m_ColorDisabled);
        auto pixmap2 = createSvgPixmap(svgData, iconSize);
        icon.addPixmap(pixmap2, QIcon::Disabled, QIcon::Off);
    }

    return icon;
}

QPixmap IconManager::loadSvgFileAsPixmap(const QString& fileName, const QSize size, const QColor color)
{
    QFile svgFile(fileName);

    if (!svgFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        throw std::runtime_error(fmt::format("Could NOT read SVG file {}", fileName.toStdString()).c_str());
    }

    QString svgData = svgFile.readAll();
    svgFile.close();

    modifySvgColor(svgData, color);
    auto pixmap = createSvgPixmap(svgData, size);

    return pixmap;
}

const QColor IconManager::getColorNormal() const
{
    return m_ColorNormal;
    ;
}

const QColor IconManager::getColorDisabled() const
{
    return m_ColorDisabled;
    ;
}

const QColor IconManager::getSemanticColor(SemanticIconTone tone) const
{
    switch (tone)
    {
    case SemanticIconTone::Info:
        return m_ColorInfo;
    case SemanticIconTone::Warning:
        return m_ColorWarning;
    case SemanticIconTone::Error:
        return m_ColorError;
    case SemanticIconTone::Question:
        return m_ColorQuestion;
    case SemanticIconTone::Success:
        return m_ColorSuccess;
    case SemanticIconTone::None:
        break;
    }

    return m_ColorNormal;
}

IconManager::~IconManager() {}
