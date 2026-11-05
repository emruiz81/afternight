// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "SCNRProcess.h"
#include "ProcessCommonUtils.h"
#include "controls/ProcessButtonIcons.h"
#include "controls/ImageViewControl.h"

#include <fmt/format.h>

SCNRProcess::SCNRProcess(ImageViewControl* control) : ImageProcessControl(control, getProcessNameStatic())
{
    setFixedSizeWindow(true);

    m_ui = an::ui::processes::buildSCNRProcessUi(this);
    setProcessDescription(QStringLiteral("Suppress green color casts using selectable SCNR protection models."));

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this, &SCNRProcess::on_dialog_box_button_clicked);
    QObject::connect(m_ui.protectionType, &QComboBox::currentTextChanged, this,
                     &SCNRProcess::on_protectionType_currentTextChanged);

    INIT_D_SPINBOX_CTRL(amountControl, m_ui.amountValue, m_ui.amountSlider, &SCNRProcess::on_amountChanged);

    updateWindowTitle();

    m_SupportTargetViewControlSelection = true;
    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    initialize();

    refreshTheme();
}

std::string SCNRProcess::getProcessNameStatic()
{
    return "SCNR";
}

bool SCNRProcess::requiresExclusiveAccessStatic()
{
    return false;
}

void SCNRProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

SCNRProcess::~SCNRProcess() {}

void SCNRProcess::on_protectionType_currentTextChanged(const QString& text)
{
    // Only the two mask methods mix green back in proportion to the amount.
    const std::optional<an::ChromaticNoiseMethod> method = an::ui::processes::scnrMethodFromLabel(text);
    amountControl->setEnabled(method == an::ChromaticNoiseMethod::MaximumMask ||
                              method == an::ChromaticNoiseMethod::AdditiveMask);
}

void SCNRProcess::on_amountChanged(double) {}

void SCNRProcess::initialize()
{
    for (const QString& label : an::ui::processes::scnrProtectionLabels())
    {
        m_ui.protectionType->addItem(label);
    }

    m_ui.protectionType->setCurrentText(QStringLiteral("Average Neutral"));

    amountControl->setValue(1.0);
    amountControl->setEnabled(false);
}

SCNRProcess::Settings SCNRProcess::settings() const
{
    Settings asked;
    asked.protection = m_ui.protectionType->currentText();
    asked.suppression.method =
        an::ui::processes::scnrMethodFromLabel(asked.protection).value_or(an::ChromaticNoiseMethod::AverageNeutral);
    asked.suppression.amount = amountControl->getValue();
    asked.suppression.preserveLightness = m_ui.preserveLightness->isChecked();
    return asked;
}

std::string SCNRProcess::stepDescription(const Settings& settings)
{
    return fmt::format("Protection Method {} preserve lightness {}", settings.protection.toStdString(),
                       settings.suppression.preserveLightness);
}

void SCNRProcess::executeSCNR()
{
    if (m_ExecutionInProgress || m_image_view_control == nullptr || !m_image_view_control->hasImage())
    {
        return;
    }
    // The dialog and the view's mask are read here, on the GUI thread; the run reads only these.
    const Settings asked = settings();
    const an::ui::processes::ViewMaskSnapshot mask = an::ui::processes::viewMaskOf(*m_image_view_control);

    m_ExecutionInProgress = true;
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(false);

    LOG_INFO("Executing process '{}' on {} '{}'... ", getProcessName(),
             m_image_view_control->isSequence() ? "sequence" : "image", m_image_view_control->getUniqueName());

    an::ui::processes::InPlaceRunRequest request;
    request.words = an::ui::processes::GeometryRunWords{
        .title = QStringLiteral("SCNR"),
        .progressText =
            QString::fromStdString(fmt::format("Executing SCNR to sequence {}", m_image_view_control->getUniqueName())),
        .errorTitle = QStringLiteral("Error running SCNR"),
        .errorLead = "An error occurred when executing the SCNR process"};
    request.stepName = getProcessName();
    request.stepDescription = stepDescription(asked);
    request.operation = [suppression = asked.suppression, mask](const an::ui::ExecutionScope& scope,
                                                                const an::ui::ProcessSource& source) {
        return an::ui::processes::suppressGreen(scope, source, suppression, mask);
    };
    // The mask is another view's image, leased before the run is admitted.
    request.reads = {mask.asset};
    request.inFlight = beginRunInFlight();
    request.admissionHook = admissionHook();
    QPointer<SCNRProcess> guard(this);
    an::ui::processes::runInPlace(*m_image_view_control, std::move(request),
                                  [guard](const an::ui::processes::GeometryRunOutcome& outcome) {
                                      if (guard)
                                      {
                                          guard->finishRun(outcome);
                                      }
                                  });
}

void SCNRProcess::finishRun(const an::ui::processes::GeometryRunOutcome& outcome)
{
    m_ExecutionInProgress = false;
    if (outcome.published)
    {
        LOG_INFO("Process '{}' executed successfully on '{}'", getProcessName(),
                 m_image_view_control != nullptr ? m_image_view_control->getUniqueName() : std::string());
    }
    // The window stays open for another run unless it was asked to close.
    if (m_ProcessCloseRequested)
    {
        close();
        return;
    }
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(true);
}

bool SCNRProcess::isViewControlSupported(const ImageViewControl* ctrl)
{
    return an::ui::processes::showsRgbImage(ctrl);
}

void SCNRProcess::on_dialog_box_button_clicked(QAbstractButton* button)
{
    QDialogButtonBox* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
        return;

    QDialogButtonBox::StandardButton sb = buttonBox->standardButton(button);

    if (sb == QDialogButtonBox::Apply)
    {
        executeSCNR();
    }
    else if (sb == QDialogButtonBox::Close)
    {
        this->close();
    }
}
