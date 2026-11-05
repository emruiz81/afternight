// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "StackingRecommendationDialog.h"
#include "app/UiStyleTokens.h"
#include "theme/AnDesignTokens.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/tabs/CalibrationPanelUtils.h"

#include <verdigris/wobjectimpl.h>
#include <QApplication>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

W_OBJECT_IMPL(DrizzleCoverageWidget)
W_OBJECT_IMPL(StackingRecommendationDialog)

namespace
{

// --------------- Style helpers (no QSS) ---------------

void setCardFrame(QFrame* frame, const QColor& bg, const QColor& border, int radius = 6, bool dashed = false)
{
    frame->setFrameShape(QFrame::StyledPanel);
    QPalette pal = frame->palette();
    pal.setColor(QPalette::Window, bg);
    pal.setColor(QPalette::Mid, border);
    frame->setPalette(pal);
    frame->setProperty("anFrameRadius", radius);
    if (dashed)
        frame->setProperty("anFrameBorderDashed", true);
}

void setLabelColor(QWidget* label, const QColor& color)
{
    QPalette pal = label->palette();
    pal.setColor(QPalette::WindowText, color);
    label->setPalette(pal);
}

void setLabelFont(QWidget* label, const QColor& color, int pixelSize = -1, QFont::Weight weight = QFont::Normal,
                  bool italic = false, bool strikeOut = false)
{
    setLabelColor(label, color);
    const auto& typography = an::ui::tokens().typography;
    QFont f = typography.body;
    if (pixelSize > 0)
    {
        if (pixelSize >= 14)
        {
            f = typography.heading2;
        }
        else if (pixelSize >= 13)
        {
            f = typography.heading3;
        }
        else if (pixelSize <= 10)
        {
            f = typography.caption;
        }
    }
    if (weight != QFont::Normal)
        f.setWeight(weight);
    if (italic)
        f.setItalic(true);
    if (strikeOut)
        f.setStrikeOut(true);
    label->setFont(f);
}

} // namespace

DrizzleCoverageWidget::DrizzleCoverageWidget(QWidget* parent) : QWidget(parent)
{
    setFixedSize(64, 64);
}

void DrizzleCoverageWidget::setCoverage(double coverage, int filledBins, int totalBins)
{
    m_coverage = coverage;
    m_filledBins = filledBins;
    m_totalBins = totalBins;
    setToolTip(
        tr("%1/%2 sub-pixel bins covered (%3%)").arg(filledBins).arg(totalBins).arg(static_cast<int>(coverage * 100)));
    update();
}

void DrizzleCoverageWidget::paintEvent([[maybe_unused]] QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int gridSize = 4;
    const int padding = 2;
    const int cellSize = (width() - (gridSize + 1) * padding) / gridSize;

    // Fill-rank per cell index: lower rank = filled first.
    // Scattered across quadrants so partial fills don't form a misleading
    // sequential top-left-to-bottom-right pattern.
    static constexpr int kFillRank[16] = {
        0,  8,  12, 4, // row 0
        10, 2,  14, 6, // row 1
        5,  13, 1,  9, // row 2
        15, 7,  11, 3 // row 3
    };

    // Background
    painter.setPen(Qt::NoPen);
    painter.setBrush(palette().color(QPalette::Window));
    painter.drawRect(rect());

    // Grid
    for (int y = 0; y < gridSize; ++y)
    {
        for (int x = 0; x < gridSize; ++x)
        {
            QRect cellRect(padding + x * (cellSize + padding), padding + y * (cellSize + padding), cellSize, cellSize);

            const int binIndex = y * gridSize + x;
            const bool isFilled = kFillRank[binIndex] < m_filledBins;

            if (isFilled)
            {
                painter.setBrush(
                    an::ui::styling::semanticColor(palette(), an::ui::styling::SemanticColorRole::StatusSuccess));
            }
            else
            {
                painter.setBrush(palette().color(QPalette::Mid));
            }

            painter.drawRoundedRect(cellRect, an::ui::tokens().radius.sm, an::ui::tokens().radius.sm);
        }
    }
}

StackingRecommendationDialog::StackingRecommendationDialog(StackingSettingsDialog::FrameType frameType,
                                                           const QString& groupLabel, int frameCount,
                                                           const StackingRecommendation& recommendation,
                                                           const DrizzleAnalysis& drizzleAnalysis,
                                                           const StackingSettings& currentSettings, QWidget* parent)
    : QDialog(parent), m_frameType(frameType), m_groupLabel(groupLabel), m_frameCount(frameCount),
      m_recommendation(recommendation), m_drizzleAnalysis(drizzleAnalysis), m_currentSettings(currentSettings)
{
    setWindowTitle(tr("Recommended Stacking Settings"));

    // Scale dialog dimensions based on font metrics for DPI awareness
    const int em = fontMetrics().height();
    resize(qMax(em * 40, 600), qMax(em * 40, 500));

    const auto themeVariant = an::ui::styling::detectThemeVariant(palette());
    m_accentColor = an::ui::styling::semanticColor(themeVariant, an::ui::styling::SemanticColorRole::AccentPrimary);
    m_goodColor = an::ui::styling::semanticColor(themeVariant, an::ui::styling::SemanticColorRole::StatusSuccess);
    m_warnColor = an::ui::styling::semanticColor(themeVariant, an::ui::styling::SemanticColorRole::StatusWarning);
    m_cardBg = an::ui::styling::semanticColor(themeVariant, an::ui::styling::SemanticColorRole::SurfaceOverlay);
    m_cardBorder = an::ui::styling::semanticColor(themeVariant, an::ui::styling::SemanticColorRole::SurfaceBorder);
    m_mutedTextColor = an::ui::styling::semanticColor(themeVariant, an::ui::styling::SemanticColorRole::TextSecondary);
    m_subtleTextColor = an::ui::styling::semanticColor(themeVariant, an::ui::styling::SemanticColorRole::TextMuted);

    setupUi();
}

void StackingRecommendationDialog::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // --- Header Section ---
    auto* headerLayout = new QVBoxLayout();
    auto* titleLabel = new QLabel(tr("Recommendations for group \"%1\"").arg(m_groupLabel));
    titleLabel->setFont(an::ui::tokens().typography.heading2);
    headerLayout->addWidget(titleLabel);

    QString frameTypeName;
    switch (m_frameType)
    {
    case StackingSettingsDialog::FrameType::BIAS:
        frameTypeName = tr("BIAS");
        break;
    case StackingSettingsDialog::FrameType::DARK:
        frameTypeName = tr("DARK");
        break;
    case StackingSettingsDialog::FrameType::FLAT:
        frameTypeName = tr("FLAT");
        break;
    case StackingSettingsDialog::FrameType::LIGHT:
        frameTypeName = tr("LIGHT");
        break;
    default:
        Q_UNREACHABLE();
        break;
    }
    auto* subtitleLabel =
        new QLabel(tr("Based on %1 <b>%2</b> frames in this stacking group.").arg(m_frameCount).arg(frameTypeName));
    headerLayout->addWidget(subtitleLabel);
    mainLayout->addLayout(headerLayout);

    // --- Comparison Section ---
    auto* comparisonGroup = new QFrame();
    setCardFrame(comparisonGroup, m_cardBg, m_cardBorder);

    auto* comparisonLayout = new QVBoxLayout(comparisonGroup);

    auto* compHeader = new QLabel(tr("PARAMETER COMPARISON"));
    setLabelFont(compHeader, m_accentColor, 10, QFont::Bold);
    comparisonLayout->addWidget(compHeader);

    auto* gridLayout = new QGridLayout();
    gridLayout->setColumnStretch(1, 1);
    gridLayout->setColumnStretch(2, 1);

    // Grid Header
    auto* h1 = new QLabel(tr("Setting"));
    auto* h2 = new QLabel(tr("Current Value"));
    auto* h3 = new QLabel(tr("Recommended"));
    QFont hFont = an::ui::tokens().typography.heading3;
    for (auto* h : {h1, h2, h3})
    {
        h->setFont(hFont);
        setLabelColor(h, m_subtleTextColor);
    }

    gridLayout->addWidget(h1, 0, 0);
    gridLayout->addWidget(h2, 0, 1);
    gridLayout->addWidget(h3, 0, 2);

    int row = 1;

    // Method
    addComparisonRow(gridLayout, row++, tr("Stack Method"), m_currentSettings.stackMethod, m_recommendation.stackMethod,
                     m_currentSettings.stackMethod != m_recommendation.stackMethod);

    // Normalization — build both display texts through the same translation
    // pipeline (normalizationColumnText) to avoid locale-dependent mismatches.
    QString currentNorm = normalizationColumnText(m_currentSettings);

    StackingSettings recNormProxy;
    recNormProxy.localNormalization.enabled = m_recommendation.localNormalization;
    if (m_recommendation.localNormalization)
    {
        recNormProxy.normalizationMode = NormalizationMode::Local;
    }
    else
    {
        const auto& nm = m_recommendation.normalizationMode;
        if (nm == QStringLiteral("None"))
        {
            recNormProxy.normalizationMode = NormalizationMode::None;
        }
        else
        {
            recNormProxy.normalizationMode = NormalizationMode::Global;
            if (nm == QStringLiteral("Additive"))
                recNormProxy.globalNormalizationMethod = GlobalNormalizationMethod::Additive;
            else if (nm == QStringLiteral("Multiplicative"))
                recNormProxy.globalNormalizationMethod = GlobalNormalizationMethod::Multiplicative;
            else if (nm == QStringLiteral("Additive With Scaling"))
                recNormProxy.globalNormalizationMethod = GlobalNormalizationMethod::AdditiveScaling;
            else if (nm == QStringLiteral("Multiplicative With Scaling"))
                recNormProxy.globalNormalizationMethod = GlobalNormalizationMethod::MultiplicativeScaling;
        }
    }
    QString recommendedNorm = normalizationColumnText(recNormProxy);
    bool normalizationDifferent = currentNorm != recommendedNorm;

    // Respect user's local normalization selection: do not suggest replacing it
    // with a global normalization method.
    if (m_currentSettings.localNormalization.enabled || m_currentSettings.normalizationMode == NormalizationMode::Local)
    {
        recommendedNorm = currentNorm;
        normalizationDifferent = false;
    }

    addComparisonRow(gridLayout, row++, tr("Normalization"), currentNorm, recommendedNorm, normalizationDifferent);

    // Rejection
    addComparisonRow(gridLayout, row++, tr("Rejection Method"), m_currentSettings.rejectionMethod,
                     m_recommendation.rejectionMethod,
                     m_currentSettings.rejectionMethod != m_recommendation.rejectionMethod);

    // Weighting
    if (m_frameType == StackingSettingsDialog::FrameType::LIGHT)
    {
        addComparisonRow(gridLayout, row++, tr("Weighting"), m_currentSettings.weighting,
                         m_recommendation.weightingMethod.isEmpty() ? tr("None") : m_recommendation.weightingMethod,
                         m_currentSettings.weighting != m_recommendation.weightingMethod &&
                             !m_recommendation.weightingMethod.isEmpty());

        // Drizzle pixel fraction (only when drizzle is viable)
        if (m_recommendation.drizzleVerdict == StackingRecommendation::DrizzleVerdict::Recommended ||
            m_recommendation.drizzleVerdict == StackingRecommendation::DrizzleVerdict::Possible)
        {
            const QString currentPixfrac = m_currentSettings.drizzle.enabled
                                               ? QString::number(m_currentSettings.drizzle.pixelFraction, 'f', 1)
                                               : tr("N/A");
            const QString recPixfrac = QString::number(m_recommendation.drizzlePixfrac, 'f', 1);
            addComparisonRow(gridLayout, row++, tr("Pixel Fraction"), currentPixfrac, recPixfrac,
                             !m_currentSettings.drizzle.enabled || std::abs(m_currentSettings.drizzle.pixelFraction -
                                                                            m_recommendation.drizzlePixfrac) >= 0.01);
        }
    }

    comparisonLayout->addLayout(gridLayout);
    mainLayout->addWidget(comparisonGroup);

    // --- Rationale Section (Scrollable) ---
    auto* rationaleScroll = new QScrollArea();
    rationaleScroll->setWidgetResizable(true);
    rationaleScroll->setFrameShape(QFrame::NoFrame);
    rationaleScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    {
        QPalette scrollPal = rationaleScroll->palette();
        scrollPal.setColor(QPalette::Window, Qt::transparent);
        rationaleScroll->setPalette(scrollPal);
    }

    auto* rationaleContainer = new QWidget();
    auto* rationaleContainerLayout = new QVBoxLayout(rationaleContainer);
    rationaleContainerLayout->setContentsMargins(0, 0, 4, 0); // Leave room for scrollbar
    rationaleContainerLayout->setSpacing(12);

    // -- "Why" Card --
    auto* whyFrame = new QFrame();
    whyFrame->setObjectName("whyFrame");
    setCardFrame(whyFrame, m_cardBg, m_cardBorder);
    auto* whyLayout = new QVBoxLayout(whyFrame);
    whyLayout->setContentsMargins(14, 14, 14, 14);
    whyLayout->setSpacing(8);

    auto* coreRationaleHeader = new QLabel(tr("RATIONALE & ANALYSIS"));
    setLabelFont(coreRationaleHeader, m_accentColor, 10, QFont::Bold);
    whyLayout->addWidget(coreRationaleHeader);

    auto* reasonText = new QLabel(m_recommendation.reason);
    reasonText->setWordWrap(true);
    setLabelFont(reasonText, m_mutedTextColor, 13);
    whyLayout->addWidget(reasonText);

    // Drizzle Rationale (LIGHT groups only): always show status so users know
    // whether drizzle was evaluated, unavailable, or explicitly not recommended.
    if (m_frameType == StackingSettingsDialog::FrameType::LIGHT)
    {
        auto* separator = new QFrame();
        separator->setFrameShape(QFrame::HLine);
        {
            QPalette p = separator->palette();
            p.setColor(QPalette::Mid, m_cardBorder);
            separator->setPalette(p);
        }
        whyLayout->addWidget(separator);

        auto* drizzleHeaderLayout = new QHBoxLayout();
        auto* drizzleTitle = new QLabel(tr("Drizzle Viability Analysis"));
        drizzleTitle->setFont(an::ui::tokens().typography.heading3);
        drizzleHeaderLayout->addWidget(drizzleTitle);

        const bool analysisAvailable = m_drizzleAnalysis.available;
        QString verdictText;
        QColor verdictColor;

        if (!analysisAvailable)
        {
            verdictText = tr("UNAVAILABLE");
            verdictColor = an::ui::styling::semanticColor(palette(), an::ui::styling::SemanticColorRole::StatusPending);
        }
        else
        {
            switch (m_recommendation.drizzleVerdict)
            {
            case StackingRecommendation::DrizzleVerdict::Recommended:
                verdictText = tr("RECOMMENDED");
                verdictColor = m_goodColor;
                break;
            case StackingRecommendation::DrizzleVerdict::Possible:
                verdictText = tr("POSSIBLE");
                verdictColor = m_warnColor;
                break;
            case StackingRecommendation::DrizzleVerdict::NotRecommended:
                verdictText = tr("NOT RECOMMENDED");
                verdictColor =
                    an::ui::styling::semanticColor(palette(), an::ui::styling::SemanticColorRole::StatusPending);
                break;
            default:
                verdictText = tr("UNAVAILABLE");
                verdictColor =
                    an::ui::styling::semanticColor(palette(), an::ui::styling::SemanticColorRole::StatusPending);
                break;
            }
        }

        auto* badge = new QLabel(verdictText);
        badge->setAlignment(Qt::AlignCenter);
        badge->setFixedWidth(120);
        badge->setFrameShape(QFrame::StyledPanel);
        {
            QPalette badgePal = badge->palette();
            badgePal.setColor(QPalette::Window, verdictColor);
            badgePal.setColor(QPalette::WindowText, an::ui::styling::semanticColor(
                                                        palette(), an::ui::styling::SemanticColorRole::TextOnAccent));
            badgePal.setColor(QPalette::Mid, verdictColor);
            badge->setPalette(badgePal);
        }
        badge->setContentsMargins(6, 2, 6, 2);
        badge->setProperty("anFrameRadius", an::ui::tokens().radius.sm);
        badge->setFont(an::ui::tokens().typography.caption);
        drizzleHeaderLayout->addWidget(badge);
        drizzleHeaderLayout->addStretch();
        whyLayout->addLayout(drizzleHeaderLayout);

        QString drizzleReasonText;
        if (!analysisAvailable)
        {
            if (m_drizzleAnalysis.totalFrames <= 0)
            {
                drizzleReasonText =
                    tr("Drizzle analysis is not available yet. If registration has not been run, "
                       "run LIGHT registration to generate transforms and metrics, then reopen this recommendation.");
            }
            else if (m_drizzleAnalysis.framesWithTransforms <= 0)
            {
                if (m_drizzleAnalysis.framesWithMetrics <= 0)
                {
                    drizzleReasonText =
                        tr("Drizzle cannot be computed because registration metrics are missing for this group. "
                           "Run LIGHT registration to generate transforms, then reopen this recommendation.");
                }
                else if (m_drizzleAnalysis.framesWithUnsupportedTransforms > 0)
                {
                    drizzleReasonText =
                        tr("Drizzle found registration transforms, but this build could not analyze their transform "
                           "model. Re-run LIGHT registration to refresh the transform metrics, then reopen this "
                           "recommendation.");
                }
                else
                {
                    drizzleReasonText =
                        tr("Drizzle found registration metrics, but usable transforms are missing for this group. "
                           "Complete LIGHT registration to generate transforms, then reopen this recommendation.");
                }
            }
            else
            {
                drizzleReasonText =
                    tr("Drizzle analysis is incomplete: only %1 of %2 frames have usable registration transforms. "
                       "Complete LIGHT registration for this group, then reopen this recommendation.")
                        .arg(m_drizzleAnalysis.framesWithTransforms)
                        .arg(m_drizzleAnalysis.totalFrames);
            }
        }
        else if (!m_recommendation.drizzleReason.isEmpty())
        {
            drizzleReasonText = m_recommendation.drizzleReason;
        }
        else
        {
            drizzleReasonText = tr("Drizzle analysis completed, but no recommendation details were returned.");
        }

        auto* drizzleContentLayout = new QHBoxLayout();
        drizzleContentLayout->setSpacing(16);
        drizzleContentLayout->setContentsMargins(0, 4, 0, 0);

        if (analysisAvailable)
        {
            auto* coverageViz = new DrizzleCoverageWidget();
            coverageViz->setCoverage(m_drizzleAnalysis.subPixelCoverage,
                                     static_cast<int>(m_drizzleAnalysis.subPixelCoverage * 16), 16);
            drizzleContentLayout->addWidget(coverageViz, 0, Qt::AlignTop);
        }

        auto* drizzleTextLayout = new QVBoxLayout();
        drizzleTextLayout->setSpacing(6);

        auto* drizzleReasonLabel = new QLabel(drizzleReasonText);
        drizzleReasonLabel->setWordWrap(true);
        drizzleReasonLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        setLabelColor(drizzleReasonLabel, m_mutedTextColor);
        drizzleTextLayout->addWidget(drizzleReasonLabel);

        auto* drizzleMetricsLabel = new QLabel(tr("Usable transforms: %1 / %2")
                                                   .arg(m_drizzleAnalysis.framesWithTransforms)
                                                   .arg(m_drizzleAnalysis.totalFrames));
        setLabelFont(drizzleMetricsLabel, m_subtleTextColor, 11);
        drizzleTextLayout->addWidget(drizzleMetricsLabel);

        if (m_drizzleAnalysis.framesWithMetrics > 0)
        {
            auto* metricsRowsLabel = new QLabel(tr("Registration metrics: %1 / %2")
                                                    .arg(m_drizzleAnalysis.framesWithMetrics)
                                                    .arg(m_drizzleAnalysis.totalFrames));
            setLabelFont(metricsRowsLabel, m_subtleTextColor, 11);
            drizzleTextLayout->addWidget(metricsRowsLabel);
        }

        // Show recommended pixel fraction when drizzle is viable
        if (analysisAvailable &&
            (m_recommendation.drizzleVerdict == StackingRecommendation::DrizzleVerdict::Recommended ||
             m_recommendation.drizzleVerdict == StackingRecommendation::DrizzleVerdict::Possible))
        {
            QString pixfracNote = tr("Recommended pixel fraction: <b>%1</b>")
                                      .arg(QString::number(m_recommendation.drizzlePixfrac, 'f', 1));
            if (m_drizzleAnalysis.medianFwhmPx > 0.0)
            {
                pixfracNote +=
                    tr("  (median FWHM: %1 px)").arg(QString::number(m_drizzleAnalysis.medianFwhmPx, 'f', 2));
            }
            auto* pixfracLabel = new QLabel(pixfracNote);
            setLabelFont(pixfracLabel, m_subtleTextColor, 11);
            drizzleTextLayout->addWidget(pixfracLabel);
        }

        drizzleContentLayout->addLayout(drizzleTextLayout, 1);

        whyLayout->addLayout(drizzleContentLayout);
    }

    rationaleContainerLayout->addWidget(whyFrame);

    // -- Alternative Option Card (if available) --
    if (m_recommendation.secondary.available)
    {
        auto* altFrame = new QFrame();
        altFrame->setObjectName("altFrame");
        setCardFrame(altFrame, m_cardBg.darker(105), m_accentColor, 6, /*dashed=*/true);
        auto* altLayout = new QVBoxLayout(altFrame);
        altLayout->setContentsMargins(14, 14, 14, 14);
        altLayout->setSpacing(8);

        auto* altHeader = new QLabel(tr("ALTERNATIVE: %1").arg(m_recommendation.secondary.label.toUpper()));
        setLabelFont(altHeader, m_accentColor, 10, QFont::Bold);
        altLayout->addWidget(altHeader);

        auto* altReason = new QLabel(m_recommendation.secondary.reason);
        altReason->setWordWrap(true);
        setLabelFont(altReason, m_mutedTextColor, 12, QFont::Normal, /*italic=*/true);
        altLayout->addWidget(altReason);

        auto* altActionLayout = new QHBoxLayout();
        auto* altDetails = new QLabel(tr("Uses <b>%1</b> rejection.").arg(m_recommendation.secondary.rejectionMethod));
        setLabelFont(altDetails, m_subtleTextColor, 11);
        altActionLayout->addWidget(altDetails);

        altActionLayout->addStretch();

        auto* applyAltBtn = new QPushButton(tr("Apply Alternative"));
        applyAltBtn->setCursor(Qt::PointingHandCursor);
        applyAltBtn->setProperty("anButtonVariant", QStringLiteral("secondary"));
        applyAltBtn->setFont(an::ui::tokens().typography.body);

        StackingRecommendation secRec = m_recommendation;
        secRec.rejectionMethod = m_recommendation.secondary.rejectionMethod;
        secRec.sigmaLow = m_recommendation.secondary.sigmaLow;
        secRec.sigmaHigh = m_recommendation.secondary.sigmaHigh;
        secRec.localNormalization = m_recommendation.secondary.localNormalization;

        if (matchesRecommendation(secRec))
        {
            applyAltBtn->setEnabled(false);
            applyAltBtn->setToolTip(tr("Alternative settings are already applied."));
        }
        else
        {
            applyAltBtn->setToolTip(tr("Apply these alternative settings."));
        }

        connect(applyAltBtn, &QPushButton::clicked, this, [this]() {
            m_recommendation.rejectionMethod = m_recommendation.secondary.rejectionMethod;
            m_recommendation.sigmaLow = m_recommendation.secondary.sigmaLow;
            m_recommendation.sigmaHigh = m_recommendation.secondary.sigmaHigh;
            m_recommendation.localNormalization = m_recommendation.secondary.localNormalization;
            m_applyRequested = true;
            accept();
        });
        altActionLayout->addWidget(applyAltBtn);
        altLayout->addLayout(altActionLayout);

        rationaleContainerLayout->addWidget(altFrame);
    }

    rationaleContainerLayout->addStretch(); // Push everything to top

    rationaleScroll->setWidget(rationaleContainer);
    mainLayout->addWidget(rationaleScroll, 1);

    // --- Action Buttons ---
    auto* applyButton = new QPushButton(tr("Apply Recommended Settings"));
    applyButton->setCursor(Qt::PointingHandCursor);
    applyButton->setProperty("anButtonVariant", QStringLiteral("primary"));
    applyButton->setFont(an::ui::tokens().typography.heading3);

    if (matchesRecommendation(m_recommendation))
    {
        applyButton->setEnabled(false);
        applyButton->setToolTip(tr("All recommended settings are already applied."));
    }
    else
    {
        applyButton->setToolTip(tr("Apply the recommended settings."));
    }

    auto* applyRow = new QHBoxLayout();
    applyRow->addWidget(applyButton);
    applyRow->addStretch();
    mainLayout->addLayout(applyRow);

    auto* closeButton = new QPushButton(tr("Close"));

    auto* closeRow = new QHBoxLayout();
    closeRow->addStretch();
    closeRow->addWidget(closeButton);
    mainLayout->addLayout(closeRow);

    connect(applyButton, &QPushButton::clicked, this, [this]() {
        m_applyRequested = true;
        accept();
    });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
}

bool StackingRecommendationDialog::matchesRecommendation(const StackingRecommendation& rec) const
{
    // Check core settings
    bool matches = (m_currentSettings.stackMethod == rec.stackMethod) &&
                   (m_currentSettings.rejectionMethod == rec.rejectionMethod) &&
                   (std::abs(m_currentSettings.sigmaLow - rec.sigmaLow) < 0.01) &&
                   (std::abs(m_currentSettings.sigmaHigh - rec.sigmaHigh) < 0.01);

    // Normalization check
    if (rec.localNormalization)
    {
        matches &= (m_currentSettings.localNormalization.enabled ||
                    m_currentSettings.normalizationMode == NormalizationMode::Local);
    }
    else
    {
        QString currentNorm;
        if (m_currentSettings.normalizationMode == NormalizationMode::None)
            currentNorm = "None";
        else if (m_currentSettings.normalizationMode == NormalizationMode::Local)
            currentNorm = "Local";
        else if (m_currentSettings.globalNormalizationMethod == GlobalNormalizationMethod::Additive)
            currentNorm = "Additive";
        else if (m_currentSettings.globalNormalizationMethod == GlobalNormalizationMethod::Multiplicative)
            currentNorm = "Multiplicative";
        else if (m_currentSettings.globalNormalizationMethod == GlobalNormalizationMethod::AdditiveScaling)
            currentNorm = "Additive With Scaling";
        else if (m_currentSettings.globalNormalizationMethod == GlobalNormalizationMethod::MultiplicativeScaling)
            currentNorm = "Multiplicative With Scaling";

        matches &= (currentNorm == rec.normalizationMode);
    }

    // Weighting check (LIGHT only)
    if (m_frameType == StackingSettingsDialog::FrameType::LIGHT && !rec.weightingMethod.isEmpty())
    {
        matches &= (m_currentSettings.weighting == rec.weightingMethod);
    }

    // Drizzle check (LIGHT only)
    if (m_frameType == StackingSettingsDialog::FrameType::LIGHT)
    {
        if (rec.drizzleVerdict == StackingRecommendation::DrizzleVerdict::Recommended ||
            rec.drizzleVerdict == StackingRecommendation::DrizzleVerdict::Possible)
        {
            matches &= (m_currentSettings.drizzle.enabled &&
                        std::abs(m_currentSettings.drizzle.scale - rec.drizzleScale) < 0.01 &&
                        std::abs(m_currentSettings.drizzle.pixelFraction - rec.drizzlePixfrac) < 0.01);
        }
        else if (rec.drizzleVerdict == StackingRecommendation::DrizzleVerdict::NotRecommended)
        {
            matches &= (!m_currentSettings.drizzle.enabled);
        }
    }

    return matches;
}

void StackingRecommendationDialog::addComparisonRow(QGridLayout* layout, int row, const QString& label,
                                                    const QString& current, const QString& recommended, bool different)
{
    auto* lLabel = new QLabel(label);
    lLabel->setMinimumWidth(140);
    auto* lCurrent = new QLabel(current);
    auto* lRecommended = new QLabel(recommended);

    if (different)
    {
        setLabelFont(lRecommended, m_goodColor, -1, QFont::Bold);
        setLabelFont(lCurrent, m_subtleTextColor, -1, QFont::Normal, false, /*strikeOut=*/true);
    }
    else
    {
        setLabelColor(lRecommended, m_subtleTextColor);
    }

    layout->addWidget(lLabel, row, 0);
    layout->addWidget(lCurrent, row, 1);
    layout->addWidget(lRecommended, row, 2);
}
