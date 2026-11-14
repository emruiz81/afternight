// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "theme/Elevation.h"

#include "theme/AnDesignTokens.h"
#include "theme/PainterGuard.h"

#include <QPainter>
#include <QPainterPath>
#include <QRectF>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <tuple>
#include <vector>

namespace
{

/// Half the spread: how far the fade reaches each side of the outline it fades.
constexpr int kHalfSpread = an::ui::kElevationShadowSpread / 2;

/// The box blur passed three times over the shadow's mask: three passes make a curve close to a gaussian, and a radius
/// of a third of the half spread keeps the whole fade within it.
constexpr int kBlurPasses = 3;

/// One kept nine-patch: the radius, the device pixel ratio in thousandths and the colour it was rendered in.
using ShadowKey = std::tuple<int, int, QRgb>;

/// The kept nine-patches, and how many have been rendered.
struct ShadowImages
{
    std::mutex mutex;
    std::map<ShadowKey, QImage> images;
    std::atomic<std::uint64_t> renders{0};
};

ShadowImages& shadowImages()
{
    static ShadowImages images;
    return images;
}

/// One pass of a box blur of a radius along the rows of a mask of a width and a height, in place.
void blurRows(std::vector<float>& mask, const int width, const int height, const int radius)
{
    std::vector<float> row(static_cast<std::size_t>(width));
    const float scale = 1.0f / static_cast<float>(2 * radius + 1);
    for (int y = 0; y < height; ++y)
    {
        float* const line = mask.data() + static_cast<std::ptrdiff_t>(y) * width;
        float sum = 0.0f;
        for (int x = -radius; x <= radius; ++x)
        {
            sum += (x >= 0 && x < width) ? line[x] : 0.0f;
        }
        for (int x = 0; x < width; ++x)
        {
            row[static_cast<std::size_t>(x)] = sum * scale;
            const int leaving = x - radius;
            const int entering = x + radius + 1;
            sum -= (leaving >= 0) ? line[leaving] : 0.0f;
            sum += (entering < width) ? line[entering] : 0.0f;
        }
        std::copy(row.begin(), row.end(), line);
    }
}

/// The same pass along the columns.
void blurColumns(std::vector<float>& mask, const int width, const int height, const int radius)
{
    std::vector<float> column(static_cast<std::size_t>(height));
    const float scale = 1.0f / static_cast<float>(2 * radius + 1);
    for (int x = 0; x < width; ++x)
    {
        const auto at = [&](const int y) -> float& { return mask[static_cast<std::size_t>(y) * width + x]; };
        float sum = 0.0f;
        for (int y = -radius; y <= radius; ++y)
        {
            sum += (y >= 0 && y < height) ? at(y) : 0.0f;
        }
        for (int y = 0; y < height; ++y)
        {
            column[static_cast<std::size_t>(y)] = sum * scale;
            const int leaving = y - radius;
            const int entering = y + radius + 1;
            sum -= (leaving >= 0) ? at(leaving) : 0.0f;
            sum += (entering < height) ? at(entering) : 0.0f;
        }
        for (int y = 0; y < height; ++y)
        {
            at(y) = column[static_cast<std::size_t>(y)];
        }
    }
}

/// Renders the nine-patch: a rounded square of the radius, the spread wide past its middle on each side, filled in the
/// colour, then faded by the blur; the square's fade lies wholly inside the image.
QImage renderShadow(const int radius, const qreal ratio, const QColor& colour)
{
    const int logical = 2 * (radius + an::ui::kElevationShadowSpread) + 1;
    const int side = static_cast<int>(std::lround(logical * ratio));
    QImage mask(side, side, QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    {
        QPainter painter(&mask);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        const qreal inset = kHalfSpread * ratio;
        const QRectF square(inset, inset, side - 2 * inset, side - 2 * inset);
        painter.drawRoundedRect(square, radius * ratio, radius * ratio);
    }

    std::vector<float> alpha(static_cast<std::size_t>(side) * side);
    for (int y = 0; y < side; ++y)
    {
        const auto* line = reinterpret_cast<const QRgb*>(mask.constScanLine(y));
        for (int x = 0; x < side; ++x)
        {
            alpha[static_cast<std::size_t>(y) * side + x] = static_cast<float>(qAlpha(line[x])) / 255.0f;
        }
    }
    const int blur = (std::max)(1, static_cast<int>(std::floor(kHalfSpread * ratio / kBlurPasses)));
    for (int pass = 0; pass < kBlurPasses; ++pass)
    {
        blurRows(alpha, side, side, blur);
        blurColumns(alpha, side, side, blur);
    }

    QImage shadow(side, side, QImage::Format_ARGB32_Premultiplied);
    const float colourAlpha = static_cast<float>(colour.alphaF());
    for (int y = 0; y < side; ++y)
    {
        auto* line = reinterpret_cast<QRgb*>(shadow.scanLine(y));
        for (int x = 0; x < side; ++x)
        {
            const float a = std::clamp(alpha[static_cast<std::size_t>(y) * side + x], 0.0f, 1.0f) * colourAlpha;
            const auto premultiplied = [a](const int component) {
                return static_cast<int>(std::lround(static_cast<float>(component) * a));
            };
            line[x] = qRgba(premultiplied(colour.red()), premultiplied(colour.green()), premultiplied(colour.blue()),
                            static_cast<int>(std::lround(a * 255.0f)));
        }
    }
    shadow.setDevicePixelRatio(ratio);
    return shadow;
}

/// Draws a nine-patch around a target: its corners at their size - smaller when the target is - its edges and its
/// middle stretched.
void drawNinePatch(QPainter& painter, const QRectF& target, const QImage& image, const qreal corner)
{
    const qreal ratio = image.devicePixelRatio();
    const qreal sourceCorner = std::round(corner * ratio);
    const qreal sourceSide = image.width();
    const qreal horizontal = (std::min)(corner, target.width() / 2.0);
    const qreal vertical = (std::min)(corner, target.height() / 2.0);
    const qreal sourceColumns[4] = {0.0, sourceCorner, sourceSide - sourceCorner, sourceSide};
    const qreal sourceRows[4] = {0.0, sourceCorner, sourceSide - sourceCorner, sourceSide};
    const qreal columns[4] = {target.left(), target.left() + horizontal, target.right() - horizontal, target.right()};
    const qreal rows[4] = {target.top(), target.top() + vertical, target.bottom() - vertical, target.bottom()};
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            const QRectF to(QPointF(columns[column], rows[row]), QPointF(columns[column + 1], rows[row + 1]));
            const QRectF from(QPointF(sourceColumns[column], sourceRows[row]),
                              QPointF(sourceColumns[column + 1], sourceRows[row + 1]));
            if (to.width() > 0.0 && to.height() > 0.0)
            {
                painter.drawImage(to, image, from);
            }
        }
    }
}

} // anonymous namespace

namespace an::ui
{

QMargins elevationWindowMargins()
{
    return QMargins(kHalfSpread, kHalfSpread - kElevationShadowOffset, kHalfSpread,
                    kHalfSpread + kElevationShadowOffset);
}

QImage elevationShadow(const int radius, const qreal devicePixelRatio)
{
    const QColor colour = tokens().colors.shadowOverlay;
    const int ratioKey = static_cast<int>(std::lround(devicePixelRatio * 1000.0));
    const ShadowKey key{(std::max)(0, radius), ratioKey, colour.rgba()};
    ShadowImages& kept = shadowImages();
    const std::lock_guard lock(kept.mutex);
    const auto found = kept.images.find(key);
    if (found != kept.images.end())
    {
        return found->second;
    }
    QImage image = renderShadow((std::max)(0, radius), devicePixelRatio, colour);
    kept.renders.fetch_add(1u, std::memory_order_relaxed);
    kept.images.emplace(key, image);
    return image;
}

void paintElevation(QPainter& painter, const QRectF& surface, const int radius)
{
    if (surface.isEmpty())
    {
        return;
    }
    const PainterGuard guard(&painter);
    const qreal ratio = painter.device() != nullptr ? painter.device()->devicePixelRatio() : 1.0;
    const QImage shadow = elevationShadow(radius, ratio);
    const QRectF target =
        surface.translated(0.0, kElevationShadowOffset).adjusted(-kHalfSpread, -kHalfSpread, kHalfSpread, kHalfSpread);
    drawNinePatch(painter, target, shadow, static_cast<qreal>(radius + kElevationShadowSpread));

    // The edge: the part's outline one pixel down, under the part.
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(tokens().colors.edge);
    painter.drawRoundedRect(surface.translated(0.0, 1.0), radius, radius);
}

void dropElevationImages()
{
    ShadowImages& kept = shadowImages();
    const std::lock_guard lock(kept.mutex);
    kept.images.clear();
}

namespace testing
{

std::size_t keptElevationImages()
{
    ShadowImages& kept = shadowImages();
    const std::lock_guard lock(kept.mutex);
    return kept.images.size();
}

std::uint64_t elevationRenders()
{
    return shadowImages().renders.load(std::memory_order_relaxed);
}

} // namespace testing

} // namespace an::ui
