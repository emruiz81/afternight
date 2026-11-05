// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "LinearMatchProcess.h"
#include "controls/ProcessButtonIcons.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageSelectorTooltipBuilder.h"
#include "processes/ProcessCommonUtils.h"

#include <fmt/format.h>

namespace
{

void addReferenceImageItem(QComboBox* selector, const ImageViewControl* control)
{
    if (!selector || !control)
    {
        return;
    }

    selector->addItem(QString::fromUtf8(control->getUniqueName()));
    selector->setItemData(selector->count() - 1, an::ui::buildImageSelectorTooltip(control), Qt::ToolTipRole);
}

} // anonymous namespace

LinearMatchProcess::LinearMatchProcess(ImageViewControl* control) : ImageProcessControl(control, getProcessNameStatic())
{
    setFixedSizeWindow(true);
    setWindowSizeFitMultipliers(1.1, 1.0);

    m_ui = an::ui::processes::buildLinearMatchProcessUi(this);
    setProcessDescription(QStringLiteral("Match the target image linear response to a selected reference image."));

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.referenceImageSelector, &QComboBox::currentTextChanged, this,
                     &LinearMatchProcess::on_referenceImageSelector_currentTextChanged);
    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &LinearMatchProcess::on_dialog_box_button_clicked);

    INIT_D_SPINBOX_CTRL(rejectLowControl, m_ui.rejectLowValue, m_ui.rejectLowSlider,
                        &LinearMatchProcess::on_rejectLowChanged);
    INIT_D_SPINBOX_CTRL(rejectHighControl, m_ui.rejectHighValue, m_ui.rejectHighSlider,
                        &LinearMatchProcess::on_rejectHighChanged);

    updateWindowTitle();

    m_SupportTargetViewControlSelection = true;
    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    initialize();

    refreshTheme();
}

void LinearMatchProcess::onImageUpdated(ImageViewControl* ctrl)
{
    ImageProcessControl::onImageUpdated(ctrl);
    if (ctrl == m_image_view_control)
    {
        rebuildReferenceImageSelector(m_ui.referenceImageSelector->currentText());
    }
}

void LinearMatchProcess::onViewControlUpdated(const ImageViewControl* ctrl)
{
    ImageProcessControl::onViewControlUpdated(ctrl);
    rebuildReferenceImageSelector(m_ui.referenceImageSelector->currentText());
}

void LinearMatchProcess::onViewControlCreated(const ImageViewControl* ctrl)
{
    ImageProcessControl::onViewControlCreated(ctrl);
    if (ctrl != m_image_view_control && imageIsCompatible(ctrl))
    {
        addReferenceImageItem(m_ui.referenceImageSelector, ctrl);
    }
}

void LinearMatchProcess::onViewControlDestroyed(const ImageViewControl* ctrl)
{
    ImageProcessControl::onViewControlDestroyed(ctrl);
    auto imageName = QString::fromUtf8(ctrl->getUniqueName());
    int idx = m_ui.referenceImageSelector->findText(imageName);
    if (idx != -1)
    {
        if (m_ui.referenceImageSelector->currentText() == imageName)
        {
            m_ui.referenceImageSelector->setCurrentText(NO_IMAGE_STR);
        }

        m_ui.referenceImageSelector->removeItem(idx);
    }
    updateOkButton();
}

void LinearMatchProcess::onViewControlRenamed(const std::string& newName, const std::string& oldName,
                                              const ImageViewControl* ctrl)
{
    ImageProcessControl::onViewControlRenamed(newName, oldName, ctrl);
    auto newImageName = QString::fromUtf8(newName);
    auto oldImageName = QString::fromUtf8(oldName);

    int idx = m_ui.referenceImageSelector->findText(oldImageName);
    if (idx != -1)
    {
        m_ui.referenceImageSelector->setItemText(idx, newImageName);
        m_ui.referenceImageSelector->setItemData(idx, an::ui::buildImageSelectorTooltip(ctrl), Qt::ToolTipRole);
    }
}

void LinearMatchProcess::onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl)
{
    if (!isViewControlSupported(newCtrl))
    {
        close();
    }
    else
    {
        ImageProcessControl::onViewControlChanged(oldCtrl, newCtrl);
        initialize();
    }
}

std::string LinearMatchProcess::getProcessNameStatic()
{
    return "Linear Match";
}

bool LinearMatchProcess::requiresExclusiveAccessStatic()
{
    return false;
}

void LinearMatchProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

LinearMatchProcess::~LinearMatchProcess() {}

void LinearMatchProcess::initialize()
{
    rebuildReferenceImageSelector();
}

void LinearMatchProcess::rebuildReferenceImageSelector(const QString& preferredSelection)
{
    const QString desiredSelection = preferredSelection.isEmpty() ? NO_IMAGE_STR : preferredSelection;

    const QSignalBlocker blocker(m_ui.referenceImageSelector);
    m_ui.referenceImageSelector->clear();
    m_ui.referenceImageSelector->addItem(NO_IMAGE_STR);

    for (auto* viewControl : process_utils::listViewControls())
    {
        if (viewControl == nullptr || viewControl == m_image_view_control || !imageIsCompatible(viewControl))
        {
            continue;
        }

        addReferenceImageItem(m_ui.referenceImageSelector, viewControl);
    }

    const int selectionIndex = m_ui.referenceImageSelector->findText(desiredSelection);
    m_ui.referenceImageSelector->setCurrentIndex(selectionIndex != -1 ? selectionIndex : 0);
    updateOkButton();
}

void LinearMatchProcess::updateOkButton()
{
    m_ui.buttonBox->button(QDialogButtonBox::Ok)
        ->setEnabled(!m_ExecutionInProgress && m_ui.referenceImageSelector->currentText() != NO_IMAGE_STR);
}

LinearMatchProcess::Settings LinearMatchProcess::settings() const
{
    return Settings{.referenceName = m_ui.referenceImageSelector->currentText(),
                    .range = an::ui::processes::FractionRange{.lowest = rejectLowControl->getValue(),
                                                              .highest = rejectHighControl->getValue()}};
}

std::string LinearMatchProcess::stepDescription(const Settings& settings, const std::string& targetName,
                                                const bool sequence)
{
    return fmt::format("Target {} '{}', Reference image '{}', Reject Low: {}, Reject High: {}",
                       sequence ? "sequence" : "image", targetName, settings.referenceName.toStdString(),
                       settings.range.lowest, settings.range.highest);
}

ImageViewControl* LinearMatchProcess::referenceViewNamed(const QString& name) const
{
    if (name == NO_IMAGE_STR || m_image_view_control == nullptr ||
        name == QString::fromStdString(m_image_view_control->getUniqueName()))
    {
        return nullptr;
    }
    return process_utils::findViewControlByName(name);
}

void LinearMatchProcess::executeLinearMatch()
{
    if (m_ExecutionInProgress || m_image_view_control == nullptr || !m_image_view_control->hasImage())
    {
        return;
    }
    // The dialog and the reference's image are read here, on the GUI thread; the run reads only these.
    const Settings asked = settings();
    ImageViewControl* referenceView = referenceViewNamed(asked.referenceName);
    if (referenceView == nullptr || !referenceView->hasImage())
    {
        notifyErrorMessage(QStringLiteral("Linear Match"), tr("The selected reference image is no longer available."));
        return;
    }
    const std::shared_ptr<const an::consumer::ImageAsset> reference = referenceView->document().asset();

    m_ExecutionInProgress = true;
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(false);

    const bool sequence = m_image_view_control->isSequence();
    LOG_INFO("Executing process '{}' on {} '{}'... ", getProcessName(), sequence ? "sequence" : "image",
             m_image_view_control->getUniqueName());

    an::ui::processes::InPlaceRunRequest request;
    request.words = an::ui::processes::GeometryRunWords{
        .title = QStringLiteral("Linear Match"),
        .progressText = QString::fromStdString(
            fmt::format("Executing linear match to sequence {}", m_image_view_control->getUniqueName())),
        .errorTitle = QStringLiteral("Error running linear match"),
        .errorLead = "An error occurred when executing the linear match process"};
    request.stepName = getProcessName();
    request.stepDescription = stepDescription(asked, m_image_view_control->getUniqueName(), sequence);
    request.operation =
        [reference, range = asked.range](const an::ui::ExecutionScope& scope,
                                         const an::ui::ProcessSource& source) -> an::Result<an::consumer::ImageAsset> {
        an::Result<an::ui::processes::ReferenceRead> read =
            an::ui::processes::ReferenceRead::read(scope, an::ui::processes::ToolReference{.asset = reference}, source);
        if (!read.hasValue())
        {
            return read.status();
        }
        return an::ui::processes::matchLinearly(scope, source, read->source(), range);
    };
    // The reference is another view's image, leased before the run is admitted.
    request.reads = {reference};
    request.inFlight = beginRunInFlight();
    request.admissionHook = admissionHook();
    QPointer<LinearMatchProcess> guard(this);
    an::ui::processes::runInPlace(*m_image_view_control, std::move(request),
                                  [guard](const an::ui::processes::GeometryRunOutcome& outcome) {
                                      if (guard)
                                      {
                                          guard->finishRun(outcome);
                                      }
                                  });
}

void LinearMatchProcess::finishRun(const an::ui::processes::GeometryRunOutcome& outcome)
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
    updateOkButton();
}

bool LinearMatchProcess::imageIsCompatible(const ImageViewControl* candidate) const
{
    const std::uint32_t channels = an::ui::processes::channelCountOf(m_image_view_control);
    return channels != 0u && an::ui::processes::channelCountOf(candidate) == channels;
}

void LinearMatchProcess::on_referenceImageSelector_currentTextChanged(const QString&)
{
    updateOkButton();
}

void LinearMatchProcess::on_rejectLowChanged(double) {}

void LinearMatchProcess::on_rejectHighChanged(double) {}

void LinearMatchProcess::on_dialog_box_button_clicked(QAbstractButton* button)
{
    QDialogButtonBox* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
        return;

    QDialogButtonBox::StandardButton sb = buttonBox->standardButton(button);

    if (sb == QDialogButtonBox::Ok)
    {
        executeLinearMatch();
    }
    else if (sb == QDialogButtonBox::Close)
    {
        this->close();
    }
}
