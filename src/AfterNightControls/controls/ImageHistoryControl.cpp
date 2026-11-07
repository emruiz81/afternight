// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The window over one view's history: the timeline, the preview read and presented on a worker, and the actions.

#include "stdafx.hpp"
#include "ImageHistoryControl.h"

#include "ImageHistoryTrackerUtils.h"
#include "app/FileDialogUtils.h"
#include "controls/AutostretchApply.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessCompanions.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"
#include "theme/AnDesignTokens.h"
#include "widgets/ImagePreviewWidget.h"
#include "widgets/PresentationBridge.h"
#include "widgets/ViewPresentation.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Presentation/DisplayPresentation.h>
#include <AfterNightEngine/Core/ErrorCode.h>

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QImage>
#include <QMenu>
#include <QMetaObject>
#include <QPixmap>
#include <QPointer>
#include <QSignalBlocker>
#include <QTableWidgetItem>
#include <QThreadPool>

#include <algorithm>
#include <utility>

namespace
{

[[nodiscard]] an::ui::IIconProviderPtr currentIconProvider()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getIconProvider();
    }

    return {};
}

[[nodiscard]] an::ui::IDialogService* currentDialogService()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getDialogService();
    }

    return nullptr;
}

[[nodiscard]] an::ui::IGlobalSettingsProvider* currentGlobalSettings()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getGlobalSettings();
    }

    return nullptr;
}

[[nodiscard]] an::ui::IPathMemoryService* currentPathMemoryService()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getPathMemoryService();
    }

    return nullptr;
}

[[nodiscard]] an::ui::IViewFactory* currentViewFactory()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getViewFactory();
    }

    return nullptr;
}

[[nodiscard]] const an::consumer::EngineSession* currentSession()
{
    auto* context = an::ui::appContext();
    auto* provider = context != nullptr ? context->getEngineSessionProvider() : nullptr;
    return provider != nullptr && provider->engineSession().valid() ? &provider->engineSession() : nullptr;
}

[[nodiscard]] QString stepTitleText(const ImageHistoryStepSPtr& step, const bool isCurrentStep)
{
    if (!step)
    {
        return QObject::tr("No Step Selected");
    }

    const QString baseTitle = QString::fromUtf8(step->getStepName().c_str());
    if (isCurrentStep)
    {
        return QObject::tr("%1 (Current)").arg(baseTitle);
    }

    return baseTitle;
}

/// The selected step's details: when it was recorded, whether it is the current step, whether it has a preview, and
/// last its description, the one part of any length - the part the details' lines cut when it is long.
[[nodiscard]] QString stepSubtitleText(const ImageHistoryStepSPtr& step, const bool isCurrentStep,
                                       const bool previewAvailable)
{
    if (!step)
    {
        return QObject::tr("Select a history row to inspect its preview, histogram, and actions.");
    }

    QStringList lines;
    lines << step->m_CreationTime.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));

    lines << (isCurrentStep
                  ? QObject::tr("Active step in the current image.")
                  : QObject::tr("Previewing a historical step. Double-click or use the context menu to jump."));

    if (!previewAvailable)
    {
        lines << QObject::tr("Preview unavailable for this step.");
    }

    const QString description = QString::fromUtf8(step->getStepDesc().c_str()).trimmed();
    if (!description.isEmpty())
    {
        lines << description;
    }

    return lines.join(QStringLiteral("\n"));
}

/// A step's image and the bytes that preview it.
struct StepPreview
{
    std::shared_ptr<const an::consumer::ImageAsset> image;
    std::shared_ptr<const an::ui::PresentedImage> shown;
};

/// How a step is shown: linear data is stretched as the recent files' thumbnails stretch it, each colour channel on its
/// own, so a linear step looks as the dashboard and the view's autostretch show it; anything else is shown as it is,
/// every channel at full scale and no stretch.
[[nodiscard]] an::ui::ViewPresentationSettings stepPresentation(const bool linear)
{
    an::ui::ViewPresentationSettings settings;
    if (linear)
    {
        an::ui::AutostretchApplySettings stretch;
        stretch.linkedChannels = false;
        settings.request = an::ui::autostretchApplyRequest(stretch);
        settings.request.output = an::PresentationOutput::Interleaved8;
    }
    else
    {
        settings.request.stretch = an::PresentationStretch::Linear;
    }
    return settings;
}

/// Reads a step and presents it as `stepPresentation` says, with the presentation's own channel layout, so a colour
/// image with alpha is never sheared.
[[nodiscard]] an::Result<StepPreview> previewStep(const an::consumer::EngineSession& session,
                                                  const ImageHistoryTracker::StepReader& read, std::stop_token stop)
{
    an::Result<an::consumer::ImageAsset> image = read(stop);
    if (!image.hasValue())
    {
        return image.status();
    }
    auto held = std::make_shared<const an::consumer::ImageAsset>(std::move(*image));
    an::Result<an::consumer::OperationScope> scope = session.admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Preview, .stopToken = stop});
    if (!scope.hasValue())
    {
        return scope.status();
    }
    an::Result<an::consumer::AssetReadLease> lease = held->read(stop);
    if (!lease.hasValue())
    {
        return lease.status();
    }
    const bool linear = lease->visit([](const auto& typed) { return an::ui::isLinearData(typed.metadata()); });
    an::Result<an::ui::ViewPresentationOutcome> presented = an::ui::presentForView(
        scope->engine(), scope->execution(), *lease, stepPresentation(linear), nullptr, &session.memory());
    if (!presented.hasValue())
    {
        return presented.status();
    }
    return StepPreview{.image = std::move(held), .shown = std::move(presented->image)};
}

} // namespace

ImageHistoryControl::ImageHistoryControl(ImageViewControl* control, const std::shared_ptr<ImageHistoryTracker>& tracker)
    : QDialog(control), ImageViewControlListener(control), m_Tracker(tracker)
{
    m_ui = an::ui::controls::buildImageHistoryControlUi(this);

    setAttribute(Qt::WA_DeleteOnClose);

    m_stepPreviewWidget = new ImagePreviewWidget(m_ui.previewWidgetHost);
    m_stepPreviewWidget->setObjectName(QStringLiteral("historyStepPreviewWidget"));
    m_stepPreviewWidget->setOwnerWidget(this);
    m_stepPreviewWidget->setQuickZoomEnabled(false);
    m_ui.previewWidgetHostLayout->addWidget(m_stepPreviewWidget);

    m_stepHistogram = an::ui::createPreviewHistogram(m_ui.histogramScrollArea);
    if (m_stepHistogram)
    {
        if (QWidget* histogram = m_stepHistogram->widget())
        {
            histogram->setObjectName(QStringLiteral("historyStepHistogramWidget"));
        }
        m_stepHistogram->setClippingInfoEnabled(true);
        m_stepHistogram->setAreaModeEnabled(true);
    }

    connect(m_ui.refreshButton, &QPushButton::released, this, &ImageHistoryControl::on_refreshButtonPushed);
    connect(m_ui.closeButton, &QPushButton::released, this, &ImageHistoryControl::on_closeButtonPushed);
    connect(m_ui.moveUpButton, &QPushButton::released, this, &ImageHistoryControl::on_moveUpButtonPushed);
    connect(m_ui.moveDownButton, &QPushButton::released, this, &ImageHistoryControl::on_moveDownButtonPushed);
    connect(m_ui.toggleLineageButton, &QPushButton::released, this, &ImageHistoryControl::on_toggleLineagePushed);
    connect(m_ui.saveLogButton, &QPushButton::released, this, &ImageHistoryControl::saveHistoryLog);
    connect(m_ui.tableWidget, &QTableWidget::itemSelectionChanged, this,
            &ImageHistoryControl::on_historyTableSelectionChanged);
    connect(m_ui.tableWidget, &QTableWidget::cellDoubleClicked, this,
            &ImageHistoryControl::on_historyTableDoubleClicked);
    connect(m_ui.tableWidget, &QWidget::customContextMenuRequested, this,
            &ImageHistoryControl::on_historyContextMenuRequested);

    std::string title = fmt::format("Image History <{}>", m_image_view_control->getUniqueName());
    setWindowTitle(QString::fromUtf8(title.c_str()));

    initialize();

    if (const auto held = this->tracker())
    {
        held->addListener(this);
    }
    recalculate();
}

void ImageHistoryControl::onImageRenamed(ImageViewControl* ctrl)
{
    Q_UNUSED(ctrl)

    std::string title = fmt::format("Image History <{}>", m_image_view_control->getUniqueName());
    setWindowTitle(QString::fromUtf8(title.c_str()));
}

void ImageHistoryControl::onImageHistoryUpdated()
{
    recalculate();
}

void ImageHistoryControl::recalculate()
{
    refillData();
}

std::uint64_t ImageHistoryControl::previewsShown() const
{
    return m_PreviewsShown;
}

std::shared_ptr<const an::ui::PresentedImage> ImageHistoryControl::shownPreview() const
{
    return m_ShownPreview;
}

ImageHistoryControl::~ImageHistoryControl()
{
    m_PreviewStop.request_stop();
    if (const auto held = tracker())
    {
        held->removeListener(this);
    }
}

std::shared_ptr<ImageHistoryTracker> ImageHistoryControl::tracker() const
{
    return m_Tracker.lock();
}

void ImageHistoryControl::on_refreshButtonPushed()
{
    recalculate();
}

void ImageHistoryControl::on_closeButtonPushed()
{
    close();
}

void ImageHistoryControl::on_moveUpButtonPushed()
{
    const auto held = tracker();
    if (!held)
    {
        return;
    }

    const auto previousStep = an::ui::history::previousStep(held->getCurrentStep());
    if (previousStep.has_value())
    {
        (void)held->jumpToStep(*previousStep);
    }
}

void ImageHistoryControl::on_moveDownButtonPushed()
{
    const auto held = tracker();
    if (!held)
    {
        return;
    }

    const auto nextStep = an::ui::history::nextStep(held->getTotalStepCount(), held->getCurrentStep());
    if (nextStep.has_value())
    {
        (void)held->jumpToStep(*nextStep);
    }
}

void ImageHistoryControl::on_toggleLineagePushed()
{
    m_LineageVisible = m_ui.toggleLineageButton->isChecked();
    recalculate();
}

void ImageHistoryControl::on_historyTableSelectionChanged()
{
    updateSelectedStepPreview();
}

void ImageHistoryControl::on_historyTableDoubleClicked(const int row, const int column)
{
    Q_UNUSED(column)

    if (row < 0)
    {
        return;
    }

    m_ui.tableWidget->selectRow(row);
    jumpToSelectedStep();
}

void ImageHistoryControl::on_historyContextMenuRequested(const QPoint& pos)
{
    const int row = m_ui.tableWidget->rowAt(pos.y());
    if (row < 0)
    {
        return;
    }

    m_ui.tableWidget->selectRow(row);

    const auto held = tracker();
    const int stepRow = row - m_LineageRowCount;
    QMenu menu(this);
    QAction* jumpAction = menu.addAction(tr("Jump To Step"));
    QAction* cloneAction = menu.addAction(tr("Clone From Step"));
    QAction* removeAction = menu.addAction(tr("Remove Step"));
    menu.addSeparator();
    QAction* saveLogAction = menu.addAction(tr("Save History Log..."));

    jumpAction->setEnabled(stepRow >= 0 && held && held->canJumpToStep(static_cast<size_t>(stepRow)));
    cloneAction->setEnabled(stepRow >= 0 && getStepForRow(row) != nullptr);
    removeAction->setEnabled(stepRow >= 0 && held && held->canRemoveStep(static_cast<size_t>(stepRow)));
    saveLogAction->setEnabled(held && held->getTotalStepCount() > 0);

    QAction* selectedAction = menu.exec(m_ui.tableWidget->viewport()->mapToGlobal(pos));
    if (selectedAction == jumpAction)
    {
        jumpToSelectedStep();
    }
    else if (selectedAction == cloneAction)
    {
        cloneSelectedStep();
    }
    else if (selectedAction == removeAction)
    {
        removeSelectedStep();
    }
    else if (selectedAction == saveLogAction)
    {
        saveHistoryLog();
    }
}

void ImageHistoryControl::initialize()
{
    auto* horizontalHeader = m_ui.tableWidget->horizontalHeader();
    auto iconProvider = currentIconProvider();

    horizontalHeader->setSectionResizeMode(QHeaderView::Interactive);
    horizontalHeader->setStretchLastSection(true);

    m_ui.tableWidget->setAlternatingRowColors(true);
    m_ui.tableWidget->setColumnCount(3);
    m_ui.tableWidget->setHorizontalHeaderLabels({"Step", "Executed", "Description"});
    m_ui.tableWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    m_ui.tableWidget->verticalHeader()->setVisible(false);
    m_ui.moveUpButton->setIconSize(QSize(18, 18));
    m_ui.moveDownButton->setIconSize(QSize(18, 18));
    m_ui.toggleLineageButton->setIconSize(QSize(18, 18));
    m_ui.saveLogButton->setIconSize(QSize(18, 18));

    if (iconProvider)
    {
        m_ui.moveUpButton->setIcon(iconProvider->getIcon("up"));
        m_ui.moveDownButton->setIcon(iconProvider->getIcon("down"));
        m_ui.toggleLineageButton->setIcon(iconProvider->getIcon("eye"));
        m_ui.saveLogButton->setIcon(iconProvider->getIcon("file_save"));
    }

    m_ui.toggleLineageButton->setToolTip(tr("Show/hide inherited processing history from parent images"));
    m_ui.saveLogButton->setToolTip(tr("Save the complete history log to a text file"));

    clearSelectedStepPreview();
    updateNavigationButtons();
}

void ImageHistoryControl::fillTableItem(const int row, const ImageHistoryStepSPtr& step, const bool isCurrentStep)
{
    auto* nameItem = new QTableWidgetItem(QString::fromUtf8(step->getStepName().c_str()));
    auto* timeItem = new QTableWidgetItem(step->m_CreationTime.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")));
    auto* descriptionItem = new QTableWidgetItem(QString::fromUtf8(step->getStepDesc().c_str()));

    if (isCurrentStep)
    {
        QFont currentFont = nameItem->font();
        currentFont.setBold(true);
        nameItem->setFont(currentFont);
        timeItem->setFont(currentFont);
        descriptionItem->setFont(currentFont);

        const QString tooltip = tr("Current active step");
        nameItem->setToolTip(tooltip);
        timeItem->setToolTip(tooltip);
        descriptionItem->setToolTip(tooltip);
    }

    m_ui.tableWidget->setItem(row, 0, nameItem);
    m_ui.tableWidget->setItem(row, 1, timeItem);
    m_ui.tableWidget->setItem(row, 2, descriptionItem);
}

void ImageHistoryControl::refillData()
{
    const auto held = tracker();
    if (!held)
    {
        m_ui.tableWidget->clearContents();
        m_ui.tableWidget->clearSpans();
        m_ui.tableWidget->setRowCount(0);
        m_LineageRowCount = 0;
        clearSelectedStepPreview();
        updateNavigationButtons();
        return;
    }

    QSignalBlocker blocker(m_ui.tableWidget);

    m_ui.tableWidget->clearContents();
    m_ui.tableWidget->clearSpans();
    m_ui.tableWidget->setSortingEnabled(false);

    const auto& lineage = held->lineage();
    const auto& lineageEntries = lineage.entries();
    const bool hasLineage = !lineageEntries.empty();
    const bool showLineage = hasLineage && m_LineageVisible;

    m_ui.toggleLineageButton->setVisible(hasLineage);
    m_ui.toggleLineageButton->setToolTip(m_LineageVisible ? tr("Hide inherited processing history from parent images")
                                                          : tr("Show inherited processing history from parent images"));

    const auto& t = an::ui::tokens();
    const QColor lineageBg = t.colors.bgCardSecondary;
    const QColor lineageText = t.colors.textSecondary;
    const QColor sectionBg = t.colors.bgSurface;
    const QColor sectionText = t.colors.textTertiary;

    m_LineageRowCount = 0;

    // Count lineage rows: section headers + entry rows
    int lineageDisplayRows = 0;
    if (showLineage)
    {
        // Group entries by image_name to insert section headers
        std::string prevImageName;
        for (const auto& entry : lineageEntries)
        {
            if (entry.image_name != prevImageName)
            {
                ++lineageDisplayRows; // section header row
                prevImageName = entry.image_name;
            }
            ++lineageDisplayRows; // entry row
        }
        // Separator row between lineage and current history
        ++lineageDisplayRows;
    }

    m_LineageRowCount = lineageDisplayRows;

    const auto steps = held->getImageHistorySteps();
    const auto totalSteps = held->getTotalStepCount();
    const auto currentStep = held->getCurrentStep();

    m_ui.tableWidget->setRowCount(m_LineageRowCount + static_cast<int>(totalSteps));

    if (showLineage)
    {
        int tableRow = 0;
        std::string prevImageName;
        for (const auto& entry : lineageEntries)
        {
            // Insert section header when image name changes
            if (entry.image_name != prevImageName)
            {
                prevImageName = entry.image_name;

                const QString sectionLabel = entry.image_name.empty()
                                                 ? tr("Inherited History")
                                                 : tr("Inherited from \"%1\"").arg(QString::fromUtf8(entry.image_name));

                auto* sectionItem = new QTableWidgetItem(QStringLiteral("  ▸ ") + sectionLabel);
                QFont sectionFont = sectionItem->font();
                sectionFont.setBold(true);
                sectionFont.setItalic(true);
                sectionItem->setFont(sectionFont);
                sectionItem->setForeground(sectionText);
                sectionItem->setBackground(sectionBg);
                sectionItem->setFlags(Qt::ItemIsEnabled);

                m_ui.tableWidget->setItem(tableRow, 0, sectionItem);

                // Fill remaining columns with empty styled items
                for (int col = 1; col < 3; ++col)
                {
                    auto* filler = new QTableWidgetItem();
                    filler->setBackground(sectionBg);
                    filler->setFlags(Qt::ItemIsEnabled);
                    m_ui.tableWidget->setItem(tableRow, col, filler);
                }
                m_ui.tableWidget->setSpan(tableRow, 0, 1, 3);
                ++tableRow;
            }

            // Entry row
            auto* nameItem = new QTableWidgetItem(QString::fromUtf8(entry.step_name));
            auto* timeItem = new QTableWidgetItem(entry.timestamp.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")));

            QString desc = QString::fromUtf8(entry.description);
            auto* descItem = new QTableWidgetItem(desc);

            QFont lineageFont = nameItem->font();
            lineageFont.setItalic(true);
            nameItem->setFont(lineageFont);
            timeItem->setFont(lineageFont);
            descItem->setFont(lineageFont);

            nameItem->setForeground(lineageText);
            timeItem->setForeground(lineageText);
            descItem->setForeground(lineageText);
            nameItem->setBackground(lineageBg);
            timeItem->setBackground(lineageBg);
            descItem->setBackground(lineageBg);

            const QString tooltip = tr("Inherited from a previous image (read-only)");
            nameItem->setToolTip(tooltip);
            timeItem->setToolTip(tooltip);
            descItem->setToolTip(tooltip);

            nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsSelectable);
            timeItem->setFlags(timeItem->flags() & ~Qt::ItemIsSelectable);
            descItem->setFlags(descItem->flags() & ~Qt::ItemIsSelectable);

            m_ui.tableWidget->setItem(tableRow, 0, nameItem);
            m_ui.tableWidget->setItem(tableRow, 1, timeItem);
            m_ui.tableWidget->setItem(tableRow, 2, descItem);
            ++tableRow;
        }

        // Separator row
        {
            auto* sepItem = new QTableWidgetItem(tr("  ▾ Current Image History"));
            QFont sepFont = sepItem->font();
            sepFont.setBold(true);
            sepItem->setFont(sepFont);
            sepItem->setBackground(sectionBg);
            sepItem->setFlags(Qt::ItemIsEnabled);

            for (int col = 1; col < 3; ++col)
            {
                auto* filler = new QTableWidgetItem();
                filler->setBackground(sectionBg);
                filler->setFlags(Qt::ItemIsEnabled);
                m_ui.tableWidget->setItem(tableRow, col, filler);
            }

            m_ui.tableWidget->setItem(tableRow, 0, sepItem);
            m_ui.tableWidget->setSpan(tableRow, 0, 1, 3);
        }
    }

    if (auto step = held->getInitialStep(); step)
    {
        fillTableItem(m_LineageRowCount, step, currentStep == 0);
    }

    int idx = 1;
    for (const auto& step : steps)
    {
        fillTableItem(m_LineageRowCount + idx, step, currentStep == static_cast<size_t>(idx));
        ++idx;
    }

    if (totalSteps > 0)
    {
        m_ui.tableWidget->selectRow(m_LineageRowCount + static_cast<int>(currentStep));
    }

    m_ui.tableWidget->setColumnWidth(0, 190);
    m_ui.tableWidget->setColumnWidth(1, 145);

    updateNavigationButtons();
    updateSelectedStepPreview();
}

void ImageHistoryControl::updateNavigationButtons()
{
    const auto held = tracker();
    if (!held)
    {
        m_ui.moveUpButton->setEnabled(false);
        m_ui.moveDownButton->setEnabled(false);
        return;
    }

    const auto previousStep = an::ui::history::previousStep(held->getCurrentStep());
    const auto nextStep = an::ui::history::nextStep(held->getTotalStepCount(), held->getCurrentStep());

    m_ui.moveUpButton->setEnabled(previousStep.has_value());
    m_ui.moveDownButton->setEnabled(nextStep.has_value());

    const QString previousName = QString::fromUtf8(held->getPreviousStepName().c_str());
    const QString nextName = QString::fromUtf8(held->getNextStepName().c_str());

    m_ui.moveUpButton->setToolTip(previousName.isEmpty() ? tr("No earlier step available.")
                                                         : tr("Jump to %1").arg(previousName));
    m_ui.moveDownButton->setToolTip(nextName.isEmpty() ? tr("No later step available.")
                                                       : tr("Jump to %1").arg(nextName));
}

void ImageHistoryControl::updateSelectedStepPreview()
{
    // A preview still on its way is for a selection that has moved on.
    m_PreviewStop.request_stop();
    m_PreviewStop = std::stop_source();
    const std::uint64_t request = ++m_PreviewRequest;

    const int selectedRow = getSelectedStepRow();
    const auto held = tracker();
    const auto step = getStepForRow(selectedRow);
    if (!held || !step)
    {
        clearSelectedStepPreview();
        return;
    }

    const int stepRow = selectedRow - m_LineageRowCount;
    const bool isCurrentStep = stepRow >= 0 && static_cast<size_t>(stepRow) == held->getCurrentStep();
    m_ui.selectedStepTitleLabel->setFullText(stepTitleText(step, isCurrentStep));
    m_ui.selectedStepSubtitleLabel->setFullText(stepSubtitleText(step, isCurrentStep, true));

    // A sequence step that changed no frame previews the frame the view shows, read from its sequence.
    const std::uint64_t frameIndex = m_image_view_control ? m_image_view_control->getSequenceCurrentFrameIndex() : 0u;
    ImageHistoryTracker::StepReader read = held->stepReader(static_cast<size_t>(stepRow), frameIndex);
    const an::consumer::EngineSession* session = currentSession();

    QPointer<ImageHistoryControl> guard(this);
    const auto show = [guard, request, step, isCurrentStep](an::Result<StepPreview>&& preview) {
        if (!guard || guard->m_PreviewRequest != request)
        {
            return;
        }
        if (!preview.hasValue() || !preview->shown)
        {
            if (preview.hasValue() || preview.status().code() != an::ErrorCode::Cancelled)
            {
                guard->m_ui.selectedStepSubtitleLabel->setFullText(stepSubtitleText(step, isCurrentStep, false));
                guard->m_ShownPreview.reset();
                guard->m_stepPreviewWidget->setPreview(nullptr, nullptr);
                guard->m_stepPreviewWidget->setPixmap(QPixmap(), true);
                if (guard->m_stepHistogram && guard->m_stepHistogram->widget())
                {
                    guard->m_stepHistogram->widget()->hide();
                }
            }
            return;
        }
        guard->m_ShownPreview = preview->shown;
        guard->m_stepPreviewWidget->setPixmap(QPixmap::fromImage(an::ui::presentedQImage(preview->shown)), true);
        guard->m_stepPreviewWidget->setPreview(preview->shown, preview->image);
        if (guard->m_stepHistogram)
        {
            if (QWidget* histogram = guard->m_stepHistogram->widget())
            {
                histogram->show();
            }
            guard->m_stepHistogram->setImage(preview->image);
        }
        ++guard->m_PreviewsShown;
    };

    if (!read || session == nullptr)
    {
        show(an::Status::failure(an::ErrorCode::NotInitialized, "the step cannot be read"));
        return;
    }

    QThreadPool::globalInstance()->start([session, read = std::move(read), show, stop = m_PreviewStop.get_token()]() {
        auto preview = std::make_shared<an::Result<StepPreview>>(previewStep(*session, read, stop));
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(application, [show, preview]() { show(std::move(*preview)); }, Qt::QueuedConnection);
    });
}

void ImageHistoryControl::jumpToSelectedStep()
{
    const int selectedRow = getSelectedStepRow();
    const int stepRow = selectedRow - m_LineageRowCount;
    if (stepRow < 0)
    {
        return;
    }

    const auto held = tracker();
    if (held && held->canJumpToStep(static_cast<size_t>(stepRow)))
    {
        (void)held->jumpToStep(static_cast<size_t>(stepRow));
    }
}

void ImageHistoryControl::cloneSelectedStep()
{
    const int selectedRow = getSelectedStepRow();
    const int stepRow = selectedRow - m_LineageRowCount;
    const auto step = getStepForRow(selectedRow);
    const auto held = tracker();
    const an::consumer::EngineSession* session = currentSession();
    if (stepRow < 0 || !step || !held || !currentViewFactory() || session == nullptr || !m_image_view_control)
    {
        return;
    }

    const std::string historyDesc =
        fmt::format("Cloned from '{}' step '{}'", m_image_view_control->getUniqueName(), step->getStepName());
    // A sequence step is cloned at the frame it changed, or at the frame the view shows.
    const std::uint64_t frameIndex = step->m_FrameIndex.value_or(m_image_view_control->getSequenceCurrentFrameIndex());
    ImageHistoryTracker::StepReader read = held->stepReader(static_cast<size_t>(stepRow), frameIndex);
    if (!read)
    {
        return;
    }

    QPointer<ImageHistoryControl> guard(this);
    QPointer<ImageViewControl> source(m_image_view_control);
    const bool autoDebayer = m_image_view_control->isSequenceAutodebayerEnabled();
    const bool pendingChanges = m_image_view_control->hasPendingChanges();
    const auto publish = [guard, source, step, historyDesc, frameIndex, autoDebayer,
                          pendingChanges](an::Result<an::consumer::ImageAsset>&& image) {
        auto* dialogService = currentDialogService();
        auto* viewFactory = currentViewFactory();
        if (!image.hasValue() || viewFactory == nullptr || !source)
        {
            if (dialogService != nullptr && guard)
            {
                dialogService->notifyErrorMessage(guard, QObject::tr("History Clone"),
                                                  QObject::tr("Unable to clone the selected history step."));
            }
            return;
        }

        // A clone is a new image that has never been a file: it keeps the step's name and no file.
        an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
            std::move(*image), an::ui::DocumentNames{.displayName = QString::fromUtf8(step->m_ImageName)});
        if (!document.hasValue())
        {
            return;
        }
        ImageViewControl* clone = nullptr;
        if (step->m_Sequence.has_value())
        {
            an::ui::DocumentSequence sequence = *step->m_Sequence;
            sequence.currentFrame = frameIndex;
            an::ui::DocumentNames names = document->names();
            names.sourcePath = sequence.sourcePath;
            document->setNames(std::move(names));
            document->setSequence(std::move(sequence));
            clone = viewFactory->createNewImageViewControlFromSequence(
                nullptr, std::move(*document), nullptr, autoDebayer, historyDesc, ViewControlMode::standard, source,
                false, pendingChanges);
        }
        else
        {
            clone = viewFactory->createNewImageViewControlFromImage(nullptr, std::move(*document), nullptr, historyDesc,
                                                                    ViewControlMode::standard, source, false,
                                                                    pendingChanges);
        }
        if (clone != nullptr)
        {
            LOG_INFO("'{}' cloned from history step '{}' into '{}'", source->getUniqueName(), step->getStepName(),
                     clone->getUniqueName());
        }
    };

    QThreadPool::globalInstance()->start([read = std::move(read), publish, stop = m_PreviewStop.get_token()]() {
        auto image = std::make_shared<an::Result<an::consumer::ImageAsset>>(read(stop));
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(
            application, [publish, image]() { publish(std::move(*image)); }, Qt::QueuedConnection);
    });
}

void ImageHistoryControl::removeSelectedStep()
{
    const int selectedRow = getSelectedStepRow();
    const int stepRow = selectedRow - m_LineageRowCount;
    if (stepRow < 0)
    {
        return;
    }

    const auto held = tracker();
    if (held && held->canRemoveStep(static_cast<size_t>(stepRow)))
    {
        (void)held->removeStep(static_cast<size_t>(stepRow));
    }
}

void ImageHistoryControl::saveHistoryLog()
{
    const auto held = tracker();
    if (!held)
    {
        return;
    }

    const QString defaultName = QString::fromUtf8(fmt::format("{}_history.txt", m_image_view_control->getUniqueName()));

    static const QString contextKey = QStringLiteral("__IMAGE_HISTORY_SAVE__");
    QString initialDir;
    if (auto* pathMemory = currentPathMemoryService())
    {
        initialDir = pathMemory->selectPath(contextKey);
        if (!initialDir.isEmpty() && !QDir(initialDir).exists())
        {
            initialDir.clear();
        }
    }

    const QString saveDir = initialDir.isEmpty() ? defaultName : QDir(initialDir).filePath(defaultName);

    const QString path = an::ui::dialogs::getSaveFileName(currentGlobalSettings(), this, tr("Save History Log"),
                                                          saveDir, tr("Text files (*.txt);;All files (*)"));

    if (path.isEmpty())
    {
        return;
    }

    if (auto* pathMemory = currentPathMemoryService())
    {
        pathMemory->upsertPath(contextKey, QFileInfo(path).absolutePath());
    }

    const std::string log = an::ui::history::formatHistoryLog(*held);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (auto* dialogService = currentDialogService())
        {
            dialogService->notifyErrorMessage(this, tr("Save History Log"),
                                              tr("Unable to write the history log file."));
        }
        return;
    }

    file.write(QByteArray::fromStdString(log));
    LOG_INFO("History log saved to '{}'", path.toStdString());
}

int ImageHistoryControl::getSelectedStepRow() const
{
    return m_ui.tableWidget->currentRow();
}

ImageHistoryStepSPtr ImageHistoryControl::getStepForRow(const int row) const
{
    const int stepRow = row - m_LineageRowCount;
    if (stepRow < 0)
    {
        return nullptr;
    }

    const auto held = tracker();
    if (!held || static_cast<size_t>(stepRow) >= held->getTotalStepCount())
    {
        return nullptr;
    }

    if (stepRow == 0)
    {
        return held->getInitialStep();
    }

    const auto steps = held->getImageHistorySteps();
    const size_t historyIndex = static_cast<size_t>(stepRow - 1);
    if (historyIndex >= steps.size())
    {
        return nullptr;
    }

    return steps.at(historyIndex);
}

void ImageHistoryControl::clearSelectedStepPreview()
{
    m_ui.selectedStepTitleLabel->setFullText(tr("No Step Selected"));
    m_ui.selectedStepSubtitleLabel->setFullText(
        tr("Select a history row to inspect its preview, histogram, and available actions."));

    m_ShownPreview.reset();
    if (m_stepPreviewWidget)
    {
        m_stepPreviewWidget->setPreview(nullptr, nullptr);
        m_stepPreviewWidget->setPixmap(QPixmap(), true);
    }

    if (m_stepHistogram)
    {
        m_stepHistogram->setImage(nullptr);
        if (QWidget* histogram = m_stepHistogram->widget())
        {
            histogram->hide();
        }
    }
}
