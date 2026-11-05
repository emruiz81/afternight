// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ExtractSequenceProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/ProcessCommonUtils.h"
#include "controls/ProcessButtonIcons.h"

#include <fmt/format.h>

ExtractSequenceProcess::ExtractSequenceProcess(ImageViewControl* control)
    : ImageProcessControl(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildExtractSequenceProcessUi(this);
    setProcessDescription(QStringLiteral("Create a new sequence from a selected frame range."));

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &ExtractSequenceProcess::on_dialog_box_button_clicked);

    INIT_SPINBOX_CTRL(startFrameControl, m_ui.startFrameValue, m_ui.startFrameSlider,
                      &ExtractSequenceProcess::on_startFrameChanged);
    INIT_SPINBOX_CTRL(endFrameControl, m_ui.endFrameValue, m_ui.endFrameSlider,
                      &ExtractSequenceProcess::on_endFrameChanged);

    updateWindowTitle();

    updateControls();
    endFrameControl->setValue(static_cast<int>(m_image_view_control->getSequenceNumOfFrames()));

    refreshTheme();
}

std::string ExtractSequenceProcess::getProcessNameStatic()
{
    return "Extract Sequence";
}

bool ExtractSequenceProcess::requiresExclusiveAccessStatic()
{
    return false;
}

ExtractSequenceProcess::Settings ExtractSequenceProcess::settings() const
{
    return Settings{.firstFrame = static_cast<std::uint64_t>(startFrameControl->getValue()),
                    .lastFrame = static_cast<std::uint64_t>(endFrameControl->getValue())};
}

void ExtractSequenceProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

void ExtractSequenceProcess::onImageUpdated(ImageViewControl* ctrl)
{
    ImageProcessControl::onImageUpdated(ctrl);
    updateControls();
}

ExtractSequenceProcess::~ExtractSequenceProcess() {}

void ExtractSequenceProcess::on_startFrameChanged(int value)
{
    if (value > endFrameControl->getValue())
    {
        startFrameControl->setValue(endFrameControl->getValue());
    }
    else
    {
        updateSizeAndFrameCount();
    }
}

void ExtractSequenceProcess::on_endFrameChanged(int value)
{
    if (value < startFrameControl->getValue())
    {
        endFrameControl->setValue(startFrameControl->getValue());
    }
    else
    {
        updateSizeAndFrameCount();
    }
}

void ExtractSequenceProcess::updateControls()
{
    const size_t numOfFrames = m_image_view_control->getSequenceNumOfFrames();
    startFrameControl->setMinimum(1);
    startFrameControl->setMaximum((int)numOfFrames);

    endFrameControl->setMinimum(1);
    endFrameControl->setMaximum((int)numOfFrames);
}

void ExtractSequenceProcess::updateSizeAndFrameCount()
{
    size_t frameCount = endFrameControl->getValue() - startFrameControl->getValue() + 1;
    m_ui.selectedFramesLabel->setText(QString(" %1").arg(frameCount));

    const std::optional<an::ImageDescriptor> described = m_image_view_control->imageDescriptor();
    const double frameBytes =
        described.has_value() ? static_cast<double>(an::ui::processes::imageBytesOf(*described)) : 0.0;
    double sequenceSizeMBytes = ((frameBytes * static_cast<double>(frameCount)) / 1024.0) / 1024.0;

    if (sequenceSizeMBytes > 1024)
    {
        m_ui.sequenceSizeLabel->setText(QString(" %1 GB").arg(sequenceSizeMBytes / 1024.0, 0, 'f', 2));
    }
    else
    {
        m_ui.sequenceSizeLabel->setText(QString(" %1 MB").arg(sequenceSizeMBytes, 0, 'f', 2));
    }
}

void ExtractSequenceProcess::executeExtractSequence()
{
    if (m_ExecutionInProgress || !m_image_view_control->isSequence())
    {
        return;
    }
    // The range is read here, on the GUI thread; the run reads only this.
    const Settings asked = settings();
    const std::string name = m_image_view_control->getUniqueName();

    an::ui::processes::NewViewsRunRequest request;
    request.words = an::ui::processes::GeometryRunWords{
        .title = QStringLiteral("Extract Sequence"),
        .progressText = QString::fromStdString(fmt::format("Extracting new sequence from sequence {}", name)),
        .errorTitle = QStringLiteral("Error running Extract Sequence"),
        .errorLead = "An error occurred when executing the Extract Sequence process"};
    request.sequenceOutputs.push_back(
        {.name = name,
         .history = fmt::format("Extract Sequence: Extracted frames {} to {} from sequence '{}'", asked.firstFrame,
                                asked.lastFrame, name)});
    std::vector<std::uint64_t> frames;
    for (std::uint64_t frame = asked.firstFrame; frame <= asked.lastFrame; ++frame)
    {
        frames.push_back(frame - 1u);
    }
    request.frames = std::move(frames);
    // The frames are copied as recorded; the new sequence is shown as this one is.
    request.readDemosaiced = false;
    request.showDemosaiced = m_image_view_control->isSequenceAutodebayerEnabled();
    request.inFlight = beginRunInFlight();
    request.admissionHook = admissionHook();

    m_ExecutionInProgress = true;
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(false);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(false);

    LOG_INFO("Executing process '{}' on sequence '{}'... ", getProcessName(), name);

    QPointer<ExtractSequenceProcess> guard(this);
    an::ui::processes::runToNewViews(*m_image_view_control, std::move(request),
                                     [guard](const an::ui::processes::GeometryRunOutcome& outcome) {
                                         if (guard)
                                         {
                                             guard->finishRun(outcome);
                                         }
                                     });
}

void ExtractSequenceProcess::finishRun(const an::ui::processes::GeometryRunOutcome& outcome)
{
    m_ExecutionInProgress = false;
    if (outcome.status.ok() && outcome.published)
    {
        LOG_INFO("Process '{}' executed successfully on sequence '{}'", getProcessName(),
                 m_image_view_control != nullptr ? m_image_view_control->getUniqueName() : std::string());
    }
    if ((outcome.status.ok() && outcome.published) || m_ProcessCloseRequested)
    {
        close();
        return;
    }
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(true);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(true);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(true);
}

bool ExtractSequenceProcess::isViewControlSupported(const ImageViewControl* ctrl)
{
    return ctrl->isSequence();
}

void ExtractSequenceProcess::on_dialog_box_button_clicked(QAbstractButton* button)
{
    QDialogButtonBox* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
        return;

    QDialogButtonBox::StandardButton sb = buttonBox->standardButton(button);

    if (sb == QDialogButtonBox::Ok)
    {
        executeExtractSequence();
    }
    else if (sb == QDialogButtonBox::Reset)
    {
        updateControls();
        startFrameControl->setValue(1);
        endFrameControl->setValue(static_cast<int>(m_image_view_control->getSequenceNumOfFrames()));
        startFrameControl->setValue(1);
    }
    else if (sb == QDialogButtonBox::Close)
    {
        this->close();
    }
}
