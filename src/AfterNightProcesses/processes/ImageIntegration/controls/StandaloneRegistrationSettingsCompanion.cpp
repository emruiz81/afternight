// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The standalone registration's way to the integration's registration settings dialog, and the integration's local
/// normalization settings dialog the standalone stacking opens: each shown over the window that asks, never run, and
/// answering through its callback when the person accepts it.

#include "stdafx.hpp"

#include "processes/ImageIntegration/controls/LocalNormalizationSettingsDialog.h"
#include "processes/ImageIntegration/controls/RegistrationSettingsDialog.h"
#include "processes/core/WorkflowToolCompanions.h"

#include <QPointer>
#include <QPushButton>

#include <utility>

namespace an::ui::processes
{

void openStandaloneRegistrationSettings(QWidget* parent, StandaloneRegistrationSettingsRequest request,
                                        std::function<void(const RegistrationSettings&)> accepted)
{
    auto* dialog = new RegistrationSettingsDialog(QStringLiteral("Standalone Registration"), request.frames, parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->removeRegistrationMethod(QStringLiteral("manual_points"));
    // The standalone registration solves a frame only when Auto-solve asks it to, Mosaic or not.
    dialog->setMosaicSolvesUnregisteredPanels(false);
    dialog->setRegistrationSettings(request.settings);

    // Auto-Assign reads the standalone registration's own database and detections rather than the integration's
    // cache: the window answers it on a worker and the dialog takes the reference it chose.
    if (dialog->m_ui.autoAssignButton != nullptr && request.autoAssign)
    {
        dialog->m_ui.autoAssignButton->setVisible(true);
        QObject::disconnect(dialog->m_ui.autoAssignButton, nullptr, dialog, nullptr);
        const QPointer<RegistrationSettingsDialog> guard(dialog);
        QObject::connect(dialog->m_ui.autoAssignButton, &QPushButton::clicked, dialog,
                         [guard, autoAssign = request.autoAssign]() {
                             if (!guard)
                             {
                                 return;
                             }
                             guard->m_ui.autoAssignButton->setEnabled(false);
                             autoAssign(guard->getRegistrationSettings(), [guard](QString reference, QString reason) {
                                 if (!guard)
                                 {
                                     return;
                                 }
                                 guard->m_ui.autoAssignButton->setEnabled(true);
                                 if (!reference.isEmpty())
                                 {
                                     guard->setReferenceImage(reference);
                                 }
                                 Q_UNUSED(reason);
                             });
                         });
    }

    QObject::connect(dialog, &QDialog::accepted, dialog, [dialog, accepted = std::move(accepted)]() {
        if (accepted)
        {
            accepted(dialog->getRegistrationSettings());
        }
    });
    dialog->open();
}

void openLocalNormalizationSettings(QWidget* parent, const LocalNormalizationSettings& settings,
                                    std::function<void(const LocalNormalizationSettings&)> accepted)
{
    auto* dialog = new LocalNormalizationSettingsDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    // The standalone stack fits each frame against its reference frame and reads none of the integration's analysis.
    dialog->setReferenceAnalysisOffered(
        false,
        QCoreApplication::translate("StandaloneLocalNormalization",
                                    "This stack fits each frame's local surfaces and scale against its reference "
                                    "frame: the reference strategy, the evaluation, the PSF flux scale and the "
                                    "hot-pixel and noise filters do not apply, so they are not offered."));
    dialog->setSettings(settings);
    QObject::connect(dialog, &QDialog::accepted, dialog, [dialog, accepted = std::move(accepted)]() {
        if (accepted)
        {
            accepted(dialog->getSettings());
        }
    });
    dialog->open();
}

} // namespace an::ui::processes
