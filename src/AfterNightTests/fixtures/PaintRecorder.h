// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A paint device that records the text a painting draws - each run with the pen, the font and the opacity it was
/// drawn in - and draws nothing. A render case reads a text's colour from it rather than from glyph pixels, which
/// differ with the host's fonts.

#include <QColor>
#include <QFont>
#include <QPaintDevice>
#include <QPaintEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QTextItem>

#include <memory>
#include <vector>

namespace an::ui::testing
{

/// One run of text a painting drew.
struct RecordedText
{
    QString text;
    QColor colour;
    QFont font;
    qreal opacity = 1.0;
};

/// A paint device of a size that keeps every run of text painted on it, and nothing else.
class PaintRecorder final : public QPaintDevice
{
public:
    explicit PaintRecorder(const QSize size) : m_size(size), m_engine(std::make_unique<Engine>(m_texts)) {}

    [[nodiscard]] QPaintEngine* paintEngine() const override
    {
        return m_engine.get();
    }

    [[nodiscard]] const std::vector<RecordedText>& texts() const
    {
        return m_texts;
    }

    /// The first run whose text holds `part`, or null.
    [[nodiscard]] const RecordedText* textHolding(const QString& part) const
    {
        for (const RecordedText& text : m_texts)
        {
            if (text.text.contains(part))
            {
                return &text;
            }
        }
        return nullptr;
    }

protected:
    [[nodiscard]] int metric(const PaintDeviceMetric metric) const override
    {
        switch (metric)
        {
        case PdmWidth:
            return m_size.width();
        case PdmHeight:
            return m_size.height();
        case PdmWidthMM:
            return m_size.width() * 254 / 960;
        case PdmHeightMM:
            return m_size.height() * 254 / 960;
        case PdmNumColors:
            return 0x7fffffff;
        case PdmDepth:
            return 32;
        case PdmDpiX:
        case PdmDpiY:
        case PdmPhysicalDpiX:
        case PdmPhysicalDpiY:
            return 96;
        case PdmDevicePixelRatio:
            return 1;
        case PdmDevicePixelRatioScaled:
            return static_cast<int>(QPaintDevice::devicePixelRatioFScale());
        default:
            return 0;
        }
    }

private:
    class Engine final : public QPaintEngine
    {
    public:
        explicit Engine(std::vector<RecordedText>& texts) : QPaintEngine(QPaintEngine::AllFeatures), m_texts(texts) {}

        bool begin(QPaintDevice*) override
        {
            return true;
        }
        bool end() override
        {
            return true;
        }
        void updateState(const QPaintEngineState& state) override
        {
            if (state.state() & QPaintEngine::DirtyPen)
            {
                m_pen = state.pen();
            }
            if (state.state() & QPaintEngine::DirtyOpacity)
            {
                m_opacity = state.opacity();
            }
        }
        void drawPixmap(const QRectF&, const QPixmap&, const QRectF&) override {}
        // Lines, rectangles and shapes are drawn as paths or polygons, and draw nothing here. One of the polygon
        // functions must be given: the engine's two defaults call each other, so a line would recurse until the stack
        // is gone.
        using QPaintEngine::drawPolygon;
        void drawPath(const QPainterPath&) override {}
        void drawPolygon(const QPointF*, int, PolygonDrawMode) override {}
        void drawTextItem(const QPointF&, const QTextItem& textItem) override
        {
            m_texts.push_back(RecordedText{textItem.text(), m_pen.color(), textItem.font(), m_opacity});
        }
        [[nodiscard]] Type type() const override
        {
            return QPaintEngine::User;
        }

    private:
        std::vector<RecordedText>& m_texts;
        QPen m_pen;
        qreal m_opacity = 1.0;
    };

    QSize m_size;
    std::vector<RecordedText> m_texts;
    std::unique_ptr<Engine> m_engine;
};

} // namespace an::ui::testing
