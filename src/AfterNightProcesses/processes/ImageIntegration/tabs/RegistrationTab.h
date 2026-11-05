// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file RegistrationTab.h
 * @brief Star registration workflow tab for Image Integration.
 *
 * Provides UI for configuring star detection and registration parameters
 * before the alignment phase of integration.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include <QWidget>
#include <QString>
#include <Qt>
#include <verdigris/wobjectdefs.h>

class IntegrationDataModel;
class QEvent;
class QUndoStack;

/**
 * @class RegistrationTab
 * @brief Tab widget for star registration configuration.
 *
 * Allows users to configure:
 * - Star detection sensitivity and thresholds
 * - Reference frame selection (auto or manual)
 * - Registration algorithm parameters
 * - Sub-pixel alignment options
 *
 * Uses pimpl idiom for ABI stability and fast compilation.
 */
class AFTERNIGHTPROCESSES_API RegistrationTab : public QWidget
{
    W_OBJECT(RegistrationTab)

public:
    explicit RegistrationTab(IntegrationDataModel* model, QWidget* parent = nullptr, QUndoStack* undoStack = nullptr);
    ~RegistrationTab() override;

    // Sortable frame quality table.
    void sortFrameQualityByColumn(int column, Qt::SortOrder order);
    [[nodiscard]] int frameQualitySortIndicatorColumn() const;
    [[nodiscard]] Qt::SortOrder frameQualitySortIndicatorOrder() const;

public slots:
    void refreshAll();
    void showFrameInTable(const QString& framePath);

signals:
    void runRegistrationRequested(const QString& registrationGroupKey)
        W_SIGNAL(runRegistrationRequested, registrationGroupKey);

    void runRegistrationAllRequested() W_SIGNAL(runRegistrationAllRequested);

    void previewFrameRequested() W_SIGNAL(previewFrameRequested);

private:
    void changeEvent(QEvent* event) override;

    class Impl;
    std::unique_ptr<Impl> m_impl;
};
