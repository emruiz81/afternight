// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "AutostretchSettingsControl.h"
#include "controls/AutostretchApply.h"
#include "controls/ImageViewControl.h"
#include "controls/IAppContext.h"
#include "app/GuiThreadInvoke.h"

#include <QMessageBox>

namespace
{

[[nodiscard]] an::ui::IDialogService* currentDialogService()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getDialogService();
    }

    return nullptr;
}

} // namespace

AutostretchSettingsControl::AutostretchSettingsControl(ImageViewControl* control)
    : QDialog(control), ImageViewControlListener(control),
      m_DisplayUpdate(this, [this](const AutostretchParams& params) {
          m_image_view_control->getViewWidget()->setAutostretchSettings(params, false);
      })
{
    m_ui = an::ui::controls::buildAutostretchSettingsControlUi(this);

    // Minimize button.. disabled for now
    // setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint);

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &AutostretchSettingsControl::on_dialog_box_button_clicked);
    QObject::connect(m_ui.linkedChannels, &QCheckBox::checkStateChanged, this,
                     &AutostretchSettingsControl::on_linkedChannelsClicked);
    QObject::connect(m_ui.neutralizeBackground, &QCheckBox::checkStateChanged, this,
                     &AutostretchSettingsControl::on_neutralizeBackgroundClicked);
    QObject::connect(m_ui.colorCalibration, &QCheckBox::checkStateChanged, this,
                     &AutostretchSettingsControl::on_colorCalibrationClicked);
    QObject::connect(m_ui.autostretchBanner, &QCheckBox::checkStateChanged, this,
                     &AutostretchSettingsControl::on_autostretchBannerClicked);
    QObject::connect(m_ui.applyStretchButton, &QPushButton::released, this,
                     &AutostretchSettingsControl::on_applyStretchButtonPushed);

    INIT_D_SPINBOX_CTRL(targetBackgroundControl, m_ui.targetBackgroundValue, m_ui.targetBackgroundSlider,
                        &AutostretchSettingsControl::on_targetBackgroundChanged);
    INIT_D_SPINBOX_CTRL(shadowsClippingControl, m_ui.shadowsClippingValue, m_ui.shadowsClippingSlider,
                        &AutostretchSettingsControl::on_shadowsClippingChanged);
    INIT_D_SPINBOX_CTRL(saturationControl, m_ui.saturationValue, m_ui.saturationSlider,
                        &AutostretchSettingsControl::on_saturationChanged);
    INIT_D_SPINBOX_CTRL(saturationBackgroundControl, m_ui.saturationBackgroundValue, m_ui.saturationBackgroundSlider,
                        &AutostretchSettingsControl::on_saturationBackgroundChanged);
    INIT_SPINBOX_CTRL(brightnessControl, m_ui.brightnessValue, m_ui.brightnessSlider,
                      &AutostretchSettingsControl::on_brightnessChanged);
    INIT_SPINBOX_CTRL(contrastControl, m_ui.contrastValue, m_ui.contrastSlider,
                      &AutostretchSettingsControl::on_contrastChanged);

    auto uniqueName = m_image_view_control->getUniqueName();

    auto title = fmt::format("Autostretch Settings <{}>", uniqueName);
    setWindowTitle(QString::fromUtf8(title));

    m_image_view_control->setAutostretchSettingsActionEnabled(false);

    auto settings = m_image_view_control->getViewWidget()->getAutostretchSettings();

    targetBackgroundControl->setValue(settings.targetBackground);
    shadowsClippingControl->setValue(settings.shadowsClipping);
    saturationControl->setValue(settings.saturation);
    saturationBackgroundControl->setValue(settings.saturationBackground);
    brightnessControl->setValue(settings.brightness);
    contrastControl->setValue(settings.contrast);

    m_ui.linkedChannels->setChecked(settings.linkedChannels);
    m_ui.neutralizeBackground->setChecked(settings.neutralizeBackground);
    m_ui.colorCalibration->setChecked(settings.colorCalibration);

    m_ui.autostretchBanner->setChecked(m_image_view_control->isShowAutostretchBannerEnabled());

    if (control->getViewControlMode() != ViewControlMode::standard)
    {
        m_ui.applyStretchButton->hide();
    }

    refreshColorActions();
}

void AutostretchSettingsControl::onImageRenamed(ImageViewControl* ctrl)
{
    std::string title = fmt::format("Autostretch Settings <{}>", m_image_view_control->getUniqueName());
    setWindowTitle(QString::fromUtf8(title));
}

void AutostretchSettingsControl::onImageUpdated(ImageViewControl* ctrl)
{
    // A process can update the view from its worker: the controls are read on the GUI thread.
    SAFE_INVOKE_LAMBDA_IN_GUI_THREAD(this, [this]() {
        refreshColorActions();
        settingsUpdated();
    });
}

void AutostretchSettingsControl::setColorCalibrationControlsEnabled(bool enabled)
{
    m_ColorCalibrationControlsEnabled = enabled;
    m_ui.colorCalibration->setEnabled(enabled && m_ui.linkedChannels->isChecked());
    m_ui.neutralizeBackground->setEnabled(enabled && m_ui.linkedChannels->isChecked());
}

AutostretchSettingsControl::~AutostretchSettingsControl() {}

void AutostretchSettingsControl::on_targetBackgroundChanged(double value)
{
    settingsUpdated();
}

void AutostretchSettingsControl::on_shadowsClippingChanged(double value)
{
    settingsUpdated();
}

void AutostretchSettingsControl::on_saturationChanged(double value)
{
    settingsUpdated();
}

void AutostretchSettingsControl::on_saturationBackgroundChanged(double value)
{
    settingsUpdated();
}
void AutostretchSettingsControl::on_linkedChannelsClicked(Qt::CheckState state)
{
    if (state == Qt::Checked)
    {
        m_ui.neutralizeBackground->setEnabled(m_ColorCalibrationControlsEnabled);
        m_ui.colorCalibration->setEnabled(m_ColorCalibrationControlsEnabled);
    }
    else
    {
        m_ui.neutralizeBackground->setEnabled(false);
        m_ui.colorCalibration->setEnabled(false);
    }

    settingsUpdated();
}

void AutostretchSettingsControl::on_neutralizeBackgroundClicked(Qt::CheckState state)
{
    settingsUpdated();
}

void AutostretchSettingsControl::on_colorCalibrationClicked(Qt::CheckState state)
{
    settingsUpdated();
}

void AutostretchSettingsControl::on_autostretchBannerClicked(Qt::CheckState state)
{
    m_image_view_control->setShowAutostretchBanner(state == Qt::Checked);
}

void AutostretchSettingsControl::on_applyStretchButtonPushed()
{
    int ret = QMessageBox::No;

    std::string msg =
        fmt::format("Do you want to apply the current autostretch preview settings to this image permanently?<br>"
                    "<h3>It is generally recommended to use more advanced stretch processes like Histogram "
                    "Transformation or Generalized Hyperbolic Transformation.</h3>");
    const QString prompt = QString::fromUtf8(msg);
    if (auto* dialogService = currentDialogService())
    {
        dialogService->messageBoxYesNo(this, tr("Apply Autostretch"), prompt, true, &ret);
    }
    else
    {
        QMessageBox messageBox(QMessageBox::Warning, tr("Apply Autostretch"), prompt,
                               QMessageBox::Yes | QMessageBox::No, this);
        ret = messageBox.exec();
    }

    if (ret == QMessageBox::Yes)
    {
        // Calling m_image_view_control->notifyImageUpdated(); will trigger this->onImageUpdated(), which will schedule
        // an update. We don't want that
        m_IgnoreLastSettingsUpdated = true;

        an::ui::AutostretchApplySettings settings;
        settings.targetBackground = targetBackgroundControl->getValue();
        settings.shadowsClipping = shadowsClippingControl->getValue();
        settings.linkedChannels = m_ui.linkedChannels->isChecked();

        // Color calibration only makes sense if working under linked mode (because unlinked stretch will undo
        // everything done in this step)
        if (settings.linkedChannels)
        {
            settings.neutralizeBackground =
                m_ui.neutralizeBackground->isChecked() && m_ui.neutralizeBackground->isEnabled();
            settings.colorCalibration = m_ui.colorCalibration->isChecked() && m_ui.colorCalibration->isEnabled();
        }

        settings.saturation = saturationControl->getValue();
        settings.saturationBackground = saturationBackgroundControl->getValue();
        settings.brightness = brightnessControl->getValue();
        settings.contrast = contrastControl->getValue();
        settings.sequenceFrame = m_image_view_control->isSequence();

        // The stretch is an edit of the view: computed on a copy under the document's writer and recorded in the
        // history before the view shows it. When the history cannot keep it, the view is left as it is and the window
        // stays open.
        QPointer<AutostretchSettingsControl> guard(this);
        m_image_view_control->editImage(
            "Autostretch", "Image modified from autostretch control",
            [settings](const an::consumer::EngineSession& session, const an::consumer::AssetWriteLease& image,
                       std::stop_token stop) {
                return an::ui::applyAutostretchToCopy(session, image.image(), image.validity(), settings,
                                                      std::move(stop));
            },
            [guard](const bool shown) {
                if (!guard)
                {
                    return;
                }
                if (!shown)
                {
                    guard->m_IgnoreLastSettingsUpdated = false;
                    return;
                }
                guard->m_image_view_control->forceLinearMode();
                guard->resetSettings();
                guard->close();
            });
    }
}

void AutostretchSettingsControl::on_brightnessChanged(int value)
{
    settingsUpdated();
}

void AutostretchSettingsControl::on_contrastChanged(int value)
{
    settingsUpdated();
}

void AutostretchSettingsControl::done(int s)
{
    m_image_view_control->setAutostretchSettingsActionEnabled(true);

    QDialog::done(s);
}

void AutostretchSettingsControl::settingsUpdated()
{
    if (!m_IgnoreLastSettingsUpdated)
    {
        AutostretchParams params;

        params.targetBackground = targetBackgroundControl->getValue();
        params.shadowsClipping = shadowsClippingControl->getValue();
        params.linkedChannels = m_ui.linkedChannels->isChecked();
        if (params.linkedChannels)
        {
            params.neutralizeBackground = m_ui.neutralizeBackground->isChecked();
            params.colorCalibration = m_ui.colorCalibration->isChecked();
        }
        else
        {
            params.neutralizeBackground = false;
            params.colorCalibration = false;
        }

        params.saturation = saturationControl->getValue();
        params.saturationBackground = saturationBackgroundControl->getValue();
        params.brightness = brightnessControl->getValue();
        params.contrast = contrastControl->getValue();

        m_DisplayUpdate.request(params);
    }
}

void AutostretchSettingsControl::resetSettings()
{
    m_IgnoreLastSettingsUpdated = true;

    targetBackgroundControl->setValue(an::kDisplayTargetBackground);
    shadowsClippingControl->setValue(an::kDisplayShadowsClipping);
    m_ui.linkedChannels->setChecked(false);
    m_ui.neutralizeBackground->setChecked(false);
    m_ui.colorCalibration->setChecked(false);

    saturationControl->setValue(0.0);
    saturationBackgroundControl->setValue(1.0);

    brightnessControl->setValue(0);
    contrastControl->setValue(0);

    m_IgnoreLastSettingsUpdated = false;

    settingsUpdated();
}

void AutostretchSettingsControl::refreshColorActions()
{
    const std::optional<an::ImageDescriptor> described = m_image_view_control->imageDescriptor();
    const bool isRGB = described.has_value() && described->extent().channelCount() >= 3u;
    m_ui.linkedChannels->setEnabled(isRGB);
    m_ui.neutralizeBackground->setEnabled(isRGB && m_ui.linkedChannels->isChecked());
    m_ui.colorCalibration->setEnabled(isRGB && m_ui.linkedChannels->isChecked());

    saturationControl->setEnabled(isRGB);
    saturationBackgroundControl->setEnabled(isRGB);
}

void AutostretchSettingsControl::on_dialog_box_button_clicked(QAbstractButton* button)
{
    QDialogButtonBox* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
        return;

    QDialogButtonBox::StandardButton sb = buttonBox->standardButton(button);

    if (sb == QDialogButtonBox::Reset)
    {
        resetSettings();
    }
    else if (sb == QDialogButtonBox::Close)
    {
        this->close();
    }
}
