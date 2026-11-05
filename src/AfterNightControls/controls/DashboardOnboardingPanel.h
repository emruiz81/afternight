// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

/// @file DashboardOnboardingPanel.h
/// Empty-state onboarding panel for the main dashboard.
///
/// Presents a visually rich first-run experience when no recent files exist.
/// Features a gradient "starfield" background (Option A), icon-rich feature
/// cards (Option B), interactive quick-action cards (Option C), a visual
/// drag-and-drop zone (Option D).

#include <QIcon>
#include <QPixmap>
#include <QWidget>
#include <verdigris/wobjectdefs.h>

#include <memory>

class QResizeEvent;

namespace an::ui
{

class AFTERNIGHTCONTROLS_API DashboardOnboardingPanel : public QWidget
{
    W_OBJECT(DashboardOnboardingPanel)

public:
    explicit DashboardOnboardingPanel(qreal dpiScale, QWidget* parent = nullptr);
    ~DashboardOnboardingPanel() override;

    /// Refresh all colors, fonts, and painted visuals after a theme change.
    void refreshTheme();

    /// Set the icons displayed on the three quick-action cards.
    void setActionIcons(const QIcon& openFile, const QIcon& newSequence, const QIcon& integration);

    /// Set the icon displayed on each feature card (6 icons, by index 0–5).
    void setFeatureIcon(int index, const QIcon& icon);

    /// Set the icon displayed in the drop zone.
    void setDropZoneIcon(const QIcon& icon);

    /// Notify the panel that a drag operation entered/left the dashboard so
    /// the drop zone can visually highlight itself.
    void setDragHighlighted(bool highlighted);

    // ── Signals ─────────────────────────────────────────────────────
    void openFileRequested() W_SIGNAL(openFileRequested);
    void newSequenceRequested() W_SIGNAL(newSequenceRequested);
    void imageIntegrationRequested() W_SIGNAL(imageIntegrationRequested);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildLayout();
    void buildLogoSection(class QVBoxLayout* parent);
    void buildTitleSection(class QVBoxLayout* parent);
    void buildFeatureCardsSection(class QVBoxLayout* parent);
    void buildActionCardsSection(class QVBoxLayout* parent);
    void buildDropZoneSection(class QVBoxLayout* parent);
    void buildKeyboardHintsSection(class QVBoxLayout* parent);
    void updateFeatureCardLayout();

    void paintShadow(class QPainter& painter, const QRectF& bounds, qreal radius) const;
    void paintBackgroundImage(class QPainter& painter, const QRectF& bounds) const;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui
