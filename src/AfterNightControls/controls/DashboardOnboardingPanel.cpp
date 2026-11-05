// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "DashboardOnboardingPanel.h"
#include "DashboardUtils.h"
#include "theme/AnDesignTokens.h"

#include <QEnterEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(an::ui::DashboardOnboardingPanel)

using DashboardUtils::scaledPixels;
using DashboardUtils::scaledSystemFont;

// ═══════════════════════════════════════════════════════════════════════
// Anonymous-namespace helper widgets (no MOC required)
// ═══════════════════════════════════════════════════════════════════════

namespace
{

// ── Shadow constants ────────────────────────────────────────────────
constexpr int SHADOW_BLUR = 6;
constexpr int SHADOW_OFFSET = 3;
constexpr int FEATURE_GRID_MAX_WIDTH = 900;
constexpr int FEATURE_GRID_MIN_WIDTH = 600;
constexpr int FEATURE_GRID_MAX_COLUMNS = 3;

// ── Feature Card ────────────────────────────────────────────────────
// A small card showing an icon, a title, and a short description.
// No signals needed — purely visual.
class FeatureCard : public QFrame
{
public:
    explicit FeatureCard(const QString& title, const QString& description, QWidget* parent = nullptr) : QFrame(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setObjectName(QStringLiteral("dashboardFeatureCard"));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setSpacing(10);

        m_iconLabel = new QLabel(this);
        m_iconLabel->setFixedSize(28, 28);
        m_iconLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_iconLabel, 0, Qt::AlignTop);

        auto* textCol = new QVBoxLayout();
        textCol->setContentsMargins(0, 0, 0, 0);
        textCol->setSpacing(4);

        m_titleLabel = new QLabel(title, this);
        m_titleLabel->setObjectName(QStringLiteral("dashboardEmptyStateFeatureTitle"));
        m_titleLabel->setWordWrap(false);
        textCol->addWidget(m_titleLabel);

        m_descLabel = new QLabel(description, this);
        m_descLabel->setObjectName(QStringLiteral("dashboardFeatureCardDesc"));
        m_descLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_descLabel->setWordWrap(true);
        textCol->addWidget(m_descLabel);

        layout->addLayout(textCol, 1);
    }

    void setIcon(const QIcon& icon)
    {
        if (icon.isNull())
        {
            return;
        }
        m_iconLabel->setPixmap(icon.pixmap(28, 28));
    }

    void refreshTheme()
    {
        const auto& t = an::ui::tokens();
        const bool dark = an::ui::isDarkTheme();

        m_titleLabel->setFont(t.typography.body);
        {
            QPalette pal = m_titleLabel->palette();
            pal.setColor(QPalette::Active, QPalette::WindowText, t.colors.textPrimary);
            pal.setColor(QPalette::Inactive, QPalette::WindowText, t.colors.textPrimary);
            pal.setColor(QPalette::Disabled, QPalette::WindowText, t.colors.textPrimary);
            m_titleLabel->setPalette(pal);
        }

        m_descLabel->setFont(t.typography.caption);
        {
            QPalette pal = m_descLabel->palette();
            pal.setColor(QPalette::Active, QPalette::WindowText, t.colors.textTertiary);
            pal.setColor(QPalette::Inactive, QPalette::WindowText, t.colors.textTertiary);
            pal.setColor(QPalette::Disabled, QPalette::WindowText, t.colors.textTertiary);
            m_descLabel->setPalette(pal);
        }
        // Subtle tinted background and border from theme state.
        m_backgroundColor = t.colors.bgHover;
        m_backgroundColor.setAlpha(dark ? 32 : 14);
        m_borderColor = t.colors.borderSubtle;
        m_borderColor.setAlpha(dark ? 70 : 45);

        update();
    }

    QSize sizeHint() const override
    {
        QSize hint = QFrame::sizeHint();
        hint.setWidth(
            (std::min)(hint.width(), FEATURE_GRID_MAX_WIDTH / FEATURE_GRID_MAX_COLUMNS - an::ui::tokens().spacing.md));
        return hint;
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QFrame::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const qreal radius = static_cast<qreal>(an::ui::tokens().radius.md);
        painter.setPen(QPen(m_borderColor, 1.0));
        painter.setBrush(m_backgroundColor);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    }

private:
    QLabel* m_iconLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_descLabel = nullptr;
    QColor m_backgroundColor = Qt::transparent;
    QColor m_borderColor = Qt::transparent;
};

// ── Action Card ─────────────────────────────────────────────────────
// A clickable card with icon + title + subtitle that emits a callback.
class ActionCard : public QFrame
{
public:
    explicit ActionCard(const QString& title, const QString& subtitle, QWidget* parent = nullptr)
        : QFrame(parent), m_title(title), m_subtitle(subtitle)
    {
        setFrameShape(QFrame::NoFrame);
        setObjectName(QStringLiteral("dashboardActionCard"));
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(14, 16, 14, 16);
        layout->setSpacing(8);
        layout->setSizeConstraint(QLayout::SetMinimumSize);

        m_iconLabel = new QLabel(this);
        m_iconLabel->setFixedSize(36, 36);
        m_iconLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_iconLabel, 0, Qt::AlignHCenter);

        m_titleLabel = new QLabel(title, this);
        m_titleLabel->setObjectName(QStringLiteral("dashboardActionCardTitle"));
        m_titleLabel->setAlignment(Qt::AlignCenter);
        m_titleLabel->setWordWrap(false);
        layout->addWidget(m_titleLabel);

        m_subtitleLabel = new QLabel(subtitle, this);
        m_subtitleLabel->setObjectName(QStringLiteral("dashboardActionCardSubtitle"));
        m_subtitleLabel->setAlignment(Qt::AlignCenter);
        m_subtitleLabel->setWordWrap(false);
        layout->addWidget(m_subtitleLabel);
    }

    std::function<void()> onClicked;

    void setIcon(const QIcon& icon)
    {
        if (icon.isNull())
        {
            return;
        }
        m_iconLabel->setPixmap(icon.pixmap(36, 36));
    }

    void refreshTheme()
    {
        const auto& t = an::ui::tokens();

        m_titleLabel->setFont([&]() {
            QFont f = t.typography.body;
            f.setWeight(QFont::DemiBold);
            return f;
        }());
        {
            QPalette pal = m_titleLabel->palette();
            pal.setColor(QPalette::WindowText, t.colors.textPrimary);
            m_titleLabel->setPalette(pal);
        }

        m_subtitleLabel->setFont(t.typography.caption);
        {
            QPalette pal = m_subtitleLabel->palette();
            pal.setColor(QPalette::WindowText, t.colors.textSecondary);
            m_subtitleLabel->setPalette(pal);
        }

        update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const auto& t = an::ui::tokens();
        const bool dark = an::ui::isDarkTheme();
        const qreal radius = static_cast<qreal>(t.radius.md);

        QColor bg;
        QColor border;
        if (m_pressed)
        {
            bg = t.colors.accentPrimary;
            bg.setAlphaF(dark ? 0.20 : 0.15);
            border = t.colors.accentPrimary;
            border.setAlphaF(dark ? 0.35 : 0.30);
        }
        else if (m_hovered)
        {
            bg = t.colors.accentPrimary;
            bg.setAlphaF(dark ? 0.12 : 0.08);
            border = t.colors.accentPrimary;
            border.setAlphaF(dark ? 0.25 : 0.18);
        }
        else
        {
            bg = t.colors.bgHover;
            bg.setAlpha(dark ? 36 : 16);
            border = t.colors.borderSubtle;
            border.setAlpha(dark ? 80 : 52);
        }

        painter.setPen(QPen(border, 1.0));
        painter.setBrush(bg);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    }

    void enterEvent(QEnterEvent* /*event*/) override
    {
        m_hovered = true;
        update();
    }

    void leaveEvent(QEvent* /*event*/) override
    {
        m_hovered = false;
        m_pressed = false;
        update();
    }

    void mousePressEvent(QMouseEvent* /*event*/) override
    {
        m_pressed = true;
        update();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        m_pressed = false;
        update();
        if (rect().contains(event->pos()) && onClicked)
        {
            onClicked();
        }
    }

private:
    QLabel* m_iconLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_subtitleLabel = nullptr;
    QString m_title;
    QString m_subtitle;
    bool m_hovered = false;
    bool m_pressed = false;
};

// ── Drop Zone ───────────────────────────────────────────────────────
// A visual drop area with a dashed border that highlights on drag.
class DropZoneWidget : public QFrame
{
public:
    explicit DropZoneWidget(QWidget* parent = nullptr) : QFrame(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setObjectName(QStringLiteral("dashboardDropZone"));
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(16, 20, 16, 20);
        layout->setSpacing(8);
        layout->setSizeConstraint(QLayout::SetMinimumSize);

        m_iconLabel = new QLabel(this);
        m_iconLabel->setFixedSize(40, 40);
        m_iconLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_iconLabel, 0, Qt::AlignHCenter);

        m_titleLabel = new QLabel(QObject::tr("Drop your images here to get started"), this);
        m_titleLabel->setObjectName(QStringLiteral("dashboardDropZoneTitle"));
        m_titleLabel->setAlignment(Qt::AlignCenter);
        m_titleLabel->setWordWrap(false);
        layout->addWidget(m_titleLabel);

        m_subtitleLabel = new QLabel(QObject::tr("Supports FITS, XISF, TIFF, RAW, JPEG and PNG"), this);
        m_subtitleLabel->setObjectName(QStringLiteral("dashboardDropZoneSubtitle"));
        m_subtitleLabel->setAlignment(Qt::AlignCenter);
        m_subtitleLabel->setWordWrap(false);
        layout->addWidget(m_subtitleLabel);
    }

    void setIcon(const QIcon& icon)
    {
        if (icon.isNull())
        {
            return;
        }
        m_iconLabel->setPixmap(icon.pixmap(40, 40));
    }

    void setHighlighted(bool highlighted)
    {
        if (m_highlighted == highlighted)
        {
            return;
        }
        m_highlighted = highlighted;
        update();
    }

    void refreshTheme()
    {
        const auto& t = an::ui::tokens();

        m_titleLabel->setFont(t.typography.body);
        {
            QPalette pal = m_titleLabel->palette();
            pal.setColor(QPalette::WindowText, t.colors.textSecondary);
            m_titleLabel->setPalette(pal);
        }

        m_subtitleLabel->setFont(t.typography.caption);
        {
            QPalette pal = m_subtitleLabel->palette();
            pal.setColor(QPalette::WindowText, t.colors.textTertiary);
            m_subtitleLabel->setPalette(pal);
        }

        update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const auto& t = an::ui::tokens();
        const bool dark = an::ui::isDarkTheme();
        const qreal radius = static_cast<qreal>(t.radius.lg);

        QRectF bounds = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);

        // Fill
        if (m_highlighted)
        {
            QColor fill = t.colors.accentPrimary;
            fill.setAlphaF(dark ? 0.10 : 0.06);
            painter.setBrush(fill);
        }
        else
        {
            painter.setBrush(Qt::NoBrush);
        }

        // Dashed border
        QPen dashPen;
        if (m_highlighted)
        {
            QColor borderColor = t.colors.accentPrimary;
            borderColor.setAlphaF(dark ? 0.60 : 0.50);
            dashPen = QPen(borderColor, 2.0, Qt::DashLine);
        }
        else
        {
            QColor borderColor = t.colors.borderSubtle;
            borderColor.setAlpha(dark ? 96 : 72);
            dashPen = QPen(borderColor, 1.5, Qt::DashLine);
        }
        dashPen.setDashPattern({6, 4});
        painter.setPen(dashPen);

        painter.drawRoundedRect(bounds, radius, radius);
    }

private:
    QLabel* m_iconLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_subtitleLabel = nullptr;
    bool m_highlighted = false;
};

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════
// DashboardOnboardingPanel implementation
// ═══════════════════════════════════════════════════════════════════════

struct an::ui::DashboardOnboardingPanel::Impl
{
    qreal dpiScale = 1.0;

    // Background images (pre-loaded)
    QPixmap bgDark;
    QPixmap bgLight;

    // Logo
    QLabel* logoLabel = nullptr;

    // Text
    QLabel* titleLabel = nullptr;
    QLabel* subtitleLabel = nullptr;
    QLabel* descriptionLabel = nullptr;

    // Feature cards (Option B)
    QWidget* featuresContainer = nullptr;
    QGridLayout* featureGrid = nullptr;
    QLabel* featuresSectionLabel = nullptr;
    std::array<FeatureCard*, 6> featureCards{};
    int featureGridColumns = FEATURE_GRID_MAX_COLUMNS;

    // Action cards (Option C)
    QWidget* actionsContainer = nullptr;
    QLabel* actionsSectionLabel = nullptr;
    ActionCard* openCard = nullptr;
    ActionCard* sequenceCard = nullptr;
    ActionCard* integrationCard = nullptr;

    // Drop zone (Option D)
    DropZoneWidget* dropZone = nullptr;

    // Keyboard hints
    QLabel* keyboardHints = nullptr;
};

// ─────────────────────────────────────────────────────────────────────
// Construction / destruction
// ─────────────────────────────────────────────────────────────────────

an::ui::DashboardOnboardingPanel::DashboardOnboardingPanel(qreal dpiScale, QWidget* parent)
    : QWidget(parent), m_impl(std::make_unique<Impl>())
{
    m_impl->dpiScale = dpiScale;
    setObjectName(QStringLiteral("dashboardEmptyStateOnboardingPanel"));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    // Pre-load background images
    m_impl->bgDark = QPixmap(QStringLiteral(":/AfterNight/icons/PNG/onboarding_bg_dark.png"));
    m_impl->bgLight = QPixmap(QStringLiteral(":/AfterNight/icons/PNG/onboarding_bg_light.png"));

    buildLayout();
}

an::ui::DashboardOnboardingPanel::~DashboardOnboardingPanel() = default;

// ─────────────────────────────────────────────────────────────────────
// Layout construction
// ─────────────────────────────────────────────────────────────────────

void an::ui::DashboardOnboardingPanel::buildLayout()
{
    const qreal s = m_impl->dpiScale;
    const int shadowMarginH = SHADOW_BLUR + scaledPixels(s, 22, 14);
    const int shadowMarginTop = SHADOW_BLUR + scaledPixels(s, 22, 14);
    const int shadowMarginBottom = SHADOW_BLUR + SHADOW_OFFSET + scaledPixels(s, 22, 14);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(shadowMarginH, shadowMarginTop, shadowMarginH, shadowMarginBottom);
    rootLayout->setSpacing(scaledPixels(s, 10, 8));
    rootLayout->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

    buildLogoSection(rootLayout);
    buildTitleSection(rootLayout);

    rootLayout->addSpacing(scaledPixels(s, 12, 8));
    buildFeatureCardsSection(rootLayout);

    rootLayout->addSpacing(scaledPixels(s, 14, 10));
    buildActionCardsSection(rootLayout);

    rootLayout->addSpacing(scaledPixels(s, 10, 6));
    buildDropZoneSection(rootLayout);

    rootLayout->addSpacing(scaledPixels(s, 6, 4));
    buildKeyboardHintsSection(rootLayout);
}

void an::ui::DashboardOnboardingPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateFeatureCardLayout();
}

void an::ui::DashboardOnboardingPanel::buildLogoSection(QVBoxLayout* parent)
{
    m_impl->logoLabel = new QLabel(this);
    m_impl->logoLabel->setObjectName(QStringLiteral("dashboardOnboardingLogo"));
    // Enlarged logo (Option E) — 96px scaled
    const int logoSize = scaledPixels(m_impl->dpiScale, 96, 72);
    QPixmap logoPix(QStringLiteral(":/AfterNight/icons/PNG/afternight_icon_256x256_blue.png"));
    if (!logoPix.isNull())
    {
        m_impl->logoLabel->setPixmap(logoPix.scaled(logoSize, logoSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    m_impl->logoLabel->setAlignment(Qt::AlignCenter);
    m_impl->logoLabel->setFixedSize(logoSize, logoSize);
    parent->addWidget(m_impl->logoLabel, 0, Qt::AlignHCenter);
}

void an::ui::DashboardOnboardingPanel::buildTitleSection(QVBoxLayout* parent)
{
    m_impl->titleLabel = new QLabel(tr("Welcome to AfterNight"), this);
    m_impl->titleLabel->setObjectName(QStringLiteral("dashboardOnboardingTitle"));
    m_impl->titleLabel->setAlignment(Qt::AlignCenter);
    m_impl->titleLabel->setWordWrap(true);
    parent->addWidget(m_impl->titleLabel);

    m_impl->subtitleLabel = new QLabel(tr("Astrophotography Processing Suite"), this);
    m_impl->subtitleLabel->setObjectName(QStringLiteral("dashboardOnboardingSubtitle"));
    m_impl->subtitleLabel->setAlignment(Qt::AlignCenter);
    m_impl->subtitleLabel->setWordWrap(true);
    parent->addWidget(m_impl->subtitleLabel);

    m_impl->descriptionLabel = new QLabel(tr("Calibrate, align, integrate and enhance your deep-sky images\n"
                                             "with professional-grade tools built for astrophotographers."),
                                          this);
    m_impl->descriptionLabel->setObjectName(QStringLiteral("dashboardOnboardingDescription"));
    m_impl->descriptionLabel->setAlignment(Qt::AlignCenter);
    m_impl->descriptionLabel->setWordWrap(true);
    parent->addWidget(m_impl->descriptionLabel);
}

void an::ui::DashboardOnboardingPanel::buildFeatureCardsSection(QVBoxLayout* parent)
{
    m_impl->featuresSectionLabel = new QLabel(tr("What you can do"), this);
    m_impl->featuresSectionLabel->setObjectName(QStringLiteral("dashboardOnboardingFeaturesHeader"));
    m_impl->featuresSectionLabel->setAlignment(Qt::AlignCenter);
    parent->addWidget(m_impl->featuresSectionLabel);

    parent->addSpacing(scaledPixels(m_impl->dpiScale, 4, 2));

    m_impl->featuresContainer = new QWidget(this);
    m_impl->featuresContainer->setObjectName(QStringLiteral("dashboardEmptyStateFeaturesContainer"));
    m_impl->featuresContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_impl->featureGrid = new QGridLayout(m_impl->featuresContainer);
    m_impl->featureGrid->setContentsMargins(0, 0, 0, 0);
    m_impl->featureGrid->setSpacing(scaledPixels(m_impl->dpiScale, 12, 8));
    for (int column = 0; column < FEATURE_GRID_MAX_COLUMNS; ++column)
    {
        m_impl->featureGrid->setColumnStretch(column, 1);
    }

    // Six features initially arranged in three columns; narrow dashboards reflow to fewer columns.
    struct FeatureInfo
    {
        const char* title;
        const char* description;
    };
    constexpr std::array<FeatureInfo, 6> features = {{
        {QT_TR_NOOP("Multi-Format Support"), QT_TR_NOOP("FITS, XISF, TIFF, RAW, JPEG and PNG")},
        {QT_TR_NOOP("Frame Calibration"), QT_TR_NOOP("Bias, dark and flat-field correction")},
        {QT_TR_NOOP("Star Alignment"), QT_TR_NOOP("Sub-exposure registration and alignment")},
        {QT_TR_NOOP("Image Stacking"), QT_TR_NOOP("Sigma clipping, averaging and drizzle")},
        {QT_TR_NOOP("Background Extraction"), QT_TR_NOOP("Gradient removal and sky modelling")},
        {QT_TR_NOOP("Enhancement"), QT_TR_NOOP("Noise reduction, sharpening and curves")},
    }};

    for (int i = 0; i < 6; ++i)
    {
        auto* card = new FeatureCard(tr(features[i].title), tr(features[i].description), m_impl->featuresContainer);
        m_impl->featureCards[i] = card;
        m_impl->featureGrid->addWidget(card, i / FEATURE_GRID_MAX_COLUMNS, i % FEATURE_GRID_MAX_COLUMNS);
    }

    // Make room for three full text cards on platforms with wider system fonts.
    m_impl->featuresContainer->setMaximumWidth(
        scaledPixels(m_impl->dpiScale, FEATURE_GRID_MAX_WIDTH, FEATURE_GRID_MIN_WIDTH));
    parent->addWidget(m_impl->featuresContainer);
}

void an::ui::DashboardOnboardingPanel::updateFeatureCardLayout()
{
    if (!m_impl->featuresContainer || !m_impl->featureGrid)
    {
        return;
    }

    const int availableWidth = m_impl->featuresContainer->contentsRect().width();
    if (availableWidth <= 0)
    {
        return;
    }

    const int spacing = (std::max)(0, m_impl->featureGrid->horizontalSpacing());
    const int threeColumnWidth = m_impl->featuresContainer->maximumWidth();
    const int twoColumnWidth = scaledPixels(m_impl->dpiScale, FEATURE_GRID_MIN_WIDTH, FEATURE_GRID_MIN_WIDTH);
    const int columnCount =
        availableWidth >= threeColumnWidth ? FEATURE_GRID_MAX_COLUMNS : (availableWidth >= twoColumnWidth ? 2 : 1);
    const int columnWidth = (std::max)(0, (availableWidth - spacing * (columnCount - 1)) / columnCount);

    for (int column = 0; column < FEATURE_GRID_MAX_COLUMNS; ++column)
    {
        const bool activeColumn = column < columnCount;
        m_impl->featureGrid->setColumnMinimumWidth(column, activeColumn ? columnWidth : 0);
        m_impl->featureGrid->setColumnStretch(column, activeColumn ? 1 : 0);
    }

    if (m_impl->featureGridColumns == columnCount)
    {
        m_impl->featureGrid->invalidate();
        return;
    }

    for (FeatureCard* card : m_impl->featureCards)
    {
        if (card)
        {
            m_impl->featureGrid->removeWidget(card);
        }
    }
    for (int index = 0; index < static_cast<int>(m_impl->featureCards.size()); ++index)
    {
        if (FeatureCard* card = m_impl->featureCards[index])
        {
            m_impl->featureGrid->addWidget(card, index / columnCount, index % columnCount);
        }
    }

    m_impl->featureGridColumns = columnCount;
    m_impl->featureGrid->invalidate();
    m_impl->featuresContainer->updateGeometry();
}

void an::ui::DashboardOnboardingPanel::buildActionCardsSection(QVBoxLayout* parent)
{
    m_impl->actionsSectionLabel = new QLabel(tr("Get started"), this);
    m_impl->actionsSectionLabel->setObjectName(QStringLiteral("dashboardOnboardingActionsHeader"));
    m_impl->actionsSectionLabel->setAlignment(Qt::AlignCenter);
    parent->addWidget(m_impl->actionsSectionLabel);

    parent->addSpacing(scaledPixels(m_impl->dpiScale, 4, 2));

    m_impl->actionsContainer = new QWidget(this);
    m_impl->actionsContainer->setObjectName(QStringLiteral("dashboardOnboardingActionsRow"));
    auto* row = new QHBoxLayout(m_impl->actionsContainer);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(scaledPixels(m_impl->dpiScale, 12, 8));

    m_impl->openCard =
        new ActionCard(tr("Open Image"), tr("FITS \u00b7 XISF \u00b7 TIFF \u00b7 RAW"), m_impl->actionsContainer);
    m_impl->openCard->onClicked = [this]() { emit openFileRequested(); };
    row->addWidget(m_impl->openCard, 1);

    m_impl->sequenceCard =
        new ActionCard(tr("New Sequence"), tr("Start a processing session"), m_impl->actionsContainer);
    m_impl->sequenceCard->onClicked = [this]() { emit newSequenceRequested(); };
    row->addWidget(m_impl->sequenceCard, 1);

    m_impl->integrationCard =
        new ActionCard(tr("Image Integration"), tr("Calibrate, align and stack"), m_impl->actionsContainer);
    m_impl->integrationCard->onClicked = [this]() { emit imageIntegrationRequested(); };
    row->addWidget(m_impl->integrationCard, 1);

    m_impl->actionsContainer->setMaximumWidth(scaledPixels(m_impl->dpiScale, 820, 600));
    parent->addWidget(m_impl->actionsContainer, 0, Qt::AlignHCenter);
}

void an::ui::DashboardOnboardingPanel::buildDropZoneSection(QVBoxLayout* parent)
{
    m_impl->dropZone = new DropZoneWidget(this);
    m_impl->dropZone->setMaximumWidth(scaledPixels(m_impl->dpiScale, 820, 600));
    parent->addWidget(m_impl->dropZone, 0, Qt::AlignHCenter);
}

void an::ui::DashboardOnboardingPanel::buildKeyboardHintsSection(QVBoxLayout* parent)
{
    m_impl->keyboardHints =
        new QLabel(tr("Ctrl+O to open  \u00b7  Ctrl+N for new sequence  \u00b7  F1 for help"), this);
    m_impl->keyboardHints->setObjectName(QStringLiteral("dashboardOnboardingKeyboardHints"));
    m_impl->keyboardHints->setAlignment(Qt::AlignCenter);
    m_impl->keyboardHints->setWordWrap(true);
    parent->addWidget(m_impl->keyboardHints);
}

// ─────────────────────────────────────────────────────────────────────
// Painting — Option A
// ─────────────────────────────────────────────────────────────────────

void an::ui::DashboardOnboardingPanel::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const auto& t = tokens();
    const qreal radius = static_cast<qreal>(t.radius.lg);
    QRectF bounds = QRectF(rect());

    // ── Shadow ──────────────────────────────────────────────────────
    paintShadow(painter, bounds, radius);

    bounds.adjust(SHADOW_BLUR, SHADOW_BLUR, -SHADOW_BLUR, -SHADOW_BLUR - SHADOW_OFFSET);

    // ── Clip to rounded rect ────────────────────────────────────────
    QPainterPath clipPath;
    clipPath.addRoundedRect(bounds, radius, radius);
    painter.setClipPath(clipPath);

    // ── Base card fill ──────────────────────────────────────────────
    painter.fillRect(bounds, t.colors.bgCard);

    // ── Background image overlay ────────────────────────────────────
    paintBackgroundImage(painter, bounds);

    // ── Border ──────────────────────────────────────────────────────
    painter.setClipping(false);
    painter.setPen(QPen(t.colors.borderSubtle, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(bounds.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
}

void an::ui::DashboardOnboardingPanel::paintShadow(QPainter& painter, const QRectF& bounds, qreal radius) const
{
    QColor shadowColor = tokens().colors.shadowOverlay;
    const double clampedAlpha = (std::min)(0.22, (std::max)(0.08, static_cast<double>(shadowColor.alphaF())));
    shadowColor.setAlphaF(clampedAlpha);
    QRectF shadowBounds = bounds.adjusted(SHADOW_BLUR, SHADOW_BLUR + SHADOW_OFFSET, -SHADOW_BLUR, -SHADOW_BLUR);
    painter.setPen(Qt::NoPen);
    QColor c = shadowColor;
    for (int i = 0; i < SHADOW_BLUR; ++i)
    {
        c.setAlphaF(shadowColor.alphaF() * (1.0 - static_cast<qreal>(i) / SHADOW_BLUR));
        painter.setBrush(c);
        painter.drawRoundedRect(shadowBounds.adjusted(-i, -i, i, i), radius + i, radius + i);
    }
}

void an::ui::DashboardOnboardingPanel::paintBackgroundImage(QPainter& painter, const QRectF& bounds) const
{
    const bool dark = isDarkTheme();
    const QPixmap& src = dark ? m_impl->bgDark : m_impl->bgLight;
    if (src.isNull())
    {
        return;
    }

    // Scale the source pixmap to fill the card bounds, preserving aspect ratio
    // (crop edges if necessary).
    const qreal srcAspect = static_cast<qreal>(src.width()) / src.height();
    const qreal dstAspect = bounds.width() / bounds.height();

    QRectF srcRect;
    if (srcAspect > dstAspect)
    {
        // Source is wider — crop horizontally
        const qreal usedWidth = src.height() * dstAspect;
        const qreal offsetX = (src.width() - usedWidth) * 0.5;
        srcRect = QRectF(offsetX, 0, usedWidth, src.height());
    }
    else
    {
        // Source is taller — crop vertically
        const qreal usedHeight = src.width() / dstAspect;
        const qreal offsetY = (src.height() - usedHeight) * 0.5;
        srcRect = QRectF(0, offsetY, src.width(), usedHeight);
    }

    painter.drawPixmap(bounds, src, srcRect);
}

// ─────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────

void an::ui::DashboardOnboardingPanel::setActionIcons(const QIcon& openFile, const QIcon& newSequence,
                                                      const QIcon& integration)
{
    if (m_impl->openCard)
    {
        m_impl->openCard->setIcon(openFile);
    }
    if (m_impl->sequenceCard)
    {
        m_impl->sequenceCard->setIcon(newSequence);
    }
    if (m_impl->integrationCard)
    {
        m_impl->integrationCard->setIcon(integration);
    }
}

void an::ui::DashboardOnboardingPanel::setFeatureIcon(int index, const QIcon& icon)
{
    if (index < 0 || index >= static_cast<int>(m_impl->featureCards.size()))
    {
        return;
    }
    if (m_impl->featureCards[index])
    {
        m_impl->featureCards[index]->setIcon(icon);
    }
}

void an::ui::DashboardOnboardingPanel::setDropZoneIcon(const QIcon& icon)
{
    if (m_impl->dropZone)
    {
        m_impl->dropZone->setIcon(icon);
    }
}

void an::ui::DashboardOnboardingPanel::setDragHighlighted(bool highlighted)
{
    if (m_impl->dropZone)
    {
        m_impl->dropZone->setHighlighted(highlighted);
    }
}

void an::ui::DashboardOnboardingPanel::refreshTheme()
{
    const auto& t = tokens();

    // Title
    if (m_impl->titleLabel)
    {
        m_impl->titleLabel->setFont(t.typography.heading1);
        QPalette pal = m_impl->titleLabel->palette();
        pal.setColor(QPalette::WindowText, t.colors.textPrimary);
        m_impl->titleLabel->setPalette(pal);
    }

    // Subtitle
    if (m_impl->subtitleLabel)
    {
        m_impl->subtitleLabel->setFont(t.typography.body);
        QPalette pal = m_impl->subtitleLabel->palette();
        pal.setColor(QPalette::WindowText, t.colors.accentPrimary);
        m_impl->subtitleLabel->setPalette(pal);
    }

    // Description
    if (m_impl->descriptionLabel)
    {
        m_impl->descriptionLabel->setFont(t.typography.body);
        QPalette pal = m_impl->descriptionLabel->palette();
        pal.setColor(QPalette::WindowText, t.colors.textSecondary);
        m_impl->descriptionLabel->setPalette(pal);
    }

    // Section headers
    for (QLabel* header : {m_impl->featuresSectionLabel, m_impl->actionsSectionLabel})
    {
        if (!header)
        {
            continue;
        }
        header->setFont(t.typography.heading3);
        QPalette pal = header->palette();
        pal.setColor(QPalette::WindowText, t.colors.textSecondary);
        header->setPalette(pal);
    }

    // Feature cards
    for (auto* card : m_impl->featureCards)
    {
        if (card)
        {
            card->refreshTheme();
        }
    }
    updateFeatureCardLayout();

    // Action cards
    if (m_impl->openCard)
    {
        m_impl->openCard->refreshTheme();
    }
    if (m_impl->sequenceCard)
    {
        m_impl->sequenceCard->refreshTheme();
    }
    if (m_impl->integrationCard)
    {
        m_impl->integrationCard->refreshTheme();
    }

    // Drop zone
    if (m_impl->dropZone)
    {
        m_impl->dropZone->refreshTheme();
    }

    // Keyboard hints
    if (m_impl->keyboardHints)
    {
        m_impl->keyboardHints->setFont(t.typography.caption);
        QPalette pal = m_impl->keyboardHints->palette();
        pal.setColor(QPalette::WindowText, t.colors.textSecondary);
        m_impl->keyboardHints->setPalette(pal);
    }

    update();
}
