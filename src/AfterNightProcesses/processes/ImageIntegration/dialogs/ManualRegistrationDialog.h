// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/model/IntegrationDataModel.h"

#include <QDialog>
#include <QPointF>

#include <memory>
#include <optional>

#include <verdigris/wobjectdefs.h>

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class ManualRegistrationPreviewController;

class AFTERNIGHTPROCESSES_API ManualRegistrationDialog : public QDialog
{
    W_OBJECT(ManualRegistrationDialog)

public:
    explicit ManualRegistrationDialog(const QString& referenceFrame, const QStringList& availableFrames,
                                      const RegistrationSettings::ManualControlPointMap& existingPoints,
                                      QWidget* parent = nullptr);
    ~ManualRegistrationDialog() override;

    [[nodiscard]] RegistrationSettings::ManualControlPointMap controlPointsByFrame() const;

private:
    [[nodiscard]] QString currentTargetFrame() const;
    void updateTargetFrameUi();
    void refreshTable();
    void refreshMarkers();
    void updateStatusLabel(const QString& message = QString());
    void addPointPair(const QPointF& sourcePoint, const QPointF& referencePoint);
    void removeSelectedPoints();
    void clearCurrentFramePoints();

    void onTargetFrameChanged();
    void onReferencePointClicked(const QPointF& point);
    void onTargetPointClicked(const QPointF& point);

    QString m_referenceFrame;
    QStringList m_availableFrames;
    RegistrationSettings::ManualControlPointMap m_controlPointsByFrame;
    std::optional<QPointF> m_pendingReferencePoint;

    QLabel* m_referenceLabel = nullptr;
    QComboBox* m_targetFrameCombo = nullptr;
    QLabel* m_statusLabel = nullptr;
    std::unique_ptr<ManualRegistrationPreviewController> m_referencePreview;
    std::unique_ptr<ManualRegistrationPreviewController> m_targetPreview;
    QTableWidget* m_pointsTable = nullptr;
    QPushButton* m_removeSelectedButton = nullptr;
    QPushButton* m_clearFrameButton = nullptr;
    QPushButton* m_clearPendingButton = nullptr;
};
