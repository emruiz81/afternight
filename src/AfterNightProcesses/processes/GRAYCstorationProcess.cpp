// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "GRAYCstorationProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/SpatialProcessSupport.h"
#include "processes/GmicProcessSupport.h"

GRAYCstorationProcess::GRAYCstorationProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<GRAYCstorationParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildGRAYCstorationProcessUi(this);

    QObject::connect(m_ui.interpolationSelector, &QComboBox::currentTextChanged, this,
                     &GRAYCstorationProcess::on_interpolationSelector_currentTextChanged);
    QObject::connect(m_ui.fastApproximation, &QCheckBox::checkStateChanged, this,
                     &GRAYCstorationProcess::on_fastApproximationClicked);

    INIT_SPINBOX_CTRL(iterationsControl, m_ui.iterationsValue, m_ui.iterationsSlider,
                      &GRAYCstorationProcess::on_iterationsChanged);
    INIT_SPINBOX_CTRL(amplitudeControl, m_ui.amplitudeValue, m_ui.amplitudeSlider,
                      &GRAYCstorationProcess::on_amplitudeChanged);
    INIT_D_SPINBOX_CTRL(sharpnessControl, m_ui.sharpnessValue, m_ui.sharpnessSlider,
                        &GRAYCstorationProcess::on_sharpnessChanged);
    INIT_D_SPINBOX_CTRL(anisotropyControl, m_ui.anisotropyValue, m_ui.anisotropySlider,
                        &GRAYCstorationProcess::on_anisotropyChanged);
    INIT_D_SPINBOX_CTRL(noiseScaleControl, m_ui.noiseScaleValue, m_ui.noiseScaleSlider,
                        &GRAYCstorationProcess::on_noiseScaleChanged);
    INIT_D_SPINBOX_CTRL(regularityControl, m_ui.regularityValue, m_ui.regularitySlider,
                        &GRAYCstorationProcess::on_regularityChanged);
    INIT_D_SPINBOX_CTRL(spatialStepSizeControl, m_ui.spatialStepSizeValue, m_ui.spatialStepSizeSlider,
                        &GRAYCstorationProcess::on_spatialStepSizeChanged);
    INIT_SPINBOX_CTRL(angularStepSizeControl, m_ui.angularStepSizeValue, m_ui.angularStepSizeSlider,
                      &GRAYCstorationProcess::on_angularStepSizeChanged);
    INIT_D_SPINBOX_CTRL(precisionControl, m_ui.precisionValue, m_ui.precisionSlider,
                        &GRAYCstorationProcess::on_precisionChanged);
    INIT_D_SPINBOX_CTRL(amountControl, m_ui.amountValue, m_ui.amountSlider, &GRAYCstorationProcess::on_amountChanged);

    m_ui.interpolationSelector->addItem("Nearest Neighbor");
    m_ui.interpolationSelector->addItem("Linear");
    m_ui.interpolationSelector->addItem("Runge-Kutta");

    updateWindowTitle();
    setProcessDescription(QStringLiteral(
        "Reduce noise with edge-preserving GRAYCstoration controls for iterations, anisotropy, and precision."));

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.verticalLayout_2, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, m_ui.channelSelector, true, true);

    initialize();

    setExecutionAutostretchSupported(true);
    setupPreviewControl(true, QSize(500, 500), "Preview: GRAYCstoration Filter");

    process_utils::refuseWithoutGmic(m_ui.preview, m_ui.buttonBox);
}

std::string GRAYCstorationProcess::getProcessNameStatic()
{
    return "GRAYCstoration";
}

bool GRAYCstorationProcess::requiresExclusiveAccessStatic()
{
    return false;
}

void GRAYCstorationProcess::onImageUpdated(ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onImageUpdated(ctrl);
}

GRAYCstorationProcess::~GRAYCstorationProcess() {}

void GRAYCstorationProcess::on_iterationsChanged(int value)
{
    settingsUpdated();
}

void GRAYCstorationProcess::on_amplitudeChanged(int value)
{
    settingsUpdated();
}

void GRAYCstorationProcess::on_sharpnessChanged(double value)
{
    settingsUpdated();
}

void GRAYCstorationProcess::on_anisotropyChanged(double value)
{
    settingsUpdated();
}

void GRAYCstorationProcess::on_noiseScaleChanged(double value)
{
    settingsUpdated();
}

void GRAYCstorationProcess::on_regularityChanged(double value)
{
    settingsUpdated();
}

void GRAYCstorationProcess::on_spatialStepSizeChanged(double value)
{
    settingsUpdated();
}

void GRAYCstorationProcess::on_angularStepSizeChanged(int value)
{
    settingsUpdated();
}

void GRAYCstorationProcess::on_precisionChanged(double value)
{
    settingsUpdated();
}

void GRAYCstorationProcess::on_amountChanged(double value)
{
    settingsUpdated();
}

void GRAYCstorationProcess::on_interpolationSelector_currentTextChanged(const QString& text)
{
    settingsUpdated();
}

void GRAYCstorationProcess::on_fastApproximationClicked(Qt::CheckState state)
{
    settingsUpdated();
}

bool GRAYCstorationProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void GRAYCstorationProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void GRAYCstorationProcess::resetSettings()
{
    initialize();

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

void GRAYCstorationProcess::settingsUpdatedImpl()
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);

    process_utils::refuseWithoutGmic(m_ui.preview, m_ui.buttonBox);
}

void GRAYCstorationProcess::enableProcessControlsImpl(bool enabled)
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    iterationsControl->setEnabled(enabled);
    amplitudeControl->setEnabled(enabled);
    sharpnessControl->setEnabled(enabled);
    anisotropyControl->setEnabled(enabled);
    noiseScaleControl->setEnabled(enabled);
    regularityControl->setEnabled(enabled);
    spatialStepSizeControl->setEnabled(enabled);
    angularStepSizeControl->setEnabled(enabled);
    precisionControl->setEnabled(enabled);
    m_ui.fastApproximation->setEnabled(enabled);

    amountControl->setEnabled(enabled);

    m_ui.interpolationSelector->setEnabled(enabled);

    process_utils::refuseWithoutGmic(m_ui.preview, m_ui.buttonBox);
}

void GRAYCstorationProcess::initialize()
{
    iterationsControl->setValue(1);
    amplitudeControl->setValue(60);
    sharpnessControl->setValue(0.7);
    anisotropyControl->setValue(0.3);
    noiseScaleControl->setValue(0.6);
    regularityControl->setValue(1.1);
    spatialStepSizeControl->setValue(0.8);
    angularStepSizeControl->setValue(30);
    precisionControl->setValue(2.0);
    m_ui.fastApproximation->setChecked(true);

    amountControl->setValue(1.0);

    m_ui.interpolationSelector->setCurrentText("Nearest Neighbor");
}

ProgressDialogControl* GRAYCstorationProcess::getProgressDialogControl(const bool forSequence)
{
    return process_utils::gmicProgressDialog(
        this, QString::fromUtf8(fmt::format("{} <{}>", getProcessName(), getParentViewControl()->getUniqueName())),
        forSequence);
}

static int getInterpolationSelectorFromIndex(int index)
{
    if (index == 0) // "Nearest Neighbor"
    {
        return 0;
    }
    else if (index == 1) // "Linear"
    {
        return 1;
    }
    else if (index == 2) // "Runge-Kutta"
    {
        return 2;
    }

    return 0;
}

an::Status GRAYCstorationProcess::executeTypedProcessIteration(const an::ChannelScopeInput& input,
                                                               const an::ChannelScopeOutput& output,
                                                               const an::ui::ProcessRun& run,
                                                               const GRAYCstorationParams& params,
                                                               an::ui::ExecutionScope& scope)
{
    // G'MIC's anisotropic smoothing, repeated the dialog's number of times. The library states how far a run has come
    // only when its script says, so the loop says before each pass: the share of the passes done.
    const std::string command =
        params.iterations > 1
            ? fmt::format("repeat {} {{ progress {{100*$>/{}}} smooth {},{},{},{},{},{},{},{},{},{} }}",
                          params.iterations, params.iterations, params.amplitude, params.sharpness, params.anisotropy,
                          params.noiseScale, params.regularity, params.spatialStepSize, params.angularStepSize,
                          params.precision, params.interpolation, params.fastApproximation ? 1 : 0)
            : fmt::format("smooth {},{},{},{},{},{},{},{},{},{}", params.amplitude, params.sharpness, params.anisotropy,
                          params.noiseScale, params.regularity, params.spatialStepSize, params.angularStepSize,
                          params.precision, params.interpolation, params.fastApproximation ? 1 : 0);
    return process_utils::runGmicCommand(scope, input, output, process_utils::GmicCommand{.text = command});
}

an::Status GRAYCstorationProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                          an::ui::ProcessDestination& destination,
                                                          const an::ui::ProcessRun& run,
                                                          const GRAYCstorationParams& params,
                                                          an::ui::ExecutionScope& scope)
{
    return executeTypedProcessUsingChannels(source, destination, run, params, scope);
}

an::ui::ProcessStep GRAYCstorationProcess::describeTypedProcessStep(const GRAYCstorationParams& params) const
{
    return an::ui::ProcessStep{.name = getProcessName(),
                               .description =
                                   fmt::format("Interpolation: {}, Iterations: {}, Amplitude: {}, Sharpness: {:.2f}, "
                                               "Anisotropy: {:.2f}, Noise Scale: {:.2f}, Regularity: {:.2f}, "
                                               "Spatial Step Size: {:.2f}, Angular Step Size: {}, "
                                               "Precision: {:.2f}, Amount: {:.2f}",
                                               m_ui.interpolationSelector->currentText().toUtf8().toStdString(),
                                               params.iterations, params.amplitude, params.sharpness, params.anisotropy,
                                               params.noiseScale, params.regularity, params.spatialStepSize,
                                               params.angularStepSize, params.precision, params.amount) +
                                   process_utils::channelStepSuffix(m_image_view_control, m_ui.channelSelector)};
}

double GRAYCstorationProcess::getTypedModulationFromParams(const GRAYCstorationParams& params)
{
    return params.amount;
}

GRAYCstorationParams GRAYCstorationProcess::getParams() const
{
    GRAYCstorationParams params;

    params.interpolation = getInterpolationSelectorFromIndex(m_ui.interpolationSelector->currentIndex());
    params.fastApproximation = m_ui.fastApproximation->isChecked();

    params.iterations = iterationsControl->getValue();
    params.amplitude = amplitudeControl->getValue();

    params.sharpness = sharpnessControl->getValue();
    params.anisotropy = anisotropyControl->getValue();
    params.noiseScale = noiseScaleControl->getValue();
    params.regularity = regularityControl->getValue();
    params.spatialStepSize = spatialStepSizeControl->getValue();
    params.angularStepSize = angularStepSizeControl->getValue();
    params.precision = precisionControl->getValue();
    params.amount = amountControl->getValue();

    return params;
}
