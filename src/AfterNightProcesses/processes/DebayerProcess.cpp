// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "DebayerProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/ProcessCommonUtils.h"
#include "controls/ProcessButtonIcons.h"

#include <fmt/format.h>

#include <array>

namespace
{

/// The output modes the dialog offers, in its order.
const QString kCombinedOutput = QStringLiteral("Combined RGB Color");
const QString kSeparateOutput = QStringLiteral("Separate RGB Channels");
const QString kBothOutputs = QStringLiteral("RGB Color + Separate RGB Channels");

/// The methods the dialog offers, in its order.
[[nodiscard]] const std::array<const QString*, 6u>& offeredMethods()
{
    static const std::array<const QString*, 6u> methods{
        &process_utils::DEMOSAIC_BILINEAR, &process_utils::DEMOSAIC_VNG4,  &process_utils::DEMOSAIC_AHD,
        &process_utils::DEMOSAIC_AMAZE,    &process_utils::DEMOSAIC_LMMSE, &process_utils::DEMOSAIC_RCD};
    return methods;
}

} // namespace

DebayerProcess::DebayerProcess(ImageViewControl* control) : ImageProcessControl(control, getProcessNameStatic())
{
    setFixedSizeWindow(true);
    setWindowSizeFitMultipliers(1.2, 1.0);
    m_ui = an::ui::processes::buildDebayerProcessUi(this);
    setProcessDescription(QStringLiteral(
        "Convert CFA/RAW mosaic data into RGB output using selectable demosaicing methods and output modes."));

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this, &DebayerProcess::on_dialog_box_button_clicked);

    updateWindowTitle();

    m_SupportTargetViewControlSelection = true;
    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    initialize();
    refreshTheme();
}

std::string DebayerProcess::getProcessNameStatic()
{
    return "Debayer";
}

bool DebayerProcess::requiresExclusiveAccessStatic()
{
    return false;
}

QString DebayerProcess::fromMetadataText()
{
    return QStringLiteral("From metadata");
}

DebayerProcess::Settings DebayerProcess::settings() const
{
    Settings asked;
    asked.methodLabel = m_ui.demosaicingMethod->currentText();
    asked.method = process_utils::demosaicChoiceFromLabel(asked.methodLabel).method;
    const QVariant pattern = m_ui.bayerPattern->currentData();
    asked.pattern =
        an::ui::bayerPatternChoiceFromText(pattern.isValid() ? pattern.toString() : m_ui.bayerPattern->currentText());
    const QString output = m_ui.outputMode->currentText();
    asked.output = output == kSeparateOutput ? an::ui::processes::DebayerOutput::Separate
                   : output == kBothOutputs  ? an::ui::processes::DebayerOutput::Both
                                             : an::ui::processes::DebayerOutput::Combined;
    return asked;
}

DebayerProcess::~DebayerProcess() {}

void DebayerProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();

    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

void DebayerProcess::initialize()
{
    // "From metadata" stores the text a persisted choice of the image's own filter array keeps.
    m_ui.bayerPattern->addItem(fromMetadataText(), an::ui::kBayerPatternFromImageText);
    for (const QString& stated : {an::ui::kBayerPatternRggbText, an::ui::kBayerPatternBggrText,
                                  an::ui::kBayerPatternGbrgText, an::ui::kBayerPatternGrbgText})
    {
        m_ui.bayerPattern->addItem(stated, stated);
    }
    m_ui.bayerPattern->setCurrentIndex(0);

    for (const QString* method : offeredMethods())
    {
        m_ui.demosaicingMethod->addItem(*method);
    }
    m_ui.demosaicingMethod->setCurrentText(process_utils::DEMOSAIC_RCD);

    m_ui.outputMode->addItem(kCombinedOutput);
    m_ui.outputMode->addItem(kSeparateOutput);
    m_ui.outputMode->addItem(kBothOutputs);
    m_ui.outputMode->setCurrentText(kCombinedOutput);
}

void DebayerProcess::executeImageDebayer()
{
    if (m_ExecutionInProgress || !m_image_view_control->hasImage())
    {
        return;
    }
    // A sequence its view demosaics shows colour frames, but Debayer reads the frames as recorded, and a recorded frame
    // that states no filter array is refused by name; so only an image that shows no mosaic is asked about.
    const bool demosaicedSequence =
        m_image_view_control->isSequence() && m_image_view_control->isSequenceAutodebayerEnabled();
    if (!m_image_view_control->isCFA() && !demosaicedSequence)
    {
        int ret = QMessageBox::No;
        process_utils::messageBoxYesNo(
            this, "Debayer",
            "This file doesn't have a CFA (color filter array) defined, please make sure this is a RAW CFA file before "
            "debayering.\nDo you want to run this process anyway?",
            true, &ret);

        if (ret == QMessageBox::No)
        {
            return;
        }
    }

    // The dialog is read here, on the GUI thread; the run reads only this.
    const Settings asked = settings();
    const std::string name = m_image_view_control->getUniqueName();
    const std::string method = asked.methodLabel.toStdString();
    const bool combined = asked.output != an::ui::processes::DebayerOutput::Separate;
    const bool separate = asked.output != an::ui::processes::DebayerOutput::Combined;

    an::ui::processes::NewViewsRunRequest request;
    request.words = an::ui::processes::GeometryRunWords{
        .title = QStringLiteral("Debayer"),
        .progressText = QString::fromStdString(fmt::format("Executing debayer to sequence {}", name)),
        .errorTitle = QStringLiteral("Error running Debayer"),
        .errorLead = "An error occurred when executing the Debayer process"};
    if (combined)
    {
        request.imageOutputs.push_back(
            {.name = name + " - RGB - " + method,
             .history = fmt::format("Debayer: RGB image created from RAW image '{}'", name)});
        request.sequenceOutputs.push_back(
            {.name = name + " - RGB - " + method,
             .history = fmt::format("Debayer: RGB sequence created from RAW sequence '{}'", name)});
    }
    if (separate)
    {
        for (const auto& [suffix, colour] :
             {std::pair<const char*, const char*>{"RED", "Red"}, std::pair<const char*, const char*>{"GREEN", "Green"},
              std::pair<const char*, const char*>{"BLUE", "Blue"}})
        {
            request.imageOutputs.push_back(
                {.name = name + " - " + suffix + " - " + method,
                 .history = fmt::format("Debayer: {} channel extracted from RAW image '{}'", colour, name)});
            request.sequenceOutputs.push_back(
                {.name = name + " - " + suffix,
                 .history = fmt::format("{} channel extracted from sequence '{}'", colour, name)});
        }
    }
    request.operation = [asked](const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source) {
        return an::ui::processes::debayerImage(scope, source, asked.method, asked.pattern, asked.output);
    };
    // A sequence's frames are read as recorded: the dialog's method demosaics them, not the view's.
    request.readDemosaiced = false;
    request.showDemosaiced = false;
    request.inFlight = beginRunInFlight();
    request.admissionHook = admissionHook();

    m_ExecutionInProgress = true;
    // The run's finish reads the view the selector names, so the target is held until the run lands.
    enableProcessControls(false);
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(false);

    LOG_INFO("Executing process '{}' on {} '{}'... ", getProcessName(),
             m_image_view_control->isSequence() ? "sequence" : "image", name);

    QPointer<DebayerProcess> guard(this);
    an::ui::processes::runToNewViews(*m_image_view_control, std::move(request),
                                     [guard](const an::ui::processes::GeometryRunOutcome& outcome) {
                                         if (guard)
                                         {
                                             guard->finishRun(outcome);
                                         }
                                     });
}

void DebayerProcess::finishRun(const an::ui::processes::GeometryRunOutcome& outcome)
{
    m_ExecutionInProgress = false;
    if (outcome.status.ok() && outcome.published)
    {
        LOG_INFO("Process '{}' executed successfully on '{}'", getProcessName(),
                 m_image_view_control != nullptr ? m_image_view_control->getUniqueName() : std::string());
    }
    if ((outcome.status.ok() && outcome.published) || m_ProcessCloseRequested)
    {
        close();
        return;
    }
    enableProcessControls(true);
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(true);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(true);
}

void DebayerProcess::on_dialog_box_button_clicked(QAbstractButton* button)
{
    QDialogButtonBox* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
        return;

    QDialogButtonBox::StandardButton sb = m_ui.buttonBox->standardButton(button);

    if (sb == QDialogButtonBox::Ok)
    {
        executeImageDebayer();
    }
    else if (sb == QDialogButtonBox::Close)
    {
        this->close();
    }
}
