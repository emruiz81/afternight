// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "StarnetProcess.h"
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

inline constexpr auto kStarOutputMask = "mask";
inline constexpr auto kStarOutputUnscreen = "unscreen";
inline constexpr auto kStarOutputBoth = "both";
inline constexpr auto kStridePresetStandard = "standard";
inline constexpr auto kStridePresetWideField = "wide-field";
inline constexpr auto kStridePresetCustom = "custom";
inline constexpr auto kStarnetWeightsFolderContext = "__STARNET_WEIGHTS_FOLDER__";

} // namespace

StarnetProcess::StarnetProcess(ImageViewControl* control) : ImageProcessControl(control, getProcessNameStatic())
{
    setWindowSizeFitMultipliers(1.1, 1.0);
    m_ui = an::ui::processes::buildStarnetProcessUi(this);
    setProcessDescription(
        QStringLiteral("Extract stars or starless output with StarNet2 and optional native mask generation."));

    this->setAttribute(Qt::WA_DeleteOnClose);

    m_ui.starOutputMode->addItem(tr("Subtractive Mask"), QString::fromLatin1(kStarOutputMask));
    m_ui.starOutputMode->addItem(tr("Unscreen Star Layer"), QString::fromLatin1(kStarOutputUnscreen));
    m_ui.starOutputMode->addItem(tr("Both"), QString::fromLatin1(kStarOutputBoth));
    m_ui.starOutputMode->setCurrentIndex(0);

    m_ui.stridePreset->addItem(tr("Standard (256)"), QString::fromLatin1(kStridePresetStandard));
    m_ui.stridePreset->addItem(tr("Wide-field (384)"), QString::fromLatin1(kStridePresetWideField));
    m_ui.stridePreset->addItem(tr("Custom"), QString::fromLatin1(kStridePresetCustom));
    m_ui.stridePreset->setCurrentIndex(0);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this, &StarnetProcess::on_dialog_box_button_clicked);
    QObject::connect(m_ui.customStrideValue, &QSpinBox::valueChanged, this,
                     &StarnetProcess::on_customStrideValueChanged);
    QObject::connect(m_ui.stridePreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     &StarnetProcess::on_stridePresetChanged);
    QObject::connect(m_ui.generateStarMask, &QCheckBox::checkStateChanged, this,
                     &StarnetProcess::on_generateStarMaskClicked);
    QObject::connect(m_ui.browseWeights, &QPushButton::released, this, &StarnetProcess::on_browseWeightsButtonPushed);
    QObject::connect(m_ui.configureStarnet, &QPushButton::released, this, &StarnetProcess::on_configureButtonPushed);

    updateWindowTitle();

    m_SupportTargetViewControlSelection = true;
    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    refreshTheme();

    initialize();
}

std::string StarnetProcess::getProcessNameStatic()
{
    return "Starnet++";
}

bool StarnetProcess::requiresExclusiveAccessStatic()
{
    return false;
}

bool StarnetProcess::isConfigured()
{
    const QString path = process_utils::selectGlobalSetting(SettingsKeys::TOOLS_STARNET_PATH, {});
    return an::ui::starnet::checkExecutable(path, 3000).supported;
}

bool StarnetProcess::requestsMaskOutput(const QString& mode)
{
    return mode == QString::fromLatin1(kStarOutputMask) || mode == QString::fromLatin1(kStarOutputBoth);
}

bool StarnetProcess::requestsUnscreenOutput(const QString& mode)
{
    return mode == QString::fromLatin1(kStarOutputUnscreen) || mode == QString::fromLatin1(kStarOutputBoth);
}

StarnetProcess::Settings StarnetProcess::settings() const
{
    Settings asked;
    asked.stride = currentStride();
    asked.upsample = m_ui.upsamplex2->isChecked();
    asked.highlightProtection = m_ui.highlightProtection->isChecked();
    asked.weightsPath = m_ui.weightsPath->text().trimmed();
    asked.preStretch = m_ui.preStretchLinearImage->isChecked();
    asked.generateStarOutput = m_ui.generateStarMask->isChecked();
    asked.starOutputMode = m_ui.starOutputMode->currentData().toString();
    return asked;
}

an::ui::processes::ExternalToolRequest StarnetProcess::toolRequest(const Settings& settings,
                                                                   const QString& executablePath)
{
    using an::ui::processes::ExternalToolFiles;
    using an::ui::processes::ExternalToolRequest;

    ExternalToolRequest request;
    request.tool.name = QStringLiteral("StarNet2");
    request.tool.executablePath = executablePath;
    request.tool.format = an::ExternalToolFormat::UInt16;
    request.preStretch = settings.preStretch ? an::ExternalToolPreStretch::Unlinked : an::ExternalToolPreStretch::None;
    request.wantsImage = true;
    request.wantsMask = settings.generateStarOutput && requestsMaskOutput(settings.starOutputMode);
    request.wantsStars = settings.generateStarOutput && requestsUnscreenOutput(settings.starOutputMode);
    request.imageState = std::string(an::ui::processes::kStarnetStarlessState);
    request.maskState = an::ui::processes::kStarnetStarMaskState;
    request.starsState = an::ui::processes::kStarnetStarsState;
    request.lineage = "StarNet2";
    request.arguments = [settings, wantsMask = request.wantsMask,
                         wantsStars = request.wantsStars](const ExternalToolFiles& files, bool machineProgress) {
        an::ui::starnet::CliOptions options;
        options.inputPath = files.input;
        options.outputPath = files.image;
        if (wantsMask)
        {
            options.maskPath = files.mask;
        }
        if (wantsStars)
        {
            options.unscreenPath = files.stars;
        }
        options.stride = settings.stride;
        options.upsample = settings.upsample;
        options.highlightProtection = settings.highlightProtection;
        options.weightsPath = settings.weightsPath;
        options.machineProgress = machineProgress;
        return an::ui::starnet::buildArguments(options);
    };
    return request;
}

void StarnetProcess::configureIntegratedStarlessGeneration(StarlessCreatedCallback callback)
{
    m_StarlessCreatedCallback = std::move(callback);
    m_CloseAfterIntegratedGeneration = true;
    m_ui.generateStarMask->setChecked(false);
    an::ui::processes::selectStarnetUnscreenExtractionMode(m_ui);
    on_generateStarMaskClicked(Qt::Unchecked);
}

void StarnetProcess::startExecution()
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
        .name = target + " - starless", .history = fmt::format("StarNet2: Starless image created from '{}'", target)});
    if (tool.wantsMask)
    {
        request.imageOutputs.push_back(an::ui::processes::NewViewOutput{
            .name = target + " - star mask", .history = fmt::format("StarNet2: Star mask created from '{}'", target)});
    }
    if (tool.wantsStars)
    {
        request.imageOutputs.push_back(an::ui::processes::NewViewOutput{
            .name = target + " - stars",
            .history = fmt::format("StarNet2: Unscreen stars image created from '{}'", target)});
    }
    request.sequenceOutputs = request.imageOutputs;
    request.operation =
        [tool](const an::ui::ExecutionScope& scope,
               const an::ui::ProcessSource& source) -> an::Result<std::vector<an::consumer::ImageAsset>> {
        an::Result<an::ui::processes::ExternalToolAnswers> answers =
            an::ui::processes::runExternalTool(scope, source, tool);
        if (!answers.hasValue())
        {
            return answers.status();
        }
        std::vector<an::consumer::ImageAsset> made;
        made.push_back(std::move(*answers->image));
        if (answers->mask.has_value())
        {
            made.push_back(std::move(*answers->mask));
        }
        if (answers->stars.has_value())
        {
            made.push_back(std::move(*answers->stars));
        }
        return made;
    };
    request.readDemosaiced = m_image_view_control->isSequenceAutodebayerEnabled();
    request.showDemosaiced = false;
    request.progressWindow = true;
    request.inFlight = beginRunInFlight();
    request.admissionHook = admissionHook();
    QPointer<StarnetProcess> guard(this);
    an::ui::processes::runToNewViews(*m_image_view_control, std::move(request),
                                     [guard](const an::ui::processes::GeometryRunOutcome& outcome) {
                                         if (guard)
                                         {
                                             guard->finishRun(outcome);
                                         }
                                     });
}

void StarnetProcess::finishRun(const an::ui::processes::GeometryRunOutcome& outcome)
{
    m_ExecutionInProgress = false;
    if (outcome.published)
    {
        LOG_INFO("Process '{}' executed successfully on '{}'", getProcessName(),
                 m_image_view_control != nullptr ? m_image_view_control->getUniqueName() : std::string());
    }
    if (outcome.published && !outcome.views.empty() && m_StarlessCreatedCallback)
    {
        m_StarlessCreatedCallback(outcome.views.front());
    }
    if (m_ProcessCloseRequested || (m_CloseAfterIntegratedGeneration && outcome.published))
    {
        close();
        return;
    }
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(true);
}

void StarnetProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
        m_ui.configureStarnet->setIcon(iconProvider->getIcon("settings"));
        m_ui.browseWeights->setIcon(iconProvider->getIcon("folder_open"));
    }
}

StarnetProcess::~StarnetProcess() {}

void StarnetProcess::on_customStrideValueChanged(int value)
{
    const int normalized = an::ui::starnet::normalizeStride(value);
    if (m_ui.customStrideValue->value() != normalized)
    {
        m_ui.customStrideValue->setValue(normalized);
    }
}

void StarnetProcess::on_stridePresetChanged(int /*index*/)
{
    const QString mode = m_ui.stridePreset->currentData().toString();
    m_ui.customStrideValue->setEnabled(mode == QString::fromLatin1(kStridePresetCustom));
}

void StarnetProcess::on_generateStarMaskClicked(Qt::CheckState state)
{
    const bool enabled = (state == Qt::Checked);
    m_ui.starOutputModeLabel->setEnabled(enabled);
    m_ui.starOutputMode->setEnabled(enabled);
}

void StarnetProcess::on_browseWeightsButtonPushed()
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

void StarnetProcess::on_configureButtonPushed()
{
    // Open settings dialog at External Tools page
    process_utils::openSettingsPage(tr("External Tools"));

    // Reload path from global settings after dialog closes
    initialize();
}

void StarnetProcess::initialize()
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

void StarnetProcess::enableActions()
{
    m_ui.preStretchLinearImage->setEnabled(true);
    m_ui.stridePreset->setEnabled(true);
    m_ui.customStrideValue->setEnabled(m_ui.stridePreset->currentData().toString() ==
                                       QString::fromLatin1(kStridePresetCustom));
    m_ui.generateStarMask->setEnabled(true);
    m_ui.starOutputModeLabel->setEnabled(m_ui.generateStarMask->isChecked());
    m_ui.starOutputMode->setEnabled(m_ui.generateStarMask->isChecked());
    m_ui.highlightProtection->setEnabled(true);
    m_ui.weightsPath->setEnabled(true);
    m_ui.browseWeights->setEnabled(true);
    m_ui.upsamplex2->setEnabled(true);
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!m_ExecutionInProgress);

    m_ui.label->setProperty("anStatusTone", "success");
    m_ui.label->setText(tr("StarNet2 is configured and ready to use."));
    if (auto* lStyle = m_ui.label->style())
    {
        lStyle->unpolish(m_ui.label);
        lStyle->polish(m_ui.label);
    }
    m_ui.label->update();
}

void StarnetProcess::disableActions(const QString& reason)
{
    m_ui.preStretchLinearImage->setEnabled(false);
    m_ui.stridePreset->setEnabled(false);
    m_ui.customStrideValue->setEnabled(false);
    m_ui.generateStarMask->setEnabled(false);
    m_ui.starOutputModeLabel->setEnabled(false);
    m_ui.starOutputMode->setEnabled(false);
    m_ui.highlightProtection->setEnabled(false);
    m_ui.weightsPath->setEnabled(false);
    m_ui.browseWeights->setEnabled(false);
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
    if (auto* lStyle = m_ui.label->style())
    {
        lStyle->unpolish(m_ui.label);
        lStyle->polish(m_ui.label);
    }
    m_ui.label->update();
}

int StarnetProcess::currentStride() const
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

void StarnetProcess::on_dialog_box_button_clicked(QAbstractButton* button)
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
