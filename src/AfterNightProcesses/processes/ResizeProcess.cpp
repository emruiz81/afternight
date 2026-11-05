// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ResizeProcess.h"
#include "controls/ProcessButtonIcons.h"
#include "ProcessCommonUtils.h"
#include "controls/ImageViewControl.h"

#include <fmt/format.h>

namespace
{

/// The width and height of the image a view shows; zero for none.
[[nodiscard]] std::pair<int, int> extentOf(const ImageViewControl* view)
{
    const std::optional<an::ImageDescriptor> described = view != nullptr ? view->imageDescriptor() : std::nullopt;
    if (!described.has_value())
    {
        return {0, 0};
    }
    return {static_cast<int>(described->extent().width()), static_cast<int>(described->extent().height())};
}

/// How many images a view's run reads: its sequence's frames, or one.
[[nodiscard]] double imageCountOf(ImageViewControl* view)
{
    return view->isSequence() ? static_cast<double>(view->getSequenceNumOfFrames()) : 1.0;
}

} // namespace

ResizeProcess::ResizeProcess(ImageViewControl* control) : ImageProcessControl(control, getProcessNameStatic())
{
    setFixedSizeWindow(true);
    m_ui = an::ui::processes::buildResizeProcessUi(this);
    setProcessDescription(QStringLiteral("Scale image dimensions by pixels or percentage using interpolation."));

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this, &ResizeProcess::on_dialog_box_button_clicked);

    QObject::connect(m_ui.targetWidth, &QSpinBox::valueChanged, this, &ResizeProcess::on_targetWidthChanged);
    QObject::connect(m_ui.targetHeight, &QSpinBox::valueChanged, this, &ResizeProcess::on_targetHeightChanged);
    QObject::connect(m_ui.targetWidthPercent, &QDoubleSpinBox::valueChanged, this,
                     &ResizeProcess::on_targetWidthPercentChanged);
    QObject::connect(m_ui.targetHeightPercent, &QDoubleSpinBox::valueChanged, this,
                     &ResizeProcess::on_targetHeightPercentChanged);
    QObject::connect(m_ui.keepAspectRatio, &QCheckBox::checkStateChanged, this,
                     &ResizeProcess::on_keepAspectRatioClicked);

    updateWindowTitle();

    m_SupportTargetViewControlSelection = true;
    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    an::ui::processes::fillInterpolationChoices(m_ui.interpolation, an::ui::processes::resizeAutoInterpolationText());

    initialize();
    refreshTheme();
}

std::string ResizeProcess::getProcessNameStatic()
{
    return "Resize";
}

bool ResizeProcess::requiresExclusiveAccessStatic()
{
    return false;
}

ResizeProcess::Settings ResizeProcess::settings() const
{
    Settings asked;
    asked.originalWidth = static_cast<std::uint32_t>(m_ui.originalWidth->value());
    asked.originalHeight = static_cast<std::uint32_t>(m_ui.originalHeight->value());
    asked.width = static_cast<std::uint32_t>(m_ui.targetWidth->value());
    asked.height = static_cast<std::uint32_t>(m_ui.targetHeight->value());
    const bool enlarges = asked.width > asked.originalWidth || asked.height > asked.originalHeight;
    asked.interpolation = an::ui::processes::resolveResizeInterpolation(
        an::ui::processes::selectedInterpolationName(m_ui.interpolation), enlarges);
    return asked;
}

std::string ResizeProcess::stepDescription(const Settings& settings)
{
    return fmt::format("Previous size: {}x{} - New size: {}x{} - Interpolation: {}", settings.originalWidth,
                       settings.originalHeight, settings.width, settings.height,
                       settings.interpolation.label.toStdString());
}

void ResizeProcess::onImageUpdated(ImageViewControl* ctrl)
{
    ImageProcessControl::onImageUpdated(ctrl);

    const auto [width, height] = extentOf(m_image_view_control);
    if (width != m_ui.originalWidth->value() || height != m_ui.originalHeight->value())
    {
        refreshSourceImageData();
        on_targetWidthPercentChanged(m_ui.targetWidthPercent->value());
        on_targetHeightPercentChanged(m_ui.targetHeightPercent->value());
    }
}

void ResizeProcess::onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl)
{
    ImageProcessControl::onViewControlChanged(oldCtrl, newCtrl);

    initialize();
}

void ResizeProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();

    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

ResizeProcess::~ResizeProcess() {}

void ResizeProcess::on_targetWidthChanged(int value)
{
    if (m_IgnoreLastTargetWidth)
        return;

    const auto [width, height] = extentOf(m_image_view_control);
    if (width == 0 || height == 0)
        return;

    m_IgnoreLastTargetWidthPercent = true;
    double percent = ((double)value * 100.0) / (double)width;
    m_ui.targetWidthPercent->setValue(percent);
    m_IgnoreLastTargetWidthPercent = false;

    if (m_ui.keepAspectRatio->isChecked())
    {
        m_IgnoreLastTargetHeight = true;
        m_ui.targetHeight->setValue(static_cast<int>((height * percent) / 100));
        m_IgnoreLastTargetHeight = false;

        m_IgnoreLastTargetHeightPercent = true;
        m_ui.targetHeightPercent->setValue(percent);
        m_IgnoreLastTargetHeightPercent = false;
    }

    recalculateNewSize();
    checkChangesPending();
}

void ResizeProcess::on_targetHeightChanged(int value)
{
    if (m_IgnoreLastTargetHeight)
        return;

    const auto [width, height] = extentOf(m_image_view_control);
    if (width == 0 || height == 0)
        return;

    m_IgnoreLastTargetHeightPercent = true;
    double percent = ((double)value * 100.0) / (double)height;
    m_ui.targetHeightPercent->setValue(percent);
    m_IgnoreLastTargetHeightPercent = false;

    if (m_ui.keepAspectRatio->isChecked())
    {
        m_IgnoreLastTargetWidth = true;
        m_ui.targetWidth->setValue(static_cast<int>((width * percent) / 100));
        m_IgnoreLastTargetWidth = false;

        m_IgnoreLastTargetWidthPercent = true;
        m_ui.targetWidthPercent->setValue(percent);
        m_IgnoreLastTargetWidthPercent = false;
    }

    recalculateNewSize();
    checkChangesPending();
}

void ResizeProcess::on_targetWidthPercentChanged(double value)
{
    if (m_IgnoreLastTargetWidthPercent)
        return;

    const auto [width, height] = extentOf(m_image_view_control);

    m_IgnoreLastTargetWidth = true;
    m_ui.targetWidth->setValue(static_cast<int>((width * value) / 100));
    m_IgnoreLastTargetWidth = false;

    if (m_ui.keepAspectRatio->isChecked())
    {
        m_IgnoreLastTargetHeight = true;
        m_ui.targetHeight->setValue(static_cast<int>((height * value) / 100));
        m_IgnoreLastTargetHeight = false;

        m_IgnoreLastTargetHeightPercent = true;
        m_ui.targetHeightPercent->setValue(value);
        m_IgnoreLastTargetHeightPercent = false;
    }

    recalculateNewSize();
    checkChangesPending();
}

void ResizeProcess::on_targetHeightPercentChanged(double value)
{
    if (m_IgnoreLastTargetHeightPercent)
        return;

    const auto [width, height] = extentOf(m_image_view_control);

    m_IgnoreLastTargetHeight = true;
    m_ui.targetHeight->setValue(static_cast<int>((height * value) / 100));
    m_IgnoreLastTargetHeight = false;

    if (m_ui.keepAspectRatio->isChecked())
    {
        m_IgnoreLastTargetWidth = true;
        m_ui.targetWidth->setValue(static_cast<int>((width * value) / 100));
        m_IgnoreLastTargetWidth = false;

        m_IgnoreLastTargetWidthPercent = true;
        m_ui.targetWidthPercent->setValue(value);
        m_IgnoreLastTargetWidthPercent = false;
    }

    recalculateNewSize();
    checkChangesPending();
}

void ResizeProcess::on_keepAspectRatioClicked(Qt::CheckState state)
{
    if (state == Qt::Checked)
    {
        const auto [width, height] = extentOf(m_image_view_control);
        if (m_ui.targetWidth->value() > m_ui.targetHeight->value())
        {
            // Adjust height
            double percent = m_ui.targetWidthPercent->value();

            m_IgnoreLastTargetHeight = true;
            m_ui.targetHeight->setValue(static_cast<int>((height * percent) / 100));
            m_IgnoreLastTargetHeight = false;

            m_IgnoreLastTargetHeightPercent = true;
            m_ui.targetHeightPercent->setValue(percent);
            m_IgnoreLastTargetHeightPercent = false;
        }
        else
        {
            // Adjust width
            double percent = m_ui.targetHeightPercent->value();

            m_IgnoreLastTargetWidth = true;
            m_ui.targetWidth->setValue(static_cast<int>((width * percent) / 100));
            m_IgnoreLastTargetWidth = false;

            m_IgnoreLastTargetWidthPercent = true;
            m_ui.targetWidthPercent->setValue(percent);
            m_IgnoreLastTargetWidthPercent = false;
        }

        recalculateNewSize();
        checkChangesPending();
    }
}

void ResizeProcess::initialize()
{
    refreshSourceImageData();

    const auto [width, height] = extentOf(m_image_view_control);
    m_ui.targetWidth->setValue(width);
    m_ui.targetHeight->setValue(height);
    m_ui.targetWidthPercent->setValue(100.0);
    m_ui.targetHeightPercent->setValue(100.0);
    m_ui.keepAspectRatio->setChecked(true);
    an::ui::processes::selectInterpolationName(m_ui.interpolation, process_utils::INTERP_AUTO);

    recalculateNewSize();
    checkChangesPending();
}

void ResizeProcess::refreshSourceImageData()
{
    const auto [width, height] = extentOf(m_image_view_control);
    m_ui.originalWidth->setValue(width);
    m_ui.originalHeight->setValue(height);

    const std::optional<an::ImageDescriptor> described = m_image_view_control->imageDescriptor();
    const double bytes = described.has_value() ? static_cast<double>(an::ui::processes::imageBytesOf(*described)) : 0.0;
    m_ui.currentSize->setText(an::ui::processes::sizeText(bytes * imageCountOf(m_image_view_control)));
}

void ResizeProcess::recalculateNewSize()
{
    const std::optional<an::ImageDescriptor> described = m_image_view_control->imageDescriptor();
    const double pixelBytes = described.has_value() ? static_cast<double>(described->extent().channelCount()) *
                                                          an::sampleTypeSizeBytes(described->sampleType())
                                                    : 0.0;
    const double bytes = static_cast<double>(m_ui.targetWidth->value()) * m_ui.targetHeight->value() * pixelBytes;
    m_ui.newSize->setText(an::ui::processes::sizeText(bytes * imageCountOf(m_image_view_control)));
}

void ResizeProcess::checkChangesPending()
{
    auto* okBtn = m_ui.buttonBox->button(QDialogButtonBox::Ok);
    const auto [width, height] = extentOf(m_image_view_control);
    if (m_ExecutionInProgress)
    {
        okBtn->setEnabled(false);
    }
    else if (width != m_ui.targetWidth->value() || height != m_ui.targetHeight->value())
    {
        okBtn->setEnabled(true);
        okBtn->setToolTip(tr("Apply the resize to the target image."));
    }
    else
    {
        okBtn->setEnabled(false);
        okBtn->setToolTip(tr("Target dimensions match the current image. Change width or height to enable."));
    }
}

void ResizeProcess::executeImageResize()
{
    if (m_ExecutionInProgress || !m_image_view_control->hasImage())
    {
        return;
    }
    bool mosaicAsGrey = false;
    if (m_image_view_control->isCFA())
    {
        int ret = QMessageBox::No;
        process_utils::messageBoxYesNo(
            this, "Resize CFA",
            "The file appears to have a bayer matrix. Running this process in a CFA matrix will produce undesired "
            "results. Please consider running a debayer process first, or run the CFA Integer Resample process "
            "instead.\nDo you want to run this process anyway?",
            true, &ret);

        if (ret == QMessageBox::No)
        {
            return;
        }
        mosaicAsGrey = true;
    }

    // The dialog is read here, on the GUI thread; the run reads only this.
    const Settings asked = settings();
    const an::ResizeRequest resize =
        an::ui::processes::resizeRequest(asked.width, asked.height, asked.interpolation.interpolation);

    m_ExecutionInProgress = true;
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(false);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(false);

    LOG_INFO("Executing process '{}' on {} '{}'... ", getProcessName(),
             m_image_view_control->isSequence() ? "sequence" : "image", m_image_view_control->getUniqueName());

    an::ui::processes::InPlaceRunRequest request;
    request.words = an::ui::processes::GeometryRunWords{
        .title = QStringLiteral("Resize"),
        .progressText = QString::fromStdString(
            fmt::format("Executing resize to sequence {}", m_image_view_control->getUniqueName())),
        .errorTitle = QStringLiteral("Error running resize"),
        .errorLead = "An error occurred when executing the resize process"};
    request.stepName = getProcessName();
    request.stepDescription = stepDescription(asked);
    request.operation = [resize, mosaicAsGrey](const an::ui::ExecutionScope& scope,
                                               const an::ui::ProcessSource& source) {
        const std::array<an::ui::processes::GeometryStep, 1u> steps{resize};
        return an::ui::processes::transformImage(scope, source, steps, "Resize", mosaicAsGrey);
    };
    request.inFlight = beginRunInFlight();
    request.admissionHook = admissionHook();
    QPointer<ResizeProcess> guard(this);
    an::ui::processes::runInPlace(*m_image_view_control, std::move(request),
                                  [guard](const an::ui::processes::GeometryRunOutcome& outcome) {
                                      if (guard)
                                      {
                                          guard->finishRun(outcome);
                                      }
                                  });
}

void ResizeProcess::finishRun(const an::ui::processes::GeometryRunOutcome& outcome)
{
    m_ExecutionInProgress = false;
    if (outcome.published)
    {
        LOG_INFO("Process '{}' executed successfully on '{}'", getProcessName(),
                 m_image_view_control != nullptr ? m_image_view_control->getUniqueName() : std::string());
    }
    if (outcome.published || m_ProcessCloseRequested)
    {
        close();
        return;
    }
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(true);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(true);
    checkChangesPending();
}

void ResizeProcess::on_dialog_box_button_clicked(QAbstractButton* button)
{
    QDialogButtonBox* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
        return;

    QDialogButtonBox::StandardButton sb = buttonBox->standardButton(button);

    if (sb == QDialogButtonBox::Ok)
    {
        executeImageResize();
    }
    else if (sb == QDialogButtonBox::Close)
    {
        this->close();
    }
    else if (sb == QDialogButtonBox::Reset)
    {
        initialize();
    }
}
