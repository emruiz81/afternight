// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "CalibrationQuickActionsWidget.h"

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/CalibrationMatcher.h"
#include "processes/ImageIntegration/model/UndoCommands.h"
#include "theme/AnDesignTokens.h"

#include <QLabel>
#include <QPushButton>
#include <QUndoStack>
#include <QVBoxLayout>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(ImageIntegration::CalibrationQuickActionsWidget)

namespace ImageIntegration
{

class CalibrationQuickActionsWidget::Impl
{
public:
    CalibrationQuickActionsWidget* q;
    IntegrationDataModel* model;
    QUndoStack* undoStack = nullptr;

    QPushButton* btnClearOverrides = nullptr;
    QPushButton* btnResetPolicy = nullptr;
    QPushButton* btnDisableFallbacks = nullptr;

    explicit Impl(CalibrationQuickActionsWidget* parent, IntegrationDataModel* m, QUndoStack* undo)
        : q(parent), model(m), undoStack(undo)
    {
        auto* layout = new QVBoxLayout(q);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);

        auto* title = new QLabel(tr("Quick Actions"), q);
        title->setObjectName(QStringLiteral("lblQuickActionsTitle"));
        title->setFont(an::ui::tokens().typography.heading3);
        layout->addWidget(title);

        btnClearOverrides = new QPushButton(tr("Clear All Overrides"), q);
        btnClearOverrides->setObjectName(QStringLiteral("btnClearOverrides"));
        btnClearOverrides->setToolTip(
            tr("Removes all manual calibration overrides you set for light groups (Bias/Dark/Flat).\n\n"
               "This does not delete any files on disk — it only clears the override choices so matching can run "
               "normally again."));
        layout->addWidget(btnClearOverrides);

        btnResetPolicy = new QPushButton(tr("Reset Matching Policy"), q);
        btnResetPolicy->setObjectName(QStringLiteral("btnResetPolicy"));
        btnResetPolicy->setToolTip(
            tr("Resets the matching policy options back to their defaults (fallback rules, tolerances, warnings, CCD "
               "mode).\n\n"
               "This does not change your loaded frames or overrides — it only resets the policy settings."));
        layout->addWidget(btnResetPolicy);

        btnDisableFallbacks = new QPushButton(tr("Disable All Fallbacks"), q);
        btnDisableFallbacks->setObjectName(QStringLiteral("btnDisableFallbacks"));
        btnDisableFallbacks->setToolTip(
            tr("Turns off all fallback matching in one click:\n"
               "  • Flat session fallback\n"
               "  • Dark exposure fallback\n"
               "  • Fallback warnings\n\n"
               "Use this if you want strict, exact matching only. (You can still use manual overrides.)"));
        layout->addWidget(btnDisableFallbacks);

        layout->addStretch(1);

        QObject::connect(btnClearOverrides, &QPushButton::clicked, q, [this]() {
            if (model)
            {
                if (undoStack)
                {
                    undoStack->push(new ClearAllCalibrationOverridesCommand(model, model->calibrationOverrides()));
                }
                else
                {
                    model->clearAllCalibrationOverrides();
                }
            }
        });

        QObject::connect(btnResetPolicy, &QPushButton::clicked, q, [this]() {
            if (model)
            {
                const CalibrationMatchingPolicy oldPolicy = model->calibrationMatchingPolicy();
                if (undoStack)
                {
                    undoStack->push(
                        new SetCalibrationMatchingPolicyCommand(model, oldPolicy, CalibrationMatchingPolicy{}));
                }
                else
                {
                    model->setCalibrationMatchingPolicy(CalibrationMatchingPolicy{});
                }
            }
        });

        QObject::connect(btnDisableFallbacks, &QPushButton::clicked, q, [this]() {
            if (!model)
            {
                return;
            }
            const CalibrationMatchingPolicy oldPolicy = model->calibrationMatchingPolicy();
            CalibrationMatchingPolicy policy = oldPolicy;
            policy.allowFlatSessionFallback = false;
            policy.allowDarkExposureFallback = false;
            policy.warnOnFallback = false;
            if (oldPolicy.allowFlatSessionFallback == policy.allowFlatSessionFallback &&
                oldPolicy.allowDarkExposureFallback == policy.allowDarkExposureFallback &&
                oldPolicy.warnOnFallback == policy.warnOnFallback)
            {
                return;
            }
            if (undoStack)
            {
                undoStack->push(new SetCalibrationMatchingPolicyCommand(model, oldPolicy, policy));
            }
            else
            {
                model->setCalibrationMatchingPolicy(policy);
            }
        });

        q->setObjectName(QStringLiteral("calibrationQuickActionsWidget"));
    }

    void updateButtonState()
    {
        if (!model)
        {
            return;
        }

        btnClearOverrides->setEnabled(!model->calibrationOverrides().empty());
    }
};

CalibrationQuickActionsWidget::CalibrationQuickActionsWidget(IntegrationDataModel* model, QWidget* parent,
                                                             QUndoStack* undoStack)
    : QWidget(parent), m_impl(std::make_unique<Impl>(this, model, undoStack))
{
}

CalibrationQuickActionsWidget::~CalibrationQuickActionsWidget() = default;

void CalibrationQuickActionsWidget::refresh()
{
    m_impl->updateButtonState();
}

} // namespace ImageIntegration
