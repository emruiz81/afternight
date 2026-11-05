// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "DeepSNRProcess.h"
#include "ProcessCommonUtils.h"
#include "controls/ProcessButtonIcons.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/settings/DeepSNRCliTools.h"
#include "controls/settings/SettingsKeys.h"

#include <AfterNightPlatform/Logging.h>

#include <QPointer>

#include <fmt/format.h>

#include <utility>

namespace
{

inline constexpr auto kStridePresetDefault = "default";
inline constexpr auto kStridePresetConservative = "conservative";
inline constexpr auto kStridePresetCustom = "custom";
inline constexpr auto kDeepSNRWeightsFolderContext = "__DEEPSNR_WEIGHTS_FOLDER__";

} // namespace

DeepSNRProcess::DeepSNRProcess(ImageViewControl* control) : ImageProcessControl(control, getProcessNameStatic())
{
    setWindowSizeFitMultipliers(1.1, 1.0);
    m_ui = an::ui::processes::buildDeepSNRProcessUi(this);
    setProcessDescription(QStringLiteral("Reduce high-frequency noise with the DeepSNR command-line tool."));

    this->setAttribute(Qt::WA_DeleteOnClose);

    m_ui.stridePreset->addItem(tr("Default (480)"), QString::fromLatin1(kStridePresetDefault));
    m_ui.stridePreset->addItem(tr("Conservative (256)"), QString::fromLatin1(kStridePresetConservative));
    m_ui.stridePreset->addItem(tr("Custom"), QString::fromLatin1(kStridePresetCustom));
    m_ui.stridePreset->setCurrentIndex(0);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this, &DeepSNRProcess::on_dialog_box_button_clicked);
    QObject::connect(m_ui.customStrideValue, &QSpinBox::valueChanged, this,
                     &DeepSNRProcess::on_customStrideValueChanged);
    QObject::connect(m_ui.stridePreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     &DeepSNRProcess::on_stridePresetChanged);
    QObject::connect(m_ui.browseWeights, &QPushButton::released, this, &DeepSNRProcess::on_browseWeightsButtonPushed);
    QObject::connect(m_ui.configureDeepSNR, &QPushButton::released, this, &DeepSNRProcess::on_configureButtonPushed);

    updateWindowTitle();

    m_SupportTargetViewControlSelection = true;
    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    refreshTheme();

    initialize();
}

std::string DeepSNRProcess::getProcessNameStatic()
{
    return "DeepSNR";
}

bool DeepSNRProcess::requiresExclusiveAccessStatic()
{
    return false;
}

bool DeepSNRProcess::isConfigured()
{
    const QString path = process_utils::selectGlobalSetting(SettingsKeys::TOOLS_DEEPSNR_PATH, {});
    return an::ui::deepsnr::checkExecutable(path, 3000).supported;
}

DeepSNRProcess::Settings DeepSNRProcess::settings() const
{
    Settings asked;
    asked.stride = currentStride();
    asked.weightsPath = m_ui.weightsPath->text().trimmed();
    asked.preStretch = m_ui.preStretchLinearImage->isChecked();
    return asked;
}

std::string DeepSNRProcess::stepDescription(const Settings& settings)
{
    return fmt::format("Denoised with DeepSNR. Stride: {}. Pre-stretch: {}", settings.stride,
                       settings.preStretch ? "enabled" : "disabled");
}

an::ui::processes::ExternalToolRequest
DeepSNRProcess::toolRequest(const Settings& settings, const QString& executablePath, std::uint32_t minimumSize)
{
    using an::ui::processes::ExternalToolFiles;
    using an::ui::processes::ExternalToolRequest;

    ExternalToolRequest request;
    request.tool.name = QStringLiteral("DeepSNR");
    request.tool.executablePath = executablePath;
    request.tool.format = an::ExternalToolFormat::UInt16;
    request.tool.minimumWidth = minimumSize;
    request.tool.minimumHeight = minimumSize;
    request.preStretch = settings.preStretch ? an::ExternalToolPreStretch::Unlinked : an::ExternalToolPreStretch::None;
    request.wantsImage = true;
    request.wantsMask = false;
    request.wantsStars = false;
    request.imageState = std::nullopt;
    request.lineage = "DeepSNR";
    request.arguments = [settings](const ExternalToolFiles& files, bool machineProgress) {
        an::ui::deepsnr::CliOptions options;
        options.inputPath = files.input;
        options.outputPath = files.image;
        options.stride = settings.stride;
        options.weightsPath = settings.weightsPath;
        options.machineProgress = machineProgress;
        return an::ui::deepsnr::buildArguments(options);
    };
    return request;
}

void DeepSNRProcess::startExecution()
{
    if (m_ExecutionInProgress || m_ProcessCloseRequested || m_image_view_control == nullptr ||
        !m_image_view_control->hasImage())
    {
        return;
    }
    // The dialog is read here, on the GUI thread; the run reads only this.
    const Settings asked = settings();
    const std::string target = m_image_view_control->getUniqueName();
    const an::ui::processes::ExternalToolRequest tool = toolRequest(asked, m_DeepSNRFullPath, minimumImageSize());

    m_ExecutionInProgress = true;
    disableActionsForExecution();

    LOG_INFO("Executing process '{}' on {} '{}'... ", getProcessName(),
             m_image_view_control->isSequence() ? "sequence" : "image", target);

    an::ui::processes::InPlaceRunRequest request;
    request.words = an::ui::processes::GeometryRunWords{
        .title = QString::fromStdString(fmt::format("{} <{}>", getProcessName(), target)),
        .progressText = QString::fromStdString(fmt::format("Executing DeepSNR on {}", target)),
        .errorTitle = QStringLiteral("Error running DeepSNR"),
        .errorLead = "An error occurred when running DeepSNR process"};
    request.stepName = getProcessName();
    request.stepDescription = stepDescription(asked);
    request.operation = [tool](const an::ui::ExecutionScope& scope,
                               const an::ui::ProcessSource& source) -> an::Result<an::consumer::ImageAsset> {
        an::Result<an::ui::processes::ExternalToolAnswers> answers =
            an::ui::processes::runExternalTool(scope, source, tool);
        if (!answers.hasValue())
        {
            return answers.status();
        }
        return std::move(*answers->image);
    };
    request.progressWindow = true;
    request.inFlight = beginRunInFlight();
    request.admissionHook = admissionHook();
    QPointer<DeepSNRProcess> guard(this);
    an::ui::processes::runInPlace(*m_image_view_control, std::move(request),
                                  [guard](const an::ui::processes::GeometryRunOutcome& outcome) {
                                      if (guard)
                                      {
                                          guard->finishRun(outcome);
                                      }
                                  });
}

void DeepSNRProcess::finishRun(const an::ui::processes::GeometryRunOutcome& outcome)
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
    enableActions();
}

void DeepSNRProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
        m_ui.configureDeepSNR->setIcon(iconProvider->getIcon("settings"));
        m_ui.browseWeights->setIcon(iconProvider->getIcon("folder_open"));
    }
}

DeepSNRProcess::~DeepSNRProcess() {}

void DeepSNRProcess::on_customStrideValueChanged(int value)
{
    const int normalized = an::ui::deepsnr::normalizeStride(value);
    if (m_ui.customStrideValue->value() != normalized)
    {
        m_ui.customStrideValue->setValue(normalized);
    }
}

void DeepSNRProcess::on_stridePresetChanged(int /*index*/)
{
    const QString mode = m_ui.stridePreset->currentData().toString();
    m_ui.customStrideValue->setEnabled(mode == QString::fromLatin1(kStridePresetCustom));
}

void DeepSNRProcess::on_browseWeightsButtonPushed()
{
    const QString initialDir = an::ui::deepsnr::weightsBrowseDirectory(m_ui.weightsPath->text(), m_DeepSNRFullPath);
    if (!initialDir.isEmpty())
    {
        process_utils::rememberPath(QString::fromLatin1(kDeepSNRWeightsFolderContext), initialDir);
    }

    const auto files = process_utils::selectFiles(this, tr("Select DeepSNR weights"),
                                                  QString::fromLatin1(kDeepSNRWeightsFolderContext),
                                                  {tr("ONNX models (*.onnx)"), tr("All Files (*.*)")});
    if (!files.isEmpty())
    {
        m_ui.weightsPath->setText(files.front());
    }
}

void DeepSNRProcess::on_configureButtonPushed()
{
    // Open settings dialog at External Tools page
    process_utils::openSettingsPage(tr("External Tools"));

    // Reload path from global settings after dialog closes
    initialize();
}

void DeepSNRProcess::initialize()
{
    // Load path from global settings
    m_DeepSNRFullPath = process_utils::selectGlobalSetting(SettingsKeys::TOOLS_DEEPSNR_PATH, "");

    const auto check = an::ui::deepsnr::checkExecutable(m_DeepSNRFullPath, 3000);
    if (check.supported)
    {
        enableActions();
    }
    else
    {
        disableActions(check.detail);
    }
}

void DeepSNRProcess::enableActions()
{
    m_ui.targetImageSelector->setEnabled(true);
    m_ui.configureDeepSNR->setEnabled(true);
    m_ui.preStretchLinearImage->setEnabled(true);
    m_ui.stridePreset->setEnabled(true);
    m_ui.customStrideValue->setEnabled(m_ui.stridePreset->currentData().toString() ==
                                       QString::fromLatin1(kStridePresetCustom));
    m_ui.weightsPath->setEnabled(true);
    m_ui.browseWeights->setEnabled(true);
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!m_ExecutionInProgress);

    m_ui.label->setProperty("anStatusTone", "success");
    m_ui.label->setText(tr("DeepSNR is configured and ready to use."));
    if (auto* lStyle = m_ui.label->style())
    {
        lStyle->unpolish(m_ui.label);
        lStyle->polish(m_ui.label);
    }
    m_ui.label->update();
}

void DeepSNRProcess::disableActions(const QString& reason)
{
    m_ui.targetImageSelector->setEnabled(true);
    m_ui.configureDeepSNR->setEnabled(true);
    m_ui.preStretchLinearImage->setEnabled(false);
    m_ui.stridePreset->setEnabled(false);
    m_ui.customStrideValue->setEnabled(false);
    m_ui.weightsPath->setEnabled(false);
    m_ui.browseWeights->setEnabled(false);
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);

    m_ui.label->setProperty("anStatusTone", "warning");
    const QString detail =
        reason.trimmed().isEmpty()
            ? tr("DeepSNR CLI %1 or newer is required. Click Configure to set the executable path in Settings > "
                 "External Tools.")
                  .arg(an::ui::deepsnr::minimumSupportedVersionText())
            : reason;
    m_ui.label->setText(detail);
    if (auto* lStyle = m_ui.label->style())
    {
        lStyle->unpolish(m_ui.label);
        lStyle->polish(m_ui.label);
    }
    m_ui.label->update();
}

void DeepSNRProcess::disableActionsForExecution()
{
    m_ui.targetImageSelector->setEnabled(false);
    m_ui.configureDeepSNR->setEnabled(false);
    m_ui.preStretchLinearImage->setEnabled(false);
    m_ui.stridePreset->setEnabled(false);
    m_ui.customStrideValue->setEnabled(false);
    m_ui.weightsPath->setEnabled(false);
    m_ui.browseWeights->setEnabled(false);
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
}

int DeepSNRProcess::currentStride() const
{
    const QString mode = m_ui.stridePreset->currentData().toString();
    if (mode == QString::fromLatin1(kStridePresetConservative))
    {
        return 256;
    }
    if (mode == QString::fromLatin1(kStridePresetCustom))
    {
        return an::ui::deepsnr::normalizeStride(m_ui.customStrideValue->value());
    }
    return an::ui::deepsnr::kDefaultStride;
}

void DeepSNRProcess::on_dialog_box_button_clicked(QAbstractButton* button)
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
