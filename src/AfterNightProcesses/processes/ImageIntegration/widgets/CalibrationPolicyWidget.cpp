// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "CalibrationPolicyWidget.h"

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/CalibrationMatcher.h"
#include "processes/ImageIntegration/model/UndoCommands.h"
#include "theme/AnDesignTokens.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QUndoStack>
#include <QVBoxLayout>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(ImageIntegration::CalibrationPolicyWidget)

namespace ImageIntegration
{

class CalibrationPolicyWidget::Impl
{
public:
    CalibrationPolicyWidget* q;
    IntegrationDataModel* model;
    QUndoStack* undoStack = nullptr;

    QCheckBox* chkFlatFallback = nullptr;
    QSpinBox* spinFlatDays = nullptr;
    QCheckBox* chkPreferNewer = nullptr;
    QCheckBox* chkDarkFallback = nullptr;
    QSpinBox* spinDarkTolerance = nullptr;
    QCheckBox* chkWarnOnFallback = nullptr;
    QCheckBox* chkSubtractBias = nullptr;

    explicit Impl(CalibrationPolicyWidget* parent, IntegrationDataModel* m, QUndoStack* undo)
        : q(parent), model(m), undoStack(undo)
    {
        auto* layout = new QVBoxLayout(q);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);

        auto* title = new QLabel(tr("Matching Policy"), q);
        title->setObjectName(QStringLiteral("lblPolicyTitle"));
        title->setFont(an::ui::tokens().typography.heading3);
        layout->addWidget(title);

        chkFlatFallback = new QCheckBox(tr("Allow flat session fallback"), q);
        chkFlatFallback->setObjectName(QStringLiteral("chkAllowFlatFallback"));
        chkFlatFallback->setToolTip(tr("When an exact FLAT match is not available, allow AfterNight to reuse a flat "
                                       "master from a nearby session.\n"
                                       "This can be helpful if you don’t shoot flats every night.\n\n"
                                       "Turn this off if you want flats to match the same session exactly (or you "
                                       "prefer to set manual overrides)."));
        layout->addWidget(chkFlatFallback);

        auto* flatForm = new QFormLayout();
        flatForm->setLabelAlignment(Qt::AlignLeft);
        flatForm->setFormAlignment(Qt::AlignLeft);
        spinFlatDays = new QSpinBox(q);
        spinFlatDays->setRange(0, 365);
        spinFlatDays->setSuffix(tr(" days"));
        spinFlatDays->setObjectName(QStringLiteral("spinFlatDays"));
        spinFlatDays->setToolTip(tr("How far (in days) AfterNight may search for a fallback FLAT session.\n\n"
                                    "Examples:\n"
                                    "  • 0 days: only the same session date\n"
                                    "  • 7 days: within one week\n"
                                    "  • 30 days: within about a month\n\n"
                                    "Smaller values reduce the risk of using outdated flats."));
        flatForm->addRow(tr("Max session gap"), spinFlatDays);

        chkPreferNewer = new QCheckBox(tr("Prefer newer flats"), q);
        chkPreferNewer->setObjectName(QStringLiteral("chkPreferNewerFlats"));
        chkPreferNewer->setToolTip(tr("Only used when multiple fallback FLAT sessions are equally close.\n\n"
                                      "Checked: prefer the newer session.\n"
                                      "Unchecked: prefer the older session.\n\n"
                                      "This does not affect exact matches; it only breaks ties."));
        flatForm->addRow(QString(), chkPreferNewer);

        layout->addLayout(flatForm);

        chkDarkFallback = new QCheckBox(tr("Allow dark exposure fallback"), q);
        chkDarkFallback->setObjectName(QStringLiteral("chkAllowDarkFallback"));
        chkDarkFallback->setToolTip(
            tr("When an exact DARK exposure match is not available, allow AfterNight to use the closest DARK exposure "
               "within the tolerance below.\n\n"
               "This can be useful if your dark library doesn’t contain every exact exposure you shoot."));
        layout->addWidget(chkDarkFallback);

        auto* darkForm = new QFormLayout();
        darkForm->setLabelAlignment(Qt::AlignLeft);
        darkForm->setFormAlignment(Qt::AlignLeft);
        spinDarkTolerance = new QSpinBox(q);
        spinDarkTolerance->setRange(0, 100);
        spinDarkTolerance->setSuffix(tr(" %"));
        spinDarkTolerance->setObjectName(QStringLiteral("spinDarkTolerance"));
        spinDarkTolerance->setToolTip(
            tr("Maximum allowed exposure difference for fallback DARK matching, expressed as a percentage of the LIGHT "
               "exposure.\n\n"
               "Example: for 180s lights and 20% tolerance, AfterNight may pick darks from 144s to 216s.\n\n"
               "Tip: 0% means “exact exposure only” (even if fallback is enabled)."));
        darkForm->addRow(tr("Exposure tolerance"), spinDarkTolerance);
        layout->addLayout(darkForm);

        chkWarnOnFallback = new QCheckBox(tr("Warn on fallback matches"), q);
        chkWarnOnFallback->setObjectName(QStringLiteral("chkWarnOnFallback"));
        chkWarnOnFallback->setToolTip(
            tr("When enabled, AfterNight will surface warnings when it had to fall back to a non-exact match (yellow "
               "cells / Issues panel).\n\n"
               "Turning this off makes the UI quieter, but it does not change which calibration masters are chosen."));
        layout->addWidget(chkWarnOnFallback);

        chkSubtractBias = new QCheckBox(tr("Subtract bias from dark (CCD mode)"), q);
        chkSubtractBias->setObjectName(QStringLiteral("chkSubtractBias"));
        chkSubtractBias->setToolTip(
            tr("CCD-style calibration: treat the dark correction as (Dark − Bias).\n\n"
               "When enabled, AfterNight expects BOTH a Bias match and a Dark match for each light group. Missing "
               "either will produce warnings.\n\n"
               "Leave this off for typical CMOS workflows where darks already include the bias/offset signal."));
        layout->addWidget(chkSubtractBias);

        layout->addStretch(1);

        connectSignals();
        q->setObjectName(QStringLiteral("calibrationPolicyWidget"));
    }

    void connectSignals()
    {
        QObject::connect(chkFlatFallback, &QCheckBox::toggled, q, [this]() { applyPolicy(); });
        QObject::connect(spinFlatDays, QOverload<int>::of(&QSpinBox::valueChanged), q, [this]() { applyPolicy(); });
        QObject::connect(chkPreferNewer, &QCheckBox::toggled, q, [this]() { applyPolicy(); });
        QObject::connect(chkDarkFallback, &QCheckBox::toggled, q, [this]() { applyPolicy(); });
        QObject::connect(spinDarkTolerance, QOverload<int>::of(&QSpinBox::valueChanged), q,
                         [this]() { applyPolicy(); });
        QObject::connect(chkWarnOnFallback, &QCheckBox::toggled, q, [this]() { applyPolicy(); });
        QObject::connect(chkSubtractBias, &QCheckBox::toggled, q, [this]() { applyPolicy(); });
    }

    void updateEnabledState()
    {
        const bool flatEnabled = chkFlatFallback->isChecked();
        spinFlatDays->setEnabled(flatEnabled);
        chkPreferNewer->setEnabled(flatEnabled);

        const bool darkEnabled = chkDarkFallback->isChecked();
        spinDarkTolerance->setEnabled(darkEnabled);
    }

    void applyPolicy()
    {
        if (!model)
        {
            return;
        }

        CalibrationMatchingPolicy policy = model->calibrationMatchingPolicy();
        const CalibrationMatchingPolicy oldPolicy = policy;
        policy.allowFlatSessionFallback = chkFlatFallback->isChecked();
        policy.maxFlatSessionDaysApart = spinFlatDays->value();
        policy.preferNewerFlat = chkPreferNewer->isChecked();
        policy.allowDarkExposureFallback = chkDarkFallback->isChecked();
        policy.darkExposureTolerancePercent = spinDarkTolerance->value();
        policy.warnOnFallback = chkWarnOnFallback->isChecked();
        policy.subtractBiasFromDark = chkSubtractBias->isChecked();

        updateEnabledState();
        if (undoStack)
        {
            undoStack->push(new SetCalibrationMatchingPolicyCommand(model, oldPolicy, policy));
        }
        else
        {
            model->setCalibrationMatchingPolicy(policy);
        }
    }

    void updateFromModel()
    {
        if (!model)
        {
            return;
        }

        const CalibrationMatchingPolicy policy = model->calibrationMatchingPolicy();

        const QSignalBlocker blockFlatFallback(chkFlatFallback);
        const QSignalBlocker blockFlatDays(spinFlatDays);
        const QSignalBlocker blockPreferNewer(chkPreferNewer);
        const QSignalBlocker blockDarkFallback(chkDarkFallback);
        const QSignalBlocker blockDarkTol(spinDarkTolerance);
        const QSignalBlocker blockWarn(chkWarnOnFallback);
        const QSignalBlocker blockSubtract(chkSubtractBias);

        chkFlatFallback->setChecked(policy.allowFlatSessionFallback);
        spinFlatDays->setValue(policy.maxFlatSessionDaysApart);
        chkPreferNewer->setChecked(policy.preferNewerFlat);
        chkDarkFallback->setChecked(policy.allowDarkExposureFallback);
        spinDarkTolerance->setValue(policy.darkExposureTolerancePercent);
        chkWarnOnFallback->setChecked(policy.warnOnFallback);
        chkSubtractBias->setChecked(policy.subtractBiasFromDark);

        updateEnabledState();
    }
};

CalibrationPolicyWidget::CalibrationPolicyWidget(IntegrationDataModel* model, QWidget* parent, QUndoStack* undoStack)
    : QWidget(parent), m_impl(std::make_unique<Impl>(this, model, undoStack))
{
}

CalibrationPolicyWidget::~CalibrationPolicyWidget() = default;

void CalibrationPolicyWidget::refresh()
{
    m_impl->updateFromModel();
}

} // namespace ImageIntegration
