// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The star profilers' window: the detection, the table, the profile documents and the windows it opens.

#include "stdafx.hpp"

#include "processes/StarProfilerWindow.h"

#include "controls/ImageViewControl.h"
#include "controls/ProfiledStarMaskCreatorControl.h"
#include "controls/ProgressDialogControl.h"
#include "processes/ProcessCommonUtils.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/StarProfileFile.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QCheckBox>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidgetItem>

#include <fmt/format.h>

#include <cmath>
#include <utility>

namespace
{

using an::ui::processes::StarProfileResult;

/// Whether a run ended because it was asked to stop, which a person who asked needs no message for.
[[nodiscard]] bool stopped(const an::Status& status) noexcept
{
    return status.code() == an::ErrorCode::Cancelled || status.code() == an::ErrorCode::DeadlineExceeded;
}

/// The engine's word for a failure, for a person.
[[nodiscard]] QString describe(const an::Status& status)
{
    const QString message = QString::fromUtf8(status.message()).trimmed();
    return message.isEmpty() ? QStringLiteral("The operation failed.") : message;
}

/// The engine's name for a generation, for a person.
[[nodiscard]] QString generationName(const an::StarProfileGeneration generation)
{
    return generation == an::StarProfileGeneration::Version1 ? QStringLiteral("first") : QStringLiteral("second");
}

} // namespace

StarProfilerWindow::StarProfilerWindow(ImageViewControl* control, const std::string& name)
    : ImageProcessControl(control, name)
{
}

StarProfilerWindow::~StarProfilerWindow()
{
    if (m_runStop)
    {
        m_runStop->request_stop();
    }
    if (m_readStop)
    {
        m_readStop->request_stop();
    }
    detachCurrentFilter();
    if (m_selection)
    {
        m_selection->detachFromViewWidget();
    }
}

void StarProfilerWindow::attachWidgets(const an::ui::processes::StarProfilerWidgets& widgets)
{
    m_widgets = widgets;
    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_widgets.buttons, &QDialogButtonBox::clicked, this,
                     &StarProfilerWindow::on_dialog_box_button_clicked);
    QObject::connect(m_widgets.detect, &QPushButton::released, this, [this]() { (void)detectStars(); });
    QObject::connect(m_widgets.reset, &QPushButton::released, this, [this]() { resetSettings(); });
    QObject::connect(m_widgets.load, &QPushButton::released, this, &StarProfilerWindow::on_LoadProfileButtonPushed);
    QObject::connect(m_widgets.save, &QPushButton::released, this, &StarProfilerWindow::on_SaveProfileButtonPushed);
    QObject::connect(m_widgets.clear, &QPushButton::released, this, &StarProfilerWindow::on_ClearProfileButtonPushed);
    QObject::connect(m_widgets.filter, &QPushButton::released, this, [this]() { (void)openStarsFilter(); });
    QObject::connect(m_widgets.mask, &QPushButton::released, this, [this]() { (void)openMaskCreator(); });
    QObject::connect(m_widgets.deleteStar, &QPushButton::released, this, [this]() { deleteSelectedStar(); });
    QObject::connect(m_widgets.centerSelected, &QCheckBox::checkStateChanged, this,
                     &StarProfilerWindow::on_centerSelectedStarCheckClicked);
    QObject::connect(m_widgets.zoom, &QDoubleSpinBox::valueChanged, this,
                     &StarProfilerWindow::on_zoomDetectedStarsValueChanged);

    if (m_image_view_control != nullptr)
    {
        m_selection = std::make_unique<ImageStarSelectionController>(m_image_view_control->getScrollArea(), this);
        m_selection->setListenerEnabled(true);
        m_selection->attachToViewWidget(m_image_view_control->getViewWidget());
    }
    m_widgets.table->setListener(this);
    initializeTable();
    updateControls();
}

void StarProfilerWindow::initializeTable()
{
    const QStringList titles = columnTitles();
    m_widgets.table->setAlternatingRowColors(true);
    m_widgets.table->setColumnCount(static_cast<int>(titles.size()));
    std::vector<QString> headers(titles.begin(), titles.end());
    m_widgets.table->setHeaderLabels(headers);
    m_widgets.table->setupSelectionColors();
}

void StarProfilerWindow::detectionStarting(std::stop_source&) {}

void StarProfilerWindow::onStarSelected(const size_t idx)
{
    // The view draws the list it was last handed - a filter's preview among them - so the star clicked is found in the
    // profile by its position.
    if (!m_selection)
    {
        return;
    }
    const an::ui::StarListPtr drawn = m_selection->getStarSelection();
    if (!drawn || idx >= drawn->size())
    {
        return;
    }
    const an::ui::processes::StarPositionKey key = an::ui::processes::starPositionKey((*drawn)[idx]);
    for (std::size_t row = 0u; row < m_stars->size(); ++row)
    {
        if (an::ui::processes::starPositionKey((*m_stars)[row]) == key)
        {
            m_widgets.table->selectRow(static_cast<int>(row));
            onRowSelected(m_widgets.table, static_cast<int>(row));
            return;
        }
    }
}

void StarProfilerWindow::onRowSelected(TableWidget*, const int row)
{
    QPointF position(0.0, 0.0);
    if (row >= 0 && static_cast<std::size_t>(row) < m_stars->size())
    {
        const an::DetectedStar& star = (*m_stars)[static_cast<std::size_t>(row)];
        position = QPointF(star.positionX, star.positionY);
    }
    if (m_selection)
    {
        m_selection->setHighlightStarSelection(position);
    }
    if (m_widgets.centerSelected->isChecked())
    {
        centerStar(position.x(), position.y());
    }
    m_widgets.deleteStar->setEnabled(row >= 0 && static_cast<std::size_t>(row) < m_stars->size());
}

void StarProfilerWindow::onColumnClicked(TableWidget*, const int column)
{
    orderByColumn(column);
}

an::ui::StarListPtr StarProfilerWindow::getCurrentStarList()
{
    return m_stars;
}

void StarProfilerWindow::setCurrentStarList(const an::ui::StarListPtr& starList, bool)
{
    m_stars = starList ? starList : std::make_shared<const an::ui::StarList>();
    m_manuallyEdited = true;
    showStars();
}

ImageViewControl* StarProfilerWindow::getViewControl()
{
    return getParentViewControl();
}

void StarProfilerWindow::updateStarSelection(const an::ui::StarListPtr& starSelection)
{
    if (m_selection)
    {
        m_selection->updateStarSelection(starSelection);
    }
}

bool StarProfilerWindow::detectStars()
{
    if (m_detecting)
    {
        return false;
    }
    const an::consumer::EngineSession* session = engineSession();
    ImageViewControl* view = m_image_view_control;
    std::shared_ptr<const an::consumer::ImageAsset> asset =
        view != nullptr && view->hasImage() ? view->document().asset() : nullptr;
    if (session == nullptr || !session->valid() || asset == nullptr)
    {
        notifyErrorMessage(QStringLiteral("Error running Star Profiler"),
                           QStringLiteral("There is no image to detect stars on."));
        return false;
    }

    // The dialog is read now; the worker reads none of the window.
    an::ui::processes::StarProfileWork work = detectionWork();
    // A detection asked for supersedes a read on its way, whatever that read would have found.
    forgetRead();
    const std::uint64_t generation = ++m_generation;
    if (m_runStop)
    {
        m_runStop->request_stop();
    }
    m_runStop = std::make_shared<std::stop_source>();
    detectionStarting(*m_runStop);

    auto* progress = new ProgressDialogControl(
        this, QString::fromUtf8(fmt::format("{} <{}>", getProcessName(), view->getUniqueName())));
    progress->setProgressBarRange(0, 0);
    progress->setValue(0);
    progress->setText(QStringLiteral("Detecting stars..."));
    progress->show();
    registerNewProgressDialog(progress);
    const QPointer<ProgressDialogControl> progressGuard(progress);

    m_detecting = true;
    updateControls();
    LOG_INFO("Executing {} detection engine for image '{}'...", logName(), view->getUniqueName());

    const std::stop_token windowStop = m_runStop->get_token();
    const std::stop_token processStop = windowStopToken();
    const std::stop_token dialogStop = progress->stopToken();
    an::ui::ExecutionObserver observer = progress->observer();
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    runThenPublish(
        [session, asset, work = std::move(work), windowStop, processStop, dialogStop,
         observer]() mutable -> an::Result<StarProfileResult> {
            // The detection's own stop, the window's going and the progress window's Cancel all stop the run.
            std::stop_source runStop;
            const std::stop_callback onWindow(windowStop, [&runStop]() { runStop.request_stop(); });
            const std::stop_callback onProcess(processStop, [&runStop]() { runStop.request_stop(); });
            const std::stop_callback onDialog(dialogStop, [&runStop]() { runStop.request_stop(); });
            // The lease first, then the admission: an edit in place holds the writer while it waits to be admitted.
            an::Result<an::consumer::AssetReadLease> lease = asset->read(runStop.get_token());
            if (!lease.hasValue())
            {
                return lease.status();
            }
            an::Result<an::ui::ExecutionScope> scope =
                an::ui::ExecutionScope::admit(*session,
                                              an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply,
                                                                       .stopToken = runStop.get_token()},
                                              std::move(observer));
            if (!scope.hasValue())
            {
                return scope.status();
            }
            an::Result<StarProfileResult> result = work(*scope, an::ui::ProcessSource::of(*lease));
            scope->finish(result.hasValue() ? an::Status::success() : result.status());
            return result;
        },
        [this, generation, progressGuard, timer](an::Result<StarProfileResult> result) {
            if (progressGuard)
            {
                // A newer detection's progress window stays the one a close cancels.
                if (currentProgressDialog() == progressGuard.data())
                {
                    unregisterCurrentProgressDialog();
                }
                progressGuard->accept();
            }
            if (generation == m_generation)
            {
                LOG_INFO("{} detection finished in {:.2f} seconds", logName(),
                         static_cast<double>(timer->elapsed()) / 1000.0);
            }
            landDetection(generation, std::move(result));
        });
    return true;
}

void StarProfilerWindow::landDetection(const std::uint64_t generation,
                                       an::Result<an::ui::processes::StarProfileResult> result)
{
    if (generation != m_generation)
    {
        // A clear, a newer detection or a read accepted after this detection was asked for supersedes it; the
        // detection running now, if any, is another one.
        return;
    }
    if (m_readPending)
    {
        // A read asked for after this detection decides what is shown: the detection is held until the read lands,
        // published if the read is refused and dropped if it is accepted.
        m_detecting = false;
        m_heldDetection.emplace(std::move(result));
        updateControls();
        return;
    }
    publishDetection(std::move(result));
}

void StarProfilerWindow::publishDetection(an::Result<an::ui::processes::StarProfileResult> result)
{
    m_detecting = false;
    if (!result.hasValue())
    {
        updateControls();
        if (stopped(result.status()) || m_ProcessCloseRequested)
        {
            LOG_INFO("{}: the detection was stopped", logName());
            return;
        }
        const std::string text =
            fmt::format("An error occurred when running Star Profiler process:\n{}", result.status().message());
        LOG_WARNING("{}: {}", logName(), text);
        notifyErrorMessage(QStringLiteral("Error running Star Profiler"), QString::fromUtf8(text));
        return;
    }
    if (result->catalogFailure.has_value())
    {
        LOG_WARNING("{}: Gaia enrichment unavailable: {}", logName(), result->catalogFailure->message());
    }
    m_stars = std::make_shared<const an::ui::StarList>(std::move(result->stars));
    m_enrichment = std::move(result->enrichment);
    m_header = std::move(result->header);
    m_manuallyEdited = false;
    m_sortColumn = -1;
    showStars();
    LOG_INFO("{} detection engine executed successfully. Detected stars: {}", logName(), m_stars->size());
    if (!m_stars->empty())
    {
        process_utils::notifyInfoMessage(
            this, QStringLiteral("Detect star profile"),
            QString::fromStdString(
                fmt::format("Star profile detected successfully! {} Detected stars.", m_stars->size())));
    }
    else
    {
        process_utils::notifyWarnMessage(this, QStringLiteral("Detect star profile"),
                                         QStringLiteral("Could NOT detect any star"));
    }
}

bool StarProfilerWindow::isDetecting() const noexcept
{
    return m_detecting;
}

bool StarProfilerWindow::loadProfileFrom(const QString& path)
{
    const an::consumer::EngineSession* session = engineSession();
    if (session == nullptr || !session->valid() || path.trimmed().isEmpty())
    {
        notifyErrorMessage(QStringLiteral("Load Star Profile"),
                           QStringLiteral("Could NOT load the star profile file."));
        return false;
    }
    // A read supersedes a read on its way at once, but a detection on its way only once the read is accepted: a read
    // the window refuses leaves the detection running and the stars it holds.
    if (m_readStop)
    {
        m_readStop->request_stop();
    }
    m_readStop = std::make_shared<std::stop_source>();
    const std::uint64_t generation = ++m_readGeneration;
    m_readPending = true;
    const std::stop_token stop = m_readStop->get_token();
    const std::string file = path.toUtf8().toStdString();
    runThenPublish(
        [session, file, stop]() -> an::Result<an::StarProfileFile> {
            an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
                *session, an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply, .stopToken = stop},
                an::ui::ExecutionObserver{});
            if (!scope.hasValue())
            {
                return scope.status();
            }
            an::Result<an::StarProfileFile> document = an::readStarProfileFile(scope->execution(), file);
            scope->finish(document.hasValue() ? an::Status::success() : document.status());
            return document;
        },
        [this, generation, path](an::Result<an::StarProfileFile> document) {
            landLoad(generation, path, std::move(document));
        });
    return true;
}

void StarProfilerWindow::landLoad(const std::uint64_t generation, const QString& path,
                                  an::Result<an::StarProfileFile> document)
{
    if (generation != m_readGeneration || !m_readPending)
    {
        // A newer read, a detection, a clear or a close asked for after this read supersedes it.
        return;
    }
    m_readPending = false;
    std::optional<an::Result<an::ui::processes::StarProfileResult>> held = std::exchange(m_heldDetection, std::nullopt);
    if (!document.hasValue() || document->generation != profileGeneration())
    {
        const QString reason =
            document.hasValue()
                ? QStringLiteral("The file holds a profile of the %1 star profiler, and this one reads the %2's.")
                      .arg(generationName(document->generation), generationName(profileGeneration()))
                : describe(document.status());
        if (document.hasValue() || !stopped(document.status()))
        {
            notifyErrorMessage(QStringLiteral("Load Star Profile"),
                               QStringLiteral("Could NOT load the star profile file. Error message: %1").arg(reason));
            LOG_WARNING("{}: Could NOT load the star profile file '{}': {}", logName(), path.toStdString(),
                        reason.toStdString());
        }
        if (held.has_value())
        {
            publishDetection(std::move(*held));
        }
        return;
    }
    // The read is accepted: it supersedes the detection on its way, if any, and the one it held, if any.
    ++m_generation;
    if (m_runStop)
    {
        m_runStop->request_stop();
    }
    m_detecting = false;
    an::ui::processes::LoadedProfile loaded = an::ui::processes::loadedProfile(*document);
    m_stars = std::make_shared<const an::ui::StarList>(std::move(loaded.stars));
    m_enrichment = std::move(loaded.enrichment);
    m_header = std::move(loaded.header);
    m_manuallyEdited = std::visit([](const auto& payload) { return payload.manuallyEdited; }, document->payload);
    m_sortColumn = -1;
    showStars();
    process_utils::notifyInfoMessage(
        this, QStringLiteral("Load star profile"),
        QString::fromStdString(fmt::format("Star profile loaded successfully! {} Detected stars.", m_stars->size())));
    LOG_INFO("{}: Load star profile loaded {} stars", logName(), m_stars->size());
}

std::optional<an::StarProfileFile> StarProfilerWindow::currentDocument() const
{
    if (m_stars->empty() || !m_header.has_value())
    {
        return std::nullopt;
    }
    return an::ui::processes::profileDocument(*m_header, *m_stars, m_enrichment, m_manuallyEdited);
}

bool StarProfilerWindow::saveProfileTo(const QString& path)
{
    std::optional<an::StarProfileFile> document = currentDocument();
    if (!document.has_value())
    {
        notifyErrorMessage(QStringLiteral("Save profile list"), QStringLiteral("No detected star list to save"));
        return false;
    }
    const an::consumer::EngineSession* session = engineSession();
    if (session == nullptr || !session->valid() || path.trimmed().isEmpty())
    {
        notifyErrorMessage(QStringLiteral("Save star profile"), QStringLiteral("The star profile could not be saved."));
        return false;
    }
    const std::string file = path.toUtf8().toStdString();
    const std::size_t count = m_stars->size();
    auto written = std::make_shared<const an::StarProfileFile>(std::move(*document));
    runThenPublish(
        [session, file, written]() -> an::Status {
            an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
                *session, an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply},
                an::ui::ExecutionObserver{});
            if (!scope.hasValue())
            {
                return scope.status();
            }
            const an::Result<an::StarProfileWriteReport> report = an::writeStarProfileFile(
                scope->engine(), scope->execution(), an::StarProfileWriteRequest{.destinationPath = file}, *written);
            const an::Status status = report.hasValue() ? an::Status::success() : report.status();
            scope->finish(status);
            return status;
        },
        [this, file, count](const an::Status status) {
            if (status.failed())
            {
                notifyErrorMessage(QStringLiteral("Save star profile"),
                                   QStringLiteral("The star profile could not be saved.\n%1").arg(describe(status)));
                LOG_WARNING("{}: the star profile could not be saved to '{}': {}", logName(), file, status.message());
                return;
            }
            process_utils::notifyInfoMessage(this, QStringLiteral("Save star profile"),
                                             QStringLiteral("Star profile saved successfully!"));
            LOG_INFO("{}: Save star profile to file '{}'. Saved stars: {}", logName(), file, count);
        });
    return true;
}

void StarProfilerWindow::forgetRead()
{
    ++m_readGeneration;
    if (m_readStop)
    {
        m_readStop->request_stop();
    }
    m_readPending = false;
    m_heldDetection.reset();
}

void StarProfilerWindow::forgetRuns()
{
    forgetRead();
    ++m_generation;
    if (m_runStop)
    {
        m_runStop->request_stop();
    }
    cancelCurrentProgressDialog();
    m_detecting = false;
}

void StarProfilerWindow::clearProfile()
{
    forgetRuns();
    m_stars = std::make_shared<const an::ui::StarList>();
    m_enrichment.clear();
    m_header.reset();
    m_manuallyEdited = false;
    m_sortColumn = -1;
    showStars();
    LOG_INFO("{}: Star profile list deleted", logName());
}

void StarProfilerWindow::deleteSelectedStar()
{
    const int row = m_widgets.table->currentRow();
    if (row >= 0 && static_cast<std::size_t>(row) < m_stars->size())
    {
        an::ui::StarList kept(*m_stars);
        kept.erase(kept.begin() + row);
        m_stars = std::make_shared<const an::ui::StarList>(std::move(kept));
        m_manuallyEdited = true;
        showStars();
    }
    m_widgets.deleteStar->setEnabled(false);
}

void StarProfilerWindow::selectStar(const int row)
{
    m_widgets.table->selectRow(row);
    onRowSelected(m_widgets.table, row);
}

void StarProfilerWindow::orderByColumn(const int column)
{
    if (column < 0 || column >= m_widgets.table->columnCount())
    {
        return;
    }
    if (m_sortColumn == column)
    {
        m_sortDescending = !m_sortDescending;
    }
    else
    {
        m_sortColumn = column;
        m_sortDescending = true;
    }
    m_stars = std::make_shared<const an::ui::StarList>(orderedBy(*m_stars, column, m_sortDescending));
    showStars();
    notifyFilterUpdated();
}

StarsFilterControlBase* StarProfilerWindow::openStarsFilter()
{
    if (m_stars->empty())
    {
        return nullptr;
    }
    StarsFilterControlBase* filter = makeStarsFilter();
    if (auto* window = dynamic_cast<QWidget*>(filter); window != nullptr)
    {
        window->show();
    }
    return filter;
}

ProfiledStarMaskCreatorControl* StarProfilerWindow::openMaskCreator()
{
    if (m_stars->empty())
    {
        notifyErrorMessage(QStringLiteral("Create Mask"), QStringLiteral("No detected star list to create a mask"));
        return nullptr;
    }
    // Shown and not modal: it paints on a worker, and the event loop keeps turning while a person sets it up.
    auto* window = new ProfiledStarMaskCreatorControl(this);
    window->setModal(false);
    window->show();
    return window;
}

const an::ui::processes::StarEnrichmentTable& StarProfilerWindow::enrichment() const noexcept
{
    return m_enrichment;
}

bool StarProfilerWindow::manuallyEdited() const noexcept
{
    return m_manuallyEdited;
}

const std::optional<an::StarProfilePayload>& StarProfilerWindow::profileHeader() const noexcept
{
    return m_header;
}

void StarProfilerWindow::showStars()
{
    TableWidget& table = *m_widgets.table;
    table.updateHeaders();
    table.clearContents();
    table.setSortingEnabled(false);
    table.setRowCount(static_cast<int>(m_stars->size()));
    for (std::size_t row = 0u; row < m_stars->size(); ++row)
    {
        fillRow(table, static_cast<int>(row), (*m_stars)[row]);
    }
    if (m_sortColumn >= 0 && m_sortColumn < table.columnCount())
    {
        const QString title = table.getHeaderLabel(m_sortColumn);
        table.setColumnTitle(m_sortColumn, m_sortDescending ? "*" + title + " <" : "*" + title + " >", false);
    }
    m_widgets.detectedStars->setText(QString::number(m_stars->size()));
    if (m_selection)
    {
        if (m_stars->empty())
        {
            m_selection->resetStarSelection();
        }
        else
        {
            m_selection->updateStarSelection(m_stars);
        }
    }
    updateControls();
}

void StarProfilerWindow::updateControls()
{
    const bool hasStars = !m_stars->empty();
    m_widgets.detect->setEnabled(!m_detecting);
    m_widgets.save->setEnabled(hasStars);
    m_widgets.clear->setEnabled(hasStars);
    m_widgets.filter->setEnabled(hasStars);
    m_widgets.mask->setEnabled(hasStars);
    m_widgets.deleteStar->setEnabled(false);
    m_widgets.centerSelected->setEnabled(hasStars);
    m_widgets.zoom->setEnabled(hasStars && m_widgets.centerSelected->isChecked());
}

void StarProfilerWindow::centerStar(const double x, const double y)
{
    if (m_image_view_control != nullptr)
    {
        m_image_view_control->zoomAndCenterOn(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)),
                                              static_cast<float>(m_widgets.zoom->value()));
    }
}

void StarProfilerWindow::closeEvent(QCloseEvent* event)
{
    if (!m_closeConfirmed && !m_stars->empty())
    {
        int answer = QMessageBox::No;
        process_utils::messageBoxYesNo(this, tr("Close Star Profiler"),
                                       tr("All non saved data will be lost. Do you want to exit this process?"), false,
                                       &answer);
        if (answer != QMessageBox::Yes)
        {
            event->ignore();
            return;
        }
    }
    m_closeConfirmed = true;
    forgetRuns();
    detachCurrentFilter();
    if (m_selection)
    {
        m_selection->destroy();
    }
    ImageProcessControl::closeEvent(event);
}

void StarProfilerWindow::on_dialog_box_button_clicked(QAbstractButton* button)
{
    if (m_widgets.buttons != nullptr && m_widgets.buttons->standardButton(button) == QDialogButtonBox::Close)
    {
        this->close();
    }
}

void StarProfilerWindow::on_LoadProfileButtonPushed()
{
    const QStringList selected = process_utils::selectFiles(this, QStringLiteral("Load star profile..."),
                                                            profileFolderContext(), {profileFileFilter()});
    if (!selected.isEmpty())
    {
        (void)loadProfileFrom(selected.back());
    }
}

void StarProfilerWindow::on_SaveProfileButtonPushed()
{
    if (m_stars->empty())
    {
        notifyErrorMessage(QStringLiteral("Save profile list"), QStringLiteral("No detected star list to save"));
        return;
    }
    const QStringList selected = process_utils::saveFiles(this, QStringLiteral("Save star profile..."), QString(),
                                                          profileFolderContext(), {profileFileFilter()});
    if (!selected.isEmpty())
    {
        (void)saveProfileTo(selected.back());
    }
}

void StarProfilerWindow::on_ClearProfileButtonPushed()
{
    int answer = QMessageBox::No;
    process_utils::messageBoxYesNo(this, QStringLiteral("Clear Star profile list"),
                                   QStringLiteral("This will remove all the stars from the list, are you sure?"), false,
                                   &answer);
    if (answer == QMessageBox::Yes)
    {
        clearProfile();
    }
}

void StarProfilerWindow::on_centerSelectedStarCheckClicked(const Qt::CheckState state)
{
    m_widgets.zoom->setEnabled(state == Qt::Checked && !m_stars->empty());
    if (state == Qt::Checked && m_selection)
    {
        const QPointF position = m_selection->getHighlightStarSelection();
        if (position.x() > 0 && position.y() > 0)
        {
            centerStar(position.x(), position.y());
        }
    }
}

void StarProfilerWindow::on_zoomDetectedStarsValueChanged(double)
{
    if (m_widgets.centerSelected->isChecked() && m_selection)
    {
        const QPointF position = m_selection->getHighlightStarSelection();
        if (position.x() > 0 && position.y() > 0)
        {
            centerStar(position.x(), position.y());
        }
    }
}
