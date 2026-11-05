// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ImageLensWidget.h"
#include "controls/ImageViewCompanions.h"
#include "theme/AnDesignTokens.h"
#include "widgets/ViewPresentation.h"

#if defined(Q_OS_LINUX)
#include <QGuiApplication>
#endif

namespace
{
bool isWaylandSession()
{
#if defined(Q_OS_LINUX)
    const QString platformName = QGuiApplication::platformName();
    return platformName.startsWith(QStringLiteral("wayland"), Qt::CaseInsensitive);
#else
    return false;
#endif
}

const an::ui::ColorTokens& lensColors()
{
    // Keep lens colors consistent across themes; light mode now mirrors dark mode styling.
    return an::ui::darkTokens().colors;
}
} // namespace

ImageLensWidget::ImageLensWidget(QWidget* parent) : QWidget(parent)
{
    this->setFixedSize(350, 250);
    if (isWaylandSession())
    {
        // Wayland compositor owns top-level window positioning. Use a child overlay so move() stays responsive.
        this->setWindowFlags(Qt::FramelessWindowHint);
    }
    else
    {
        this->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    }
    this->setAttribute(Qt::WA_TranslucentBackground);
    this->setAttribute(Qt::WA_ShowWithoutActivating);

    m_Histogram = an::ui::createLensHistogram(this);
    if (m_Histogram && m_Histogram->widget())
    {
        m_Histogram->widget()->setFixedSize(75, 54);
        m_Histogram->widget()->move(10, 190);
    }
}

ImageLensWidget::~ImageLensWidget() = default;

void ImageLensWidget::setHistogram(const an::ui::LensHistogram& counts)
{
    if (m_Histogram)
    {
        m_Histogram->setCounts(counts);
    }
}

void ImageLensWidget::attachToOwner(QWidget* owner)
{
    if (isWaylandSession())
    {
        QWidget* overlayParent = owner ? owner->window() : nullptr;
        if (this->parentWidget() == overlayParent && !this->isWindow())
        {
            return;
        }

        this->setParent(overlayParent);
        return;
    }

    if (this->parentWidget() == owner && this->isWindow())
    {
        return;
    }

    this->setParent(owner, this->windowFlags());
}

void ImageLensWidget::detachFromOwner()
{
    if (isWaylandSession())
    {
        this->setParent(nullptr);
        return;
    }

    this->setParent(nullptr, this->windowFlags());
}

QColor ImageLensWidget::getBorderColor() const
{
    return lensColors().borderPrimary;
}

void ImageLensWidget::changeEvent(QEvent* ev)
{
    if (ev->type() == QEvent::ApplicationPaletteChange || ev->type() == QEvent::ThemeChange)
        update();
    QWidget::changeEvent(ev);
}

void ImageLensWidget::paintEvent(QPaintEvent* evt)
{
    evt->accept();

    QPainter painter(this);

    painter.setRenderHint(QPainter::Antialiasing, true);

    drawBackground(painter);
    drawText(painter);
    drawPixmap(painter);
}

void ImageLensWidget::drawBackground(QPainter& painter)
{
    const auto& c = lensColors();
    painter.setOpacity(0.8);
    painter.fillRect(rect().x(), rect().y(), rect().width(), rect().height(), c.bgPrimary);

    QPen pen = painter.pen();
    pen.setColor(c.borderPrimary);
    pen.setWidth(1);
    painter.setPen(pen);

    painter.setOpacity(1.0);

    painter.drawRect(rect().x(), rect().y(), rect().width(), rect().height());
}

void ImageLensWidget::drawText(QPainter& painter)
{
    QPen pen = painter.pen();
    pen.setColor(lensColors().textPrimary);
    pen.setWidth(1);
    painter.setPen(pen);

    painter.setFont(an::ui::tokens().typography.monospace);

    QRect textArea(10, 5, 80, 200);
    std::string textFmt = fmt::format("X: {:.3f}\nY: {:.3f}\n\n", m_CurrentX, m_CurrentY);

    if (isRGB)
    {
        textFmt +=
            fmt::format("R: {:.5f}\nG: {:.5f}\nB: {:.5f}\n", m_CurrentChannel0, m_CurrentChannel1, m_CurrentChannel2);
    }
    else
    {
        textFmt += fmt::format("K: {:.5f}\n", m_CurrentChannel0);
    }

    if (isMasked)
    {
        if (m_CurrentMaskValues.size() == 3)
        {
            textFmt += fmt::format("\nM0: {:.5f}\nM1: {:.5f}\nM2: {:.5f}\n", m_CurrentMaskValues[0],
                                   m_CurrentMaskValues[1], m_CurrentMaskValues[2]);
        }
        else
        {
            textFmt += fmt::format("\nM: {:.5f}\n", m_CurrentMaskValues[0]);
        }
    }

    painter.drawText(textArea, Qt::AlignLeft, QString::fromStdString(textFmt));
}

void ImageLensWidget::drawPixmap(QPainter& painter)
{
    painter.drawPixmap(95, 5, 250, 240, this->m_CurrentPixmap);

    if (isMasked)
    {
        for (auto& pmap : m_CurrentMaskPixmaps)
            painter.drawPixmap(95, 5, 250, 240, pmap);
    }
}
