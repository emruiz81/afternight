// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// How a person moves between the pages of Image Integration: the seam between its pages and what shows them.
///
/// Image Integration has eight pages - the four frame sources and calibration, registration, stacking and the
/// summary - held by a list of the job's stages with the stage's page beside it (`StageListNavigator`,
/// `widgets/StageListNavigator.h`). A page, and anything a page holds, reaches what moves between the pages through
/// this seam alone (`of`): it asks for a page, for the page shown and to show another, and knows nothing of the list.

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/ui_models/IntegrationStageStates.h"

#include <QObject>

#include <verdigris/wobjectdefs.h>

class QWidget;

W_REGISTER_ARGTYPE(ImageIntegration::StagePage)

namespace ImageIntegration
{

/// What moves between the pages of an Image Integration. A child of the process's widget; used on the GUI thread.
class AFTERNIGHTPROCESSES_API StageNavigator : public QObject
{
    W_OBJECT(StageNavigator)

public:
    explicit StageNavigator(QObject* parent = nullptr);
    ~StageNavigator() override;

    StageNavigator(const StageNavigator&) = delete;
    StageNavigator& operator=(const StageNavigator&) = delete;

    /// The navigator of the Image Integration a widget is part of - a page, or anything a page holds; null for a
    /// widget of no Image Integration.
    [[nodiscard]] static StageNavigator* of(const QWidget* part);

    /// What holds the pages and what moves between them.
    [[nodiscard]] virtual QWidget* widget() const = 0;

    /// A page: the same widget whatever shows it.
    [[nodiscard]] virtual QWidget* page(StagePage page) const = 0;

    [[nodiscard]] virtual StagePage currentPage() const = 0;

    /// Shows a page, and tells it (`currentPageChanged`) when another was shown.
    virtual void setCurrentPage(StagePage page) = 0;

    /// Draws again what follows the look.
    virtual void refreshTheme() = 0;

    /// Told when another page is shown.
    void currentPageChanged(ImageIntegration::StagePage page) W_SIGNAL(currentPageChanged, page)
};

} // namespace ImageIntegration
