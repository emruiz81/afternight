// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file StackingTab.h
 * @brief Stacking configuration tab for Image Integration.
 *
 * Provides UI for configuring pixel rejection, normalization,
 * and integration algorithms used during the stacking phase.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include <QWidget>
#include <Qt>
#include <verdigris/wobjectdefs.h>

class IntegrationDataModel;
class QEvent;
class QUndoStack;

/**
 * @class StackingTab
 * @brief Tab widget for configuring the stacking algorithm.
 *
 * Allows users to configure:
 * - Pixel rejection algorithm (sigma clip, winsorized, linear fit, etc.)
 * - Normalization method (additive, multiplicative, etc.)
 * - Weighting scheme (equal, noise, FWHM, etc.)
 * - Output format and bit depth
 * - Memory usage limits
 *
 * Uses pimpl idiom for ABI stability and fast compilation.
 */
class AFTERNIGHTPROCESSES_API StackingTab : public QWidget
{
    W_OBJECT(StackingTab)

public:
    explicit StackingTab(IntegrationDataModel* model, QWidget* parent = nullptr, QUndoStack* undoStack = nullptr);
    ~StackingTab() override;

    // Sortable integration groups table.
    void sortIntegrationGroupsByColumn(int column, Qt::SortOrder order);
    [[nodiscard]] int integrationGroupsSortIndicatorColumn() const;
    [[nodiscard]] Qt::SortOrder integrationGroupsSortIndicatorOrder() const;

    [[nodiscard]] bool isReprocessExistingEnabled() const;
    [[nodiscard]] bool isOpenIntegratedFilesEnabled() const;
    [[nodiscard]] bool isCombineRgbImagesEnabled() const;
    [[nodiscard]] int maxParallelCores() const;

public slots:
    void refreshAll();
    void showFrameInTable(const QString& framePath);

signals:
    void runIntegrationRequested(const QString& integrationGroupKey)
        W_SIGNAL(runIntegrationRequested, integrationGroupKey);

    void runIntegrationAllRequested() W_SIGNAL(runIntegrationAllRequested);

private:
    void changeEvent(QEvent* event) override;

    class Impl;
    std::unique_ptr<Impl> m_impl;
};
