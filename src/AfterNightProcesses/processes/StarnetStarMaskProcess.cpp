// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "StarnetStarMaskProcess.h"
#include "ProcessCommonUtils.h"
#include "controls/ProcessButtonIcons.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/settings/SettingsKeys.h"
#include "controls/settings/StarNetCliTools.h"

#include <AfterNightPlatform/Logging.h>

#include <QPointer>

#include <fmt/format.h>

#include <utility>
#include <vector>

namespace
{

inline constexpr auto kStridePresetStandard = "standard";
inline constexpr auto kStridePresetWideField = "wide-field";
inline constexpr auto kStridePresetCustom = "custom";
inline constexpr auto kStarnetWeightsFolderContext = "__STARNET_WEIGHTS_FOLDER__";

} // namespace

StarnetStarMaskProcess::StarnetStarMaskProcess(ImageViewControl* control)
    : ImageProcessControl(control, getProcessNameStatic())
{
    setFixedSizeWindow(true);
    setWindowSizeFitMultipliers(1.3, 1.0);

    m_ui = an::ui::processes::buildStarnetStarMaskProcessUi(this);
    setProcessDescription(
        QStringLiteral("Generate native StarNet2 star masks with mask shaping and optional binary thresholding."));

    m_ui.groupBox->setFlat(true);
    m_ui.groupBox_2->setFlat(true);

    this->setAttribute(Qt::WA_DeleteOnClose);

    m_ui.stridePreset->addItem(tr("Standard (256)"), QString::fromLatin1(kStridePresetStandard));
    m_ui.stridePreset->addItem(tr("Wide-field (384)"), QString::fromLatin1(kStridePresetWideField));
    m_ui.stridePreset->addItem(tr("Custom"), QString::fromLatin1(kStridePresetCustom));
    m_ui.stridePreset->setCurrentIndex(0);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &StarnetStarMaskProcess::on_dialog_box_button_clicked);
    QObject::connect(m_ui.customStrideValue, &QSpinBox::valueChanged, this,
                     &StarnetStarMaskProcess::on_customStrideValueChanged);
    QObject::connect(m_ui.stridePreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     &StarnetStarMaskProcess::on_stridePresetChanged);
    QObject::connect(m_ui.lightness, &QCheckBox::checkStateChanged, this, &StarnetStarMaskProcess::on_lightnessClicked);
    QObject::connect(m_ui.binaryMask, &QCheckBox::checkStateChanged, this,
                     &StarnetStarMaskProcess::on_binaryMaskClicked);
    QObject::connect(m_ui.browseWeights, &QPushButton::released, this,
                     &StarnetStarMaskProcess::on_browseWeightsButtonPushed);

    QObject::connect(m_ui.configureStarnet, &QPushButton::released, this,
                     &StarnetStarMaskProcess::on_configureButtonPushed);

    INIT_D_SPINBOX_CTRL(lowerLimitControl, m_ui.lowerLimitValue, m_ui.lowerLimitSlider,
                        &StarnetStarMaskProcess::on_lowerLimitChanged);
    INIT_D_SPINBOX_CTRL(upperLimitControl, m_ui.upperLimitValue, m_ui.upperLimitSlider,
                        &StarnetStarMaskProcess::on_upperLimitChanged);
    INIT_D_SPINBOX_CTRL(smoothnessControl, m_ui.smoothnessValue, m_ui.smoothnessSlider,
                        &StarnetStarMaskProcess::on_smoothnessChanged);

    updateWindowTitle();

    m_SupportTargetViewControlSelection = true;
    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    refreshTheme();

    initialize();
}

std::string StarnetStarMaskProcess::getProcessNameStatic()
{
    return "Starnet++ Star Mask";
}

bool StarnetStarMaskProcess::requiresExclusiveAccessStatic()
{
    return false;
}

StarnetStarMaskProcess::Settings StarnetStarMaskProcess::settings() const
{
    Settings asked;
    asked.stride = currentStride();
    asked.upsample = m_ui.upsamplex2->isChecked();
    asked.highlightProtection = m_ui.highlightProtection->isChecked();
    asked.weightsPath = m_ui.weightsPath->text().trimmed();
    asked.preStretch = m_ui.preStretchLinearImage->isChecked();
    asked.shaping.lightness = m_ui.lightness->isChecked();
    asked.shaping.binary = asked.shaping.lightness && m_ui.binaryMask->isChecked();
    asked.shaping.lowerLimit = lowerLimitControl->getValue();
    asked.shaping.upperLimit = upperLimitControl->getValue();
    asked.shaping.smoothness = smoothnessControl->getValue();
    return asked;
}

an::ui::processes::ExternalToolRequest StarnetStarMaskProcess::toolRequest(const Settings& settings,
                                                                           const QString& executablePath)
{
    using an::ui::processes::ExternalToolFiles;
    using an::ui::processes::ExternalToolRequest;

    ExternalToolRequest request;
    request.tool.name = QStringLiteral("StarNet2");
    request.tool.executablePath = executablePath;
    request.tool.format = an::ExternalToolFormat::UInt16;
    request.preStretch = settings.preStretch ? an::ExternalToolPreStretch::Linked : an::ExternalToolPreStretch::None;
    request.wantsImage = false;
    request.wantsMask = true;
    request.wantsStars = false;
    request.maskState = an::ui::processes::kStarnetStarMaskState;
    request.lineage = "StarNet2";
    request.arguments = [settings](const ExternalToolFiles& files, bool machineProgress) {
        an::ui::starnet::CliOptions options;
        options.inputPath = files.input;
        options.outputPath = files.image;
        options.maskPath = files.mask;
        options.stride = settings.stride;
        options.upsample = settings.upsample;
        options.highlightProtection = settings.highlightProtection;
        options.weightsPath = settings.weightsPath;
        options.machineProgress = machineProgress;
        return an::ui::starnet::buildArguments(options);
    };
    return request;
}

void StarnetStarMaskProcess::startExecution()
{
    if (m_ExecutionInProgress || m_image_view_control == nullptr || !m_image_view_control->hasImage())
    {
        return;
    }
    // The dialog is read here, on the GUI thread; the run reads only this.
    const Settings asked = settings();
    const std::string target = m_image_view_control->getUniqueName();
    const an::ui::processes::ExternalToolRequest tool = toolRequest(asked, m_StarnetFullPath);

    m_ExecutionInProgress = true;
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);

    LOG_INFO("Executing process '{}' on {} '{}'... ", getProcessName(),
             m_image_view_control->isSequence() ? "sequence" : "image", target);

    an::ui::processes::NewViewsRunRequest request;
    request.words = an::ui::processes::GeometryRunWords{
        .title = QString::fromStdString(fmt::format("{} <{}>", getProcessName(), target)),
        .progressText = QString::fromStdString(fmt::format("Executing StarNet2 on {}", target)),
        .errorTitle = QStringLiteral("Error running StarNet2"),
        .errorLead = "An error occurred when running StarNet2 process"};
    request.imageOutputs.push_back(an::ui::processes::NewViewOutput{
        .name = target + " - STARMASK", .history = fmt::format("StarNet2: Starmask image created from '{}'", target)});
    request.sequenceOutputs = request.imageOutputs;
    request.operation = [tool, shaping = asked.shaping](
                            const an::ui::ExecutionScope& scope,
                            const an::ui::ProcessSource& source) -> an::Result<std::vector<an::consumer::ImageAsset>> {
        an::Result<an::ui::processes::ExternalToolAnswers> answers =
            an::ui::processes::runExternalTool(scope, source, tool);
        if (!answers.hasValue())
        {
            return answers.status();
        }
        an::Result<an::consumer::ImageAsset> shaped =
            an::ui::processes::shapeStarMask(scope, std::move(*answers->mask), shaping);
        if (!shaped.hasValue())
        {
            return shaped.status();
        }
        std::vector<an::consumer::ImageAsset> made;
        made.push_back(std::move(*shaped));
        return made;
    };
    request.readDemosaiced = m_image_view_control->isSequenceAutodebayerEnabled();
    request.showDemosaiced = false;
    request.progressWindow = true;
    request.inFlight = beginRunInFlight();
    request.admissionHook = admissionHook();
    QPointer<StarnetStarMaskProcess> guard(this);
    an::ui::processes::runToNewViews(*m_image_view_control, std::move(request),
                                     [guard](const an::ui::processes::GeometryRunOutcome& outcome) {
                                         if (guard)
                                         {
                                             guard->finishRun(outcome);
                                         }
                                     });
}

void StarnetStarMaskProcess::finishRun(const an::ui::processes::GeometryRunOutcome& outcome)
{
    m_ExecutionInProgress = false;
    if (outcome.published)
    {
        LOG_INFO("Process '{}' executed successfully on '{}'", getProcessName(),
                 m_image_view_control != nullptr ? m_image_view_control->getUniqueName() : std::string());
    }
    if (m_ProcessCloseRequested)
    {
        close();
        return;
    }
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(true);
}

void StarnetStarMaskProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
        m_ui.configureStarnet->setIcon(iconProvider->getIcon("settings"));
        m_ui.browseWeights->setIcon(iconProvider->getIcon("folder_open"));
    }
}

StarnetStarMaskProcess::~StarnetStarMaskProcess() {}

void StarnetStarMaskProcess::on_customStrideValueChanged(int value)
{
    const int normalized = an::ui::starnet::normalizeStride(value);
    if (m_ui.customStrideValue->value() != normalized)
    {
        m_ui.customStrideValue->setValue(normalized);
    }
}

void StarnetStarMaskProcess::on_stridePresetChanged(int /*index*/)
{
    const QString mode = m_ui.stridePreset->currentData().toString();
    m_ui.customStrideValue->setEnabled(mode == QString::fromLatin1(kStridePresetCustom));
}

void StarnetStarMaskProcess::on_browseWeightsButtonPushed()
{
    const QString initialDir = an::ui::starnet::weightsBrowseDirectory(m_ui.weightsPath->text(), m_StarnetFullPath);
    if (!initialDir.isEmpty())
    {
        process_utils::rememberPath(QString::fromLatin1(kStarnetWeightsFolderContext), initialDir);
    }

    const auto files = process_utils::selectFiles(this, tr("Select StarNet2 weights"),
                                                  QString::fromLatin1(kStarnetWeightsFolderContext),
                                                  {tr("ONNX models (*.onnx)"), tr("All Files (*.*)")});
    if (!files.isEmpty())
    {
        m_ui.weightsPath->setText(files.front());
    }
}

void StarnetStarMaskProcess::on_lightnessClicked(Qt::CheckState state)
{
    if (state == Qt::Checked)
    {
        m_ui.binaryMask->setEnabled(true);

        lowerLimitControl->setEnabled(m_ui.binaryMask->isChecked());
        upperLimitControl->setEnabled(m_ui.binaryMask->isChecked());
        smoothnessControl->setEnabled(m_ui.binaryMask->isChecked());
    }
    else
    {
        m_ui.binaryMask->setEnabled(false);
        lowerLimitControl->setEnabled(false);
        upperLimitControl->setEnabled(false);
        smoothnessControl->setEnabled(false);
    }
}

void StarnetStarMaskProcess::on_binaryMaskClicked(Qt::CheckState state)
{
    bool enabled = (state == Qt::Checked);
    lowerLimitControl->setEnabled(enabled);
    upperLimitControl->setEnabled(enabled);
    smoothnessControl->setEnabled(enabled);
}

void StarnetStarMaskProcess::on_lowerLimitChanged(double value)
{
    if (value >= upperLimitControl->getValue())
    {
        upperLimitControl->setValue(value);
    }
}

void StarnetStarMaskProcess::on_upperLimitChanged(double value)
{
    if (value <= lowerLimitControl->getValue())
    {
        lowerLimitControl->setValue(value);
    }
}

void StarnetStarMaskProcess::on_smoothnessChanged(double) {}

void StarnetStarMaskProcess::on_configureButtonPushed()
{
    // Open settings dialog at External Tools page
    process_utils::openSettingsPage(tr("External Tools"));

    // Reload path from global settings after dialog closes
    initialize();
}

void StarnetStarMaskProcess::initialize()
{
    // Load path from global settings
    m_StarnetFullPath = process_utils::selectGlobalSetting(SettingsKeys::TOOLS_STARNET_PATH, "");

    const auto check = an::ui::starnet::checkExecutable(m_StarnetFullPath, 3000);
    if (check.supported)
    {
        enableActions();
    }
    else
    {
        disableActions(check.detail);
    }
}

void StarnetStarMaskProcess::enableActions()
{
    m_ui.preStretchLinearImage->setEnabled(true);
    m_ui.stridePreset->setEnabled(true);
    m_ui.customStrideValue->setEnabled(m_ui.stridePreset->currentData().toString() ==
                                       QString::fromLatin1(kStridePresetCustom));
    m_ui.highlightProtection->setEnabled(true);
    m_ui.weightsPath->setEnabled(true);
    m_ui.browseWeights->setEnabled(true);
    m_ui.lightness->setEnabled(true);
    m_ui.binaryMask->setEnabled(m_ui.lightness->isChecked());
    lowerLimitControl->setEnabled(m_ui.lightness->isChecked() && m_ui.binaryMask->isChecked());
    upperLimitControl->setEnabled(m_ui.lightness->isChecked() && m_ui.binaryMask->isChecked());
    smoothnessControl->setEnabled(m_ui.lightness->isChecked() && m_ui.binaryMask->isChecked());

    m_ui.upsamplex2->setEnabled(true);
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!m_ExecutionInProgress);

    m_ui.label->setProperty("anStatusTone", "success");
    m_ui.label->setText(tr("StarNet2 is configured and ready to use."));
}

void StarnetStarMaskProcess::disableActions(const QString& reason)
{
    m_ui.preStretchLinearImage->setEnabled(false);
    m_ui.stridePreset->setEnabled(false);
    m_ui.customStrideValue->setEnabled(false);
    m_ui.highlightProtection->setEnabled(false);
    m_ui.weightsPath->setEnabled(false);
    m_ui.browseWeights->setEnabled(false);
    m_ui.lightness->setEnabled(false);
    m_ui.binaryMask->setEnabled(false);
    lowerLimitControl->setEnabled(false);
    upperLimitControl->setEnabled(false);
    smoothnessControl->setEnabled(false);
    m_ui.upsamplex2->setEnabled(false);
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);

    m_ui.label->setProperty("anStatusTone", "warning");
    const QString detail =
        reason.trimmed().isEmpty()
            ? tr("StarNet2 CLI %1 or newer is required. Click Configure to set the executable path in Settings > "
                 "External Tools.")
                  .arg(an::ui::starnet::minimumSupportedVersionText())
            : reason;
    m_ui.label->setText(detail);
}

int StarnetStarMaskProcess::currentStride() const
{
    const QString mode = m_ui.stridePreset->currentData().toString();
    if (mode == QString::fromLatin1(kStridePresetWideField))
    {
        return 384;
    }
    if (mode == QString::fromLatin1(kStridePresetCustom))
    {
        return an::ui::starnet::normalizeStride(m_ui.customStrideValue->value());
    }
    return 256;
}

void StarnetStarMaskProcess::on_dialog_box_button_clicked(QAbstractButton* button)
{
    QDialogButtonBox* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
        return;

    QDialogButtonBox::StandardButton sb = buttonBox->standardButton(button);

    if (sb == QDialogButtonBox::Ok)
    {
        startExecution();
    }
    else if (sb == QDialogButtonBox::Close)
    {
        this->close();
    }
}
