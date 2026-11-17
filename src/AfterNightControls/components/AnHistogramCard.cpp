// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "components/AnHistogramCard.h"

#include "components/AnIconButton.h"
#include "components/ReadoutFigures.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"
#include "widgets/HistogramWidget.h"

#include <QComboBox>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <verdigris/wobjectimpl.h>

#include <array>
#include <utility>

W_OBJECT_IMPL(an::ui::AnHistogramCard)

namespace an::ui
{
namespace
{

/// The tools, as the histogram window names them, with their icons and whether they are toggles and start on.
struct ToolSpec
{
    const char* icon;
    const char* text;
    bool checkable;
    bool checked;
};

constexpr std::array<ToolSpec, 7> kTools{{
    {"zoom_in", "Zoom in", false, false},
    {"zoom_out", "Zoom out", false, false},
    {"zoom_fit", "Zoom reset", false, false},
    {"grid", "Show grid", true, true},
    {"info", "Show clipping info", true, true},
    {"scale", "Absolute scales", true, false},
    {"area_chart", "Draw histogram areas", true, true},
}};

void keepLayout(QLayout* layout)
{
    layout->setProperty("anKeepLayoutMargins", true);
    layout->setProperty("anKeepLayoutSpacing", true);
}

/// A thin upright line between two groups of tools.
class ToolSeparator final : public QWidget
{
public:
    explicit ToolSeparator(QWidget* parent) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        return QSize(parts::kReadoutToolGap * 2 + 1, tokens().metrics.controlXSmall);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        const int x = width() / 2;
        const int inset = height() / 4;
        painter.setPen(tokens().colors.borderSubtle);
        painter.drawLine(x, inset, x, height() - inset);
    }
};

} // namespace

AnHistogramCard::AnHistogramCard(QWidget* parent) : AnReadoutCard(tr("Histogram"), parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    // Folded, the card is its header alone and grows no more.
    QObject::connect(this, &AnReadoutCard::foldedChanged, this, [this](const bool folded) {
        setSizePolicy(QSizePolicy::Preferred, folded ? QSizePolicy::Maximum : QSizePolicy::Expanding);
    });
    m_window = addHeaderTool(QStringLiteral("open_in_new"), tr("Open the Histogram window (Ctrl+H)"));

    m_well = new AnReadoutWell(body());
    m_scroll = new QScrollArea(m_well);
    m_scroll->setObjectName(QStringLiteral("readoutHistogramScrollArea"));
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setAutoFillBackground(false);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_well->setPlot(m_scroll);

    // The scroll area owns the widget, as the histogram window's does; the card reaches it through a guard.
    m_plot = new HistogramWidget(nullptr);
    m_plot->setObjectName(QStringLiteral("readoutHistogram"));
    m_plot->setClippingInfoEnabled(true);
    m_plot->setMouseTracking(true);
    m_scroll->setWidget(m_plot);
    m_plot->setParentScrollArea(m_scroll);
    m_plot->setInteractiveZoomEnabled(true);
    m_plot->setAreaModeEnabled(true);
    bodyLayout()->addWidget(m_well, 1);

    auto* tools = new QHBoxLayout();
    keepLayout(tools);
    tools->setContentsMargins(0, 0, 0, 0);
    tools->setSpacing(parts::kReadoutToolGap);
    for (std::size_t index = 0u; index < kTools.size(); ++index)
    {
        const ToolSpec& spec = kTools[index];
        auto* tool = new AnIconButton(QString::fromLatin1(spec.icon), tr(spec.text), body());
        tool->setCheckable(spec.checkable);
        tool->setChecked(spec.checked);
        m_tools[index] = tool;
        if (index == static_cast<std::size_t>(HistogramCardTool::Grid))
        {
            tools->addWidget(new ToolSeparator(body()));
        }
        tools->addWidget(tool);
    }
    tools->addWidget(new ToolSeparator(body()));
    m_resolution = new QComboBox(body());
    m_resolution->setObjectName(QStringLiteral("readoutHistogramResolution"));
    m_resolution->setToolTip(tr("Plot resolution"));
    m_resolution->setAccessibleName(tr("Plot resolution"));
    for (const HistogramResolution resolution : kHistogramResolutions)
    {
        m_resolution->addItem(histogramResolutionName(resolution), static_cast<int>(resolution));
    }
    m_resolution->setCurrentIndex(m_resolution->findData(static_cast<int>(HistogramResolution::Bits8)));
    tools->addWidget(m_resolution, 1);
    bodyLayout()->addLayout(tools);

    const auto toolOf = [this](const HistogramCardTool which) { return m_tools[static_cast<std::size_t>(which)]; };
    QObject::connect(toolOf(HistogramCardTool::ZoomIn), &QAbstractButton::clicked, this, [this]() {
        if (m_plot)
        {
            m_plot->zoomIn();
        }
    });
    QObject::connect(toolOf(HistogramCardTool::ZoomOut), &QAbstractButton::clicked, this, [this]() {
        if (m_plot)
        {
            m_plot->zoomOut();
        }
    });
    QObject::connect(toolOf(HistogramCardTool::ZoomReset), &QAbstractButton::clicked, this, [this]() {
        if (m_plot)
        {
            m_plot->resetZoom();
        }
    });
    QObject::connect(toolOf(HistogramCardTool::Grid), &QAbstractButton::toggled, this, [this](const bool on) {
        if (m_plot)
        {
            m_plot->setGridEnabled(on);
        }
    });
    QObject::connect(toolOf(HistogramCardTool::ClippingInfo), &QAbstractButton::toggled, this, [this](const bool on) {
        if (m_plot)
        {
            m_plot->setClippingInfoEnabled(on);
        }
    });
    QObject::connect(toolOf(HistogramCardTool::AbsoluteScales), &QAbstractButton::toggled, this, [this](const bool on) {
        if (m_plot)
        {
            m_plot->setLocalChannelNormalization(on);
        }
    });
    QObject::connect(toolOf(HistogramCardTool::Areas), &QAbstractButton::toggled, this, [this](const bool on) {
        if (m_plot)
        {
            m_plot->setAreaModeEnabled(on);
        }
    });
    QObject::connect(m_resolution, &QComboBox::currentIndexChanged, this, [this](int) {
        const HistogramResolution chosen = resolution();
        if (m_plot && m_plot->getHistogramResolution() != chosen)
        {
            m_plot->setHistogramResolution(chosen);
            resolutionChanged();
        }
    });
    QObject::connect(m_window, &QAbstractButton::clicked, this, [this]() { histogramWindowRequested(); });
}

AnHistogramCard::~AnHistogramCard() = default;

void AnHistogramCard::setCounts(std::shared_ptr<const HistogramDisplayData> counts)
{
    if (m_plot)
    {
        m_plot->setHistogram(std::move(counts));
    }
}

void AnHistogramCard::setChannelCount(const std::size_t channels)
{
    setSummary(channels == 0u ? QString() : (channels >= 3u ? tr("RGB") : tr("Grayscale")));
}

void AnHistogramCard::setShownChannels(const bool red, const bool green, const bool blue)
{
    if (m_plot)
    {
        m_plot->setDisplayChannels(red, green, blue);
    }
}

HistogramResolution AnHistogramCard::resolution() const
{
    const QVariant data = m_resolution->currentData();
    return data.isValid() ? static_cast<HistogramResolution>(data.toInt()) : HistogramResolution::Bits8;
}

HistogramWidget* AnHistogramCard::histogramWidget() const noexcept
{
    return m_plot.data();
}

AnIconButton* AnHistogramCard::tool(const HistogramCardTool tool) const
{
    return m_tools[static_cast<std::size_t>(tool)];
}

QComboBox* AnHistogramCard::resolutionPicker() const noexcept
{
    return m_resolution;
}

AnIconButton* AnHistogramCard::windowButton() const noexcept
{
    return m_window;
}

AnReadoutWell* AnHistogramCard::well() const noexcept
{
    return m_well;
}

void AnHistogramCard::staleChanged()
{
    // The plot is a widget of its own: it is drawn at 55 % through an opacity effect while its counts are made again.
    if (isStale())
    {
        if (m_staleEffect == nullptr)
        {
            m_staleEffect = new QGraphicsOpacityEffect(m_scroll);
            m_staleEffect->setOpacity(parts::kReadoutStaleOpacity);
            m_scroll->setGraphicsEffect(m_staleEffect);
        }
    }
    else if (m_staleEffect != nullptr)
    {
        m_scroll->setGraphicsEffect(nullptr);
        m_staleEffect = nullptr;
    }
}

} // namespace an::ui
