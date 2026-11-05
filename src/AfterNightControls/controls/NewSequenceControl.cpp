// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "NewSequenceControl.h"

#include "app/AppContextHelpers.h"
#include "app/FileCompanions.h"
#include "common.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewFiles.h"
#include "controls/ProgressDialogControl.h"
#include "controls/SequenceContainers.h"
#include "controls/TableWidgetUtils.h"
#include "session/IEngineSessionProvider.h"
#include "theme/AnDesignTokens.h"
#include "widgets/HistogramCompanions.h"
#include "widgets/PresentationBridge.h"
#include "widgets/ViewPresentation.h"
#include "widgets/waitingspinnerwidget.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/IO/ImageListManifest.h>
#include <AfterNightEngine/IO/SequenceOutput.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <AfterNightPlatform/Logging.h>

#include <verdigris/wobjectimpl.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QMetaObject>
#include <QStandardItemModel>
#include <QThreadPool>
#include <QTimeZone>

#include <utility>
#include <variant>

W_OBJECT_IMPL(NewSequenceControl)

namespace
{

/// How many probed files are added to the table at once.
constexpr std::size_t kAddBatchSize = 50u;

/// The sequence kinds the window offers, in its selector's order.
const QString kImageListKind = QStringLiteral("Image List");
const QString kSerKind = QStringLiteral("SER Sequence");
const QString kFitsKind = QStringLiteral("FITS Sequence");

/// A failure the window decided, which no engine call produced.
[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

QColor previewBackgroundColor()
{
    return an::ui::tokens().colors.bgSurface;
}

QColor normalIconColor()
{
    if (const an::ui::IIconProviderPtr provider = process_utils::currentIconProvider())
    {
        return provider->getColorNormal();
    }
    return an::ui::tokens().colors.textSecondary;
}

QIcon controlIcon(const QString& name)
{
    if (const an::ui::IIconProviderPtr provider = process_utils::currentIconProvider())
    {
        return provider->getIcon(name);
    }
    return {};
}

QPixmap noImageLoadedPixmap()
{
    if (const an::ui::IIconProviderPtr provider = process_utils::currentIconProvider())
    {
        return provider->loadSvgFileAsPixmap(QStringLiteral(":/AfterNight/icons/SVG/picture.svg"), QSize(64, 64),
                                             normalIconColor());
    }
    return QIcon(QStringLiteral(":/AfterNight/icons/SVG/picture.svg")).pixmap(QSize(64, 64));
}

/// The process's session, or null when the application lends none.
const an::consumer::EngineSession* currentSession()
{
    if (auto* provider = process_utils::currentEngineSessionProvider())
    {
        return &provider->engineSession();
    }
    return nullptr;
}

/// Runs a function on the GUI thread through the event loop, never waited on.
void postToGui(std::function<void()> function)
{
    if (QCoreApplication* application = QCoreApplication::instance())
    {
        QMetaObject::invokeMethod(application, std::move(function), Qt::QueuedConnection);
    }
}

/// A stop source stopped by either of two tokens, for as long as it lives.
class LinkedStop
{
public:
    LinkedStop(const std::stop_token& first, const std::stop_token& second)
        : m_first(first, Stopper{&m_source}), m_second(second, Stopper{&m_source})
    {
    }

    [[nodiscard]] std::stop_token token() const noexcept
    {
        return m_source.get_token();
    }

private:
    struct Stopper
    {
        std::stop_source* source;
        void operator()() const noexcept
        {
            source->request_stop();
        }
    };

    std::stop_source m_source;
    std::stop_callback<Stopper> m_first;
    std::stop_callback<Stopper> m_second;
};

/// A time as the table shows it.
QString tableTime(const an::UtcInstant& instant)
{
    return QDateTime::fromMSecsSinceEpoch(instant.nanosecondsSinceEpoch / 1000000, QTimeZone::UTC)
        .toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));
}

/// The table's Depth column for a representation.
QString depthText(an::SampleType type)
{
    switch (type)
    {
    case an::SampleType::UInt8:
    case an::SampleType::Int8:
        return QStringLiteral("8 bit");
    case an::SampleType::UInt16:
    case an::SampleType::Int16:
        return QStringLiteral("16 bit");
    case an::SampleType::UInt32:
    case an::SampleType::Int32:
        return QStringLiteral("32 bit int");
    case an::SampleType::Float32:
        return QStringLiteral("32 bit float");
    case an::SampleType::Float64:
        return QStringLiteral("64 bit float");
    }
    return QStringLiteral("-");
}

/// Probes one file: its description and metadata, read without decoding a pixel.
an::Result<ImageListProcessHelpers::ImageListEntry> probeFile(const an::consumer::EngineSession& session,
                                                              const QString& path, const std::stop_token& stop)
{
    if (stop.stop_requested())
    {
        return refusal(an::ErrorCode::Cancelled, "the window was closed");
    }
    an::Result<an::consumer::OperationScope> scope = session.admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply, .stopToken = stop});
    if (!scope.hasValue())
    {
        return scope.status();
    }
    const std::string file = path.toUtf8().toStdString();
    an::Result<an::CodecImageInfo> probed = an::probeImage(
        scope->engine(), scope->execution(), an::CodecReadRequest{.path = file, .validatePayload = false});
    if (!probed.hasValue())
    {
        return probed.status();
    }
    const std::optional<an::ImageDescriptor> descriptor = probed->descriptor.toImageDescriptor();
    if (!descriptor.has_value())
    {
        return refusal(an::ErrorCode::Unsupported, "the file holds samples outside the eight this engine processes");
    }
    return ImageListProcessHelpers::ImageListEntry{
        .path = path, .descriptor = *descriptor, .metadata = std::move(probed->metadata)};
}

/// A preview of one file: the image read, demosaiced by the fast method when it is a mosaic, and presented as it is -
/// every channel at full scale and no stretch.
struct FilePreview
{
    std::shared_ptr<const an::consumer::ImageAsset> image;
    std::shared_ptr<const an::ui::PresentedImage> shown;
};

an::Result<FilePreview> previewFile(const an::consumer::EngineSession& session, const QString& path,
                                    const std::stop_token& stop)
{
    an::Result<an::ui::ViewReadResult> read = an::ui::readViewImage(
        session, an::ui::ViewReadRequest{.path = path, .demosaic = an::DemosaicMethod::Fast}, stop, nullptr);
    if (!read.hasValue())
    {
        return read.status();
    }
    auto held = std::make_shared<const an::consumer::ImageAsset>(std::move(read->asset));
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
    an::ui::ViewPresentationSettings settings;
    settings.request.stretch = an::PresentationStretch::Linear;
    an::Result<an::ui::ViewPresentationOutcome> presented =
        an::ui::presentForView(scope->engine(), scope->execution(), *lease, settings, nullptr, &session.memory());
    if (!presented.hasValue())
    {
        return presented.status();
    }
    return FilePreview{.image = std::move(held), .shown = std::move(presented->image)};
}

/// The first frame's description, written into an image list's manifest for its reader.
an::ImageListManifest manifestOf(const an::ui::NewSequenceRequest& request)
{
    an::ImageListManifest manifest;
    manifest.timestampSeconds = request.createdSeconds;
    const an::ImageDescriptor& first = request.files.front().descriptor;
    manifest.width = first.extent().width();
    manifest.height = first.extent().height();
    manifest.channels = first.extent().channelCount();
    manifest.mosaiced = first.isMosaiced();
    manifest.framesPerSecond = request.framesPerSecond;
    manifest.framePaths.reserve(request.files.size());
    for (const ImageListProcessHelpers::ImageListEntry& file : request.files)
    {
        manifest.framePaths.push_back(QFileInfo(file.path).absoluteFilePath().toUtf8().toStdString());
    }
    return manifest;
}

/// Decodes one file into an image under the caller's execution.
an::Result<an::consumer::AnyImage> decodeFrame(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                               const QString& path)
{
    const std::string file = path.toUtf8().toStdString();
    an::Result<an::CodecDecodeResult> decoded = an::decodeImage(engine, execution, an::CodecReadRequest{.path = file});
    if (!decoded.hasValue())
    {
        return decoded.status();
    }
    return an::ui::importViewFrame(engine, execution, std::move(decoded->frame), std::move(decoded->metadata));
}

} // namespace

namespace an::ui
{

an::Status makeNewSequence(const an::consumer::EngineSession& session, const NewSequenceRequest& request,
                           std::stop_token stop, const std::function<void(std::uint64_t, std::uint64_t)>& progress)
{
    if (request.files.empty())
    {
        return refusal(an::ErrorCode::EmptySelection, "no image is listed");
    }
    const auto admit = [&]() {
        return session.admit(
            an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply, .stopToken = stop});
    };
    const std::uint64_t total = request.files.size();
    if (request.kind == NewSequenceKind::ImageList)
    {
        an::Result<an::consumer::OperationScope> scope = admit();
        if (!scope.hasValue())
        {
            return scope.status();
        }
        const std::string destination = request.path.toUtf8().toStdString();
        const an::Result<an::SequenceOutputReport> written =
            an::writeImageListManifest(scope->engine(), scope->execution(),
                                       an::SequenceOutputRequest{.destinationPath = destination}, manifestOf(request));
        if (!written.hasValue())
        {
            return written.status();
        }
        if (progress)
        {
            progress(total, total);
        }
        return an::Status::success();
    }

    SequenceWriteSettings settings;
    settings.container = an::SequenceFormat::Ser;
    settings.frameRate = sequenceRateOf(request.framesPerSecond);
    settings.frameCount = total;
    settings.createdSeconds = request.createdSeconds;
    an::Result<SequenceWriter> writer = SequenceWriter::create(request.path, settings);
    if (!writer.hasValue())
    {
        return writer.status();
    }
    std::uint64_t written = 0u;
    for (const ImageListProcessHelpers::ImageListEntry& file : request.files)
    {
        // One admitted step per frame, so the session's other work runs between frames.
        an::Result<an::consumer::OperationScope> scope = admit();
        if (!scope.hasValue())
        {
            return scope.status();
        }
        an::Result<an::consumer::AnyImage> frame = decodeFrame(scope->engine(), scope->execution(), file.path);
        if (!frame.hasValue())
        {
            return frame.status();
        }
        const std::optional<an::ImageDescriptor> stored =
            std::visit([](const auto& typed) { return typed.descriptor(); }, *frame);
        if (request.demosaic && stored.has_value() && stored->extent().channelCount() == 1u &&
            (stored->isMosaiced() || request.pattern.stated.has_value()))
        {
            const an::Result<an::CfaDescriptor> pattern = resolveBayerPattern(request.pattern, *stored);
            if (!pattern.hasValue())
            {
                return pattern.status();
            }
            an::Result<an::consumer::AnyImage> colour =
                demosaicViewFrame(scope->engine(), scope->execution(), *frame, request.method, *pattern);
            if (!colour.hasValue())
            {
                return colour.status();
            }
            frame = std::move(colour);
        }
        if (an::Status appended = writer->append(scope->engine(), scope->execution(), *frame); appended.failed())
        {
            return appended;
        }
        ++written;
        if (progress)
        {
            progress(written, total);
        }
    }
    an::Result<an::consumer::OperationScope> scope = admit();
    if (!scope.hasValue())
    {
        return scope.status();
    }
    an::Result<an::SequenceDescriptor> published = writer->finish(scope->execution());
    return published.hasValue() ? an::Status::success() : published.status();
}

} // namespace an::ui

NewSequenceControl::NewSequenceControl(QWidget* parent) : TopLevelProcessBaseControl(getProcessNameStatic())
{
    Q_UNUSED(parent);

    m_ui = an::ui::controls::buildNewSequenceControlUi(this);
    setProcessDescription(QStringLiteral("Build a sequence from selected frames with configurable debayering, "
                                         "output type and frame rate."));

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &NewSequenceControl::on_dialog_box_button_clicked);
    QObject::connect(m_ui.addFiles, &QPushButton::released, this, &NewSequenceControl::on_addFilesButtonPushed);
    QObject::connect(m_ui.removeSelected, &QPushButton::released, this,
                     &NewSequenceControl::on_removeSelectedButtonPushed);
    QObject::connect(m_ui.moveUpButton, &QPushButton::released, this, &NewSequenceControl::on_moveUpButtonPushed);
    QObject::connect(m_ui.moveDownButton, &QPushButton::released, this, &NewSequenceControl::on_moveDownButtonPushed);
    QObject::connect(m_ui.debayerCFA, &QCheckBox::checkStateChanged, this, &NewSequenceControl::on_debayerCFA_clicked);
    QObject::connect(m_ui.clearAll, &QPushButton::released, this, &NewSequenceControl::on_clearAllButtonPushed);

    m_ui.tableWidget->setListener(this);

    m_NoImageLoadedPixmap = noImageLoadedPixmap();

    m_ImagePreviewSpinner = std::make_unique<WaitingSpinnerWidget>(m_ui.imagePreviewWidget, true, false);
    m_ImagePreviewSpinner->setColor(an::ui::tokens().colors.accentPrimary);

    m_TableSpinner = std::make_unique<WaitingSpinnerWidget>(m_ui.tableWidget, true, true);
    m_TableSpinner->setColor(normalIconColor());

    m_Histogram = std::make_unique<an::ui::HistogramCountScheduler>(m_ui.histogramWidget);

    m_ui.moveUpButton->setIcon(controlIcon(QStringLiteral("up")));
    m_ui.moveDownButton->setIcon(controlIcon(QStringLiteral("down")));

    m_ui.imagePreviewWidget->setOwnerWidget(this);
    m_ui.imagePreviewWidget->setBackgroundColor(previewBackgroundColor());
    showNoPreview();

    m_ui.histogramWidget->setClippingInfoEnabled(false);

    m_ui.moveUpButton->setEnabled(false);
    m_ui.moveDownButton->setEnabled(false);

    m_ui.sequenceNameEdit->setText(QStringLiteral("NEW_SEQUENCE"));

    initialize();
    initializeTable();

    initializeTopLevelProcess();
}

NewSequenceControl::~NewSequenceControl()
{
    m_Previews.cancel();
}

std::string NewSequenceControl::getProcessNameStatic()
{
    return "New Sequence";
}

void NewSequenceControl::onRowSelected(TableWidget* w, int row)
{
    Q_UNUSED(w);
    if (m_IgnoreLastRowSelectedEvent)
    {
        return;
    }

    process_utils::updateNavigationButtons(m_ui.moveUpButton, m_ui.moveDownButton, row, m_image_list.size());

    if (row < 0 || row >= static_cast<int>(m_image_list.size()) || m_CurrentSelectedRow == row)
    {
        return;
    }

    m_CurrentSelectedRow = row;
    m_ui.currentFrameLabel->setText(QString::number(m_CurrentSelectedRow + 1));
    requestPreview(row);
}

void NewSequenceControl::onColumnClicked(TableWidget* w, int column)
{
    Q_UNUSED(w);
    Q_UNUSED(column);
}

void NewSequenceControl::refreshTheme()
{
    TopLevelProcessBaseControl::refreshTheme();

    m_ui.imagePreviewWidget->setBackgroundColor(previewBackgroundColor());
    m_ui.tableWidget->setupSelectionColors();
    m_ui.moveUpButton->setIcon(controlIcon(QStringLiteral("up")));
    m_ui.moveDownButton->setIcon(controlIcon(QStringLiteral("down")));
    if (m_TableSpinner)
    {
        m_TableSpinner->setColor(normalIconColor());
    }
}

void NewSequenceControl::addItemsNonBlocking(const QStringList& items)
{
    QStringList fresh;
    std::unordered_set<std::string> batch;
    for (const QString& item : items)
    {
        const std::string path = item.toUtf8().toStdString();
        if (m_image_paths_set.count(path) == 0u && batch.insert(path).second)
        {
            fresh.push_back(item);
        }
    }
    if (fresh.isEmpty())
    {
        return;
    }
    const an::consumer::EngineSession* session = currentSession();
    if (session == nullptr)
    {
        addingFinished(fresh.front(), refusal(an::ErrorCode::NotInitialized, "no engine session is lent"));
        return;
    }

    ++m_AddsOutstanding;
    m_TableSpinner->start();
    QPointer<NewSequenceControl> guard(this);
    QThreadPool::globalInstance()->start([session, fresh, guard, stop = windowStopToken()]() {
        std::vector<ImageListProcessHelpers::ImageListEntry> pending;
        QString failedPath;
        an::Status failure;
        for (const QString& path : fresh)
        {
            an::Result<ImageListProcessHelpers::ImageListEntry> probed = probeFile(*session, path, stop);
            if (!probed.hasValue())
            {
                failedPath = path;
                failure = probed.status();
                break;
            }
            pending.push_back(std::move(*probed));
            if (pending.size() == kAddBatchSize)
            {
                auto shipped =
                    std::make_shared<std::vector<ImageListProcessHelpers::ImageListEntry>>(std::move(pending));
                pending = {};
                postToGui([guard, shipped]() {
                    if (guard)
                    {
                        guard->addItems(std::move(*shipped));
                    }
                });
            }
        }
        auto shipped = std::make_shared<std::vector<ImageListProcessHelpers::ImageListEntry>>(std::move(pending));
        postToGui([guard, shipped, failedPath, failure]() {
            if (guard)
            {
                guard->addItems(std::move(*shipped));
                guard->addingFinished(failedPath, failure);
            }
        });
    });
}

void NewSequenceControl::addItems(std::vector<ImageListProcessHelpers::ImageListEntry>&& files)
{
    for (ImageListProcessHelpers::ImageListEntry& item : files)
    {
        const std::string pathText = item.path.toUtf8().toStdString();
        if (!m_image_paths_set.insert(pathText).second)
        {
            continue;
        }
        const int row = m_ui.tableWidget->rowCount();
        m_ui.tableWidget->insertRow(row);
        m_ui.tableWidget->setItem(row, 0, new QTableWidgetItem(item.path));

        const QFileInfo info(item.path);
        m_ui.tableWidget->setItem(row, 1, new QTableWidgetItem(info.fileName()));

        const an::AcquisitionProjection projected = an::projectAcquisition(item.metadata.codec());
        std::optional<an::UtcInstant> created = projected.creationTime;
        if (!created.has_value())
        {
            created = item.metadata.acquisition().exposureStart();
        }
        const QString date = created.has_value()
                                 ? tableTime(*created)
                                 : info.lastModified().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));
        m_ui.tableWidget->setItem(row, 2, new QTableWidgetItem(date));

        const an::ImageExtent& extent = item.descriptor.extent();
        m_ui.tableWidget->setItem(
            row, 3, new QTableWidgetItem(QStringLiteral("%1x%2").arg(extent.width()).arg(extent.height())));

        const std::optional<std::pair<bool, std::int64_t>> isoOrGain =
            ImageListProcessHelpers::isoSpeedOrGainOf(item.metadata);
        m_ui.tableWidget->setItem(
            row, 4,
            new QTableWidgetItem(isoOrGain.has_value() ? QString::number(isoOrGain->second) : QStringLiteral("-")));

        const std::optional<std::int64_t> offset = ImageListProcessHelpers::cameraOffsetOf(item.metadata);
        m_ui.tableWidget->setItem(
            row, 5, new QTableWidgetItem(offset.has_value() ? QString::number(*offset) : QStringLiteral("-")));

        const std::optional<double> exposure = ImageListProcessHelpers::exposureSecondsOf(item.metadata);
        m_ui.tableWidget->setItem(
            row, 6, new QTableWidgetItem(exposure.has_value() ? QString::number(*exposure) : QStringLiteral("-")));

        m_ui.tableWidget->setItem(
            row, 7, new QTableWidgetItem(item.descriptor.isMosaiced() ? QStringLiteral("Yes") : QStringLiteral("No")));
        m_ui.tableWidget->setItem(row, 8, new QTableWidgetItem(depthText(item.descriptor.sampleType())));

        m_image_list.push_back(std::move(item));
    }

    tableUpdated();
}

void NewSequenceControl::addingFinished(const QString& failedPath, const an::Status& failure)
{
    if (m_AddsOutstanding > 0u)
    {
        --m_AddsOutstanding;
    }
    if (m_AddsOutstanding == 0u)
    {
        m_TableSpinner->stop();
    }
    if (failure.failed() && failure.code() != an::ErrorCode::Cancelled)
    {
        LOG_WARNING("New Sequence: '{}' could not be read: {}", failedPath.toStdString(), failure.message());
        if (auto* dialogs = process_utils::currentDialogService())
        {
            dialogs->notifyErrorMessageAsync(this, QStringLiteral("Error loading image metadata"),
                                             QStringLiteral("Could NOT load image %1. Error desc: %2")
                                                 .arg(failedPath, QString::fromStdString(failure.message())));
        }
    }
}

void NewSequenceControl::removeItems(const std::vector<QString>& items)
{
    ImageListProcessHelpers::removeItemsFromImageList(items, m_ui.tableWidget, m_image_list, m_image_paths_set);
    m_CurrentSelectedRow = -1;
    tableUpdated();
}

const std::vector<ImageListProcessHelpers::ImageListEntry>& NewSequenceControl::entries() const
{
    return m_image_list;
}

bool NewSequenceControl::isIdle() const
{
    return m_AddsOutstanding == 0u && m_PreviewsAsked == m_PreviewsLanded && !m_Running;
}

std::uint64_t NewSequenceControl::previewsShown() const
{
    return m_PreviewsShown;
}

std::shared_ptr<const an::consumer::ImageAsset> NewSequenceControl::previewImage() const
{
    return m_PreviewImage;
}

void NewSequenceControl::requestPreview(int row)
{
    const an::consumer::EngineSession* session = currentSession();
    if (session == nullptr || row < 0 || row >= static_cast<int>(m_image_list.size()))
    {
        return;
    }
    const an::ui::CommitTicket ticket = m_Previews.issueUnbound();
    ++m_PreviewsAsked;
    m_ImagePreviewSpinner->start();
    QPointer<NewSequenceControl> guard(this);
    const QString path = m_image_list[static_cast<std::size_t>(row)].path;
    QThreadPool::globalInstance()->start([session, path, ticket, guard]() {
        auto previewed = std::make_shared<an::Result<FilePreview>>(previewFile(*session, path, ticket.stopToken()));
        postToGui([guard, ticket, previewed]() {
            if (!guard)
            {
                return;
            }
            ++guard->m_PreviewsLanded;
            if (guard->m_PreviewsLanded == guard->m_PreviewsAsked)
            {
                guard->m_ImagePreviewSpinner->stop();
            }
            if (ticket.verdict() != an::ui::CommitVerdict::Commit)
            {
                return;
            }
            if (!previewed->hasValue())
            {
                LOG_WARNING("New Sequence: no preview: {}", previewed->status().message());
                guard->showNoPreview();
                return;
            }
            guard->m_PreviewImage = (*previewed)->image;
            guard->m_ui.imagePreviewWidget->setPixmap(QPixmap::fromImage(an::ui::presentedQImage((*previewed)->shown)),
                                                      true);
            guard->m_ui.imagePreviewWidget->setPreview((*previewed)->shown, (*previewed)->image);
            guard->m_ui.imagePreviewWidget->setQuickZoomEnabled(true);
            guard->m_Histogram->count((*previewed)->image);
            ++guard->m_PreviewsShown;
            guard->updateImageSummary();
        });
    });
}

void NewSequenceControl::showNoPreview()
{
    m_PreviewImage.reset();
    m_ui.imagePreviewWidget->setPixmap(m_NoImageLoadedPixmap, false);
    m_ui.imagePreviewWidget->setPreview(nullptr, nullptr);
    m_ui.imagePreviewWidget->setQuickZoomEnabled(false);
    if (m_Histogram)
    {
        m_Histogram->count(nullptr);
    }
}

void NewSequenceControl::on_addFilesButtonPushed()
{
    QStringList selected;
    if (auto* context = an::ui::appContext())
    {
        if (auto* fileDialogs = context->getFileDialogService())
        {
            selected = fileDialogs->selectImageFiles(this, QStringLiteral("Select Files"),
                                                     QStringLiteral("__NEW_SEQ_ADD_FILES__"));
        }
    }
    addItemsNonBlocking(selected);
}

void NewSequenceControl::on_removeSelectedButtonPushed()
{
    const std::vector<QString> files = process_utils::selectedFilesFromTable(m_ui.tableWidget);
    if (!files.empty())
    {
        removeItems(files);
    }
}

void NewSequenceControl::on_moveUpButtonPushed()
{
    if (m_CurrentSelectedRow > 0)
    {
        process_utils::swapTableRows(m_ui.tableWidget, m_CurrentSelectedRow, m_CurrentSelectedRow - 1);
        std::swap(m_image_list[static_cast<std::size_t>(m_CurrentSelectedRow)],
                  m_image_list[static_cast<std::size_t>(m_CurrentSelectedRow - 1)]);
        m_CurrentSelectedRow = m_CurrentSelectedRow - 1;
        m_ui.tableWidget->selectRow(m_CurrentSelectedRow);
        m_ui.currentFrameLabel->setText(QString::number(m_CurrentSelectedRow + 1));
    }
}

void NewSequenceControl::on_moveDownButtonPushed()
{
    if (m_CurrentSelectedRow + 1 < m_ui.tableWidget->rowCount())
    {
        process_utils::swapTableRows(m_ui.tableWidget, m_CurrentSelectedRow, m_CurrentSelectedRow + 1);
        std::swap(m_image_list[static_cast<std::size_t>(m_CurrentSelectedRow)],
                  m_image_list[static_cast<std::size_t>(m_CurrentSelectedRow + 1)]);
        m_CurrentSelectedRow = m_CurrentSelectedRow + 1;
        m_ui.tableWidget->selectRow(m_CurrentSelectedRow);
        m_ui.currentFrameLabel->setText(QString::number(m_CurrentSelectedRow + 1));
    }
}

void NewSequenceControl::on_clearAllButtonPushed()
{
    clearData();
}

void NewSequenceControl::on_debayerCFA_clicked(Qt::CheckState state)
{
    m_ui.bayerPatternSelector->setEnabled(state == Qt::Checked);
}

void NewSequenceControl::closeEvent(QCloseEvent* evt)
{
    // Everything the window started stops with the window's token, requested as the base closes, and lands on a window
    // that is gone; nothing is waited for.
    m_Previews.cancel();
    if (m_Progress)
    {
        m_Progress->requestCancel();
    }
    TopLevelProcessBaseControl::closeEvent(evt);
}

void NewSequenceControl::initialize()
{
    m_ui.addFiles->setEnabled(true);

    m_ui.removeSelected->setEnabled(false);
    m_ui.clearAll->setEnabled(false);

    m_ui.currentFrameLabel->setText(QStringLiteral("0"));
    m_ui.totalFramesLabel->setText(QStringLiteral("0"));

    m_ui.bayerPatternSelector->addItem(an::ui::kBayerPatternFromImageText);
    m_ui.bayerPatternSelector->addItem(an::ui::kBayerPatternRggbText);
    m_ui.bayerPatternSelector->addItem(an::ui::kBayerPatternBggrText);
    m_ui.bayerPatternSelector->addItem(an::ui::kBayerPatternGbrgText);
    m_ui.bayerPatternSelector->addItem(an::ui::kBayerPatternGrbgText);
    m_ui.bayerPatternSelector->setCurrentText(an::ui::kBayerPatternFromImageText);
    m_ui.bayerPatternSelector->setEnabled(false);

    m_ui.sequenceTypeSelector->addItem(kImageListKind);
    m_ui.sequenceTypeSelector->addItem(kSerKind);
    m_ui.sequenceTypeSelector->addItem(kFitsKind);

    // The engine writes no FITS frame container, so the choice is shown and cannot be made.
    if (auto* model = qobject_cast<QStandardItemModel*>(m_ui.sequenceTypeSelector->model()))
    {
        if (QStandardItem* item = model->item(2))
        {
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        }
    }
    m_ui.sequenceTypeSelector->setCurrentText(kImageListKind);

    updateImageSummary();

    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
}

void NewSequenceControl::initializeTable()
{
    m_ui.tableWidget->setAlternatingRowColors(true);
    m_ui.tableWidget->setColumnCount(9);
    m_ui.tableWidget->setHeaderLabels(
        {"Path", "File", "Date", "Size", "ISO/Gain", "Offset", "Exposure", "CFA", "Depth"});
    m_ui.tableWidget->setupSelectionColors();
}

void NewSequenceControl::createSequence()
{
    if (m_Running)
    {
        return;
    }
    const auto refuse = [this](const QString& message) {
        if (auto* dialogs = process_utils::currentDialogService())
        {
            dialogs->notifyErrorMessageAsync(
                this, QStringLiteral("Error creating sequence"),
                QStringLiteral("An exception occurred when creating sequence: %1").arg(message));
        }
    };
    if (m_image_list.empty())
    {
        refuse(QStringLiteral("No image selected"));
        return;
    }
    if (!ImageListProcessHelpers::validateImageListSizes(m_image_list))
    {
        refuse(QStringLiteral("Some images in the list have different size"));
        return;
    }
    if (!ImageListProcessHelpers::validateImageListColorDepth(m_image_list))
    {
        refuse(QStringLiteral("Some images in the list have different color depth"));
        return;
    }
    const an::consumer::EngineSession* session = currentSession();
    auto* temporaryFiles = an::ui::appContext() ? an::ui::appContext()->getTempFileService() : nullptr;
    if (session == nullptr || temporaryFiles == nullptr)
    {
        refuse(QStringLiteral("Temp file service is unavailable"));
        return;
    }

    an::ui::NewSequenceRequest request;
    request.files = m_image_list;
    request.kind = m_ui.sequenceTypeSelector->currentText() == kSerKind ? an::ui::NewSequenceKind::Ser
                                                                        : an::ui::NewSequenceKind::ImageList;
    const an::SequenceFormat format =
        request.kind == an::ui::NewSequenceKind::Ser ? an::SequenceFormat::Ser : an::SequenceFormat::ImageList;
    request.path = QString::fromStdString(temporaryFiles->createTempFilePathForNewSequence(format));
    request.framesPerSecond = m_ui.fpsValue->value();
    request.demosaic = m_ui.debayerCFA->isChecked();
    request.pattern = an::ui::bayerPatternChoiceFromText(m_ui.bayerPatternSelector->currentText());
    request.method = an::ui::demosaicMethodFromSettings();
    request.createdSeconds = static_cast<std::uint64_t>((std::max)(qint64{0}, QDateTime::currentSecsSinceEpoch()));
    QString name = m_ui.sequenceNameEdit->text().trimmed();
    if (name.isEmpty())
    {
        name = QStringLiteral("NEW_SEQUENCE");
    }

    auto* progress = new ProgressDialogControl(this, QStringLiteral("Create Sequence"));
    progress->setProgressBarRange(0, 100);
    progress->setText(request.kind == an::ui::NewSequenceKind::Ser
                          ? QStringLiteral("Creating %1 %2...").arg(kSerKind, name)
                          : QStringLiteral("Creating an image sequence %1...").arg(name));
    progress->show();
    m_Progress = progress;
    setRunning(true);

    QPointer<NewSequenceControl> guard(this);
    QPointer<ProgressDialogControl> dialog(progress);
    auto shared = std::make_shared<const an::ui::NewSequenceRequest>(std::move(request));
    const bool demosaic = shared->demosaic;
    QThreadPool::globalInstance()->start([session, shared, guard, dialog, name, demosaic, format,
                                          windowStop = windowStopToken(), cancel = progress->stopToken()]() {
        const LinkedStop runStop(windowStop, cancel);
        const std::stop_token stop = runStop.token();
        const an::Status made =
            an::ui::makeNewSequence(*session, *shared, stop, [dialog](std::uint64_t written, std::uint64_t total) {
                const int percent = total > 0u ? static_cast<int>((written * 100u) / total) : 0;
                postToGui([dialog, percent]() {
                    if (dialog)
                    {
                        dialog->setValue(percent);
                    }
                });
            });
        auto opened = std::make_shared<an::Result<an::ui::ImageDocument>>(made);
        if (made.ok())
        {
            *opened = an::ui::openSequenceDocument(*session, shared->path, format, stop);
            if (!opened->hasValue())
            {
                // Published but not opened: nothing will show it.
                (void)an::ui::removeSequenceFromDisk(shared->path);
            }
        }
        postToGui([guard, dialog, opened, path = shared->path, name, demosaic,
                   framesPerSecond = shared->framesPerSecond, method = shared->method]() {
            if (dialog)
            {
                dialog->closeAsync();
            }
            if (!guard)
            {
                if (opened->hasValue())
                {
                    (void)an::ui::removeSequenceFromDisk(path);
                }
                return;
            }
            if (opened->hasValue())
            {
                if (std::optional<an::ui::DocumentSequence> sequence = (*opened)->sequence(); sequence.has_value())
                {
                    if (!sequence->descriptor.nominalFrameRate.has_value())
                    {
                        sequence->descriptor.nominalFrameRate = an::ui::sequenceRateOf(framesPerSecond);
                    }
                    sequence->demosaic = method;
                    (*opened)->setSequence(std::move(sequence));
                }
            }
            guard->creationFinished(path, name, demosaic, std::move(*opened));
        });
    });
}

void NewSequenceControl::creationFinished(const QString& path, const QString& name, bool demosaic,
                                          an::Result<an::ui::ImageDocument>&& made)
{
    setRunning(false);
    m_Progress.clear();
    if (!made.hasValue())
    {
        if (made.status().code() == an::ErrorCode::Cancelled)
        {
            LOG_INFO("New Sequence: making '{}' was cancelled", name.toStdString());
            return;
        }
        if (auto* dialogs = process_utils::currentDialogService())
        {
            dialogs->notifyErrorMessageAsync(this, QStringLiteral("Error creating sequence"),
                                             QStringLiteral("An exception occurred when creating sequence: %1")
                                                 .arg(QString::fromStdString(made.status().message())));
        }
        return;
    }
    made->setNames(an::ui::DocumentNames{.displayName = name, .sourcePath = path});
    ImageViewControl* view = nullptr;
    if (auto* factory = process_utils::currentViewFactory())
    {
        view = factory->createNewImageViewControlFromSequence(nullptr, std::move(*made), nullptr, demosaic,
                                                              "New sequence manually created from image list");
    }
    if (view == nullptr)
    {
        (void)an::ui::removeSequenceFromDisk(path);
        if (auto* dialogs = process_utils::currentDialogService())
        {
            dialogs->notifyErrorMessageAsync(this, QStringLiteral("Error creating sequence"),
                                             QStringLiteral("Could not create the sequence view"));
        }
        return;
    }
    LOG_INFO("New Sequence: '{}' made at '{}'", name.toStdString(), path.toStdString());
    view->fitToWindowAsync();
    close();
}

void NewSequenceControl::setRunning(bool running)
{
    m_Running = running;
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(!running && m_ui.tableWidget->rowCount() > 1);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(!running);
}

void NewSequenceControl::clearData()
{
    ImageListProcessHelpers::clearImageListData(m_ui.tableWidget, m_image_list, m_image_paths_set,
                                                m_IgnoreLastRowSelectedEvent);
    tableUpdated();
}

void NewSequenceControl::tableUpdated()
{
    const int rows = m_ui.tableWidget->rowCount();
    if (rows == 0)
    {
        m_ui.removeSelected->setEnabled(false);
        m_ui.clearAll->setEnabled(false);
        m_Previews.cancel();
        showNoPreview();
        m_ui.moveUpButton->setEnabled(false);
        m_ui.moveDownButton->setEnabled(false);
        m_CurrentSelectedRow = -1;
        m_ui.histogramWidget->setClippingInfoEnabled(false);
    }
    else
    {
        m_ui.removeSelected->setEnabled(true);
        m_ui.clearAll->setEnabled(true);
        m_ui.moveUpButton->setEnabled(m_CurrentSelectedRow > 0);
        m_ui.moveDownButton->setEnabled(m_CurrentSelectedRow >= 0 && m_CurrentSelectedRow + 1 < rows);
        m_ui.histogramWidget->setClippingInfoEnabled(true);
    }

    const int selectedRow = (std::max)(m_CurrentSelectedRow, 0) + 1;
    m_ui.currentFrameLabel->setText(QString::number(rows == 0 ? 0 : selectedRow));
    m_ui.totalFramesLabel->setText(QString::number(rows));

    if (rows > 0 && m_CurrentSelectedRow == -1)
    {
        m_ui.tableWidget->selectRow(0);
    }

    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(!m_Running && rows > 1);
    updateImageSummary();
}

void NewSequenceControl::updateImageSummary()
{
    if (m_CurrentSelectedRow < 0 || m_CurrentSelectedRow >= static_cast<int>(m_image_list.size()))
    {
        m_ui.imageSummary->setText(QStringLiteral("<h2>The image list is empty</h2>"));
        return;
    }
    const ImageListProcessHelpers::ImageListEntry& entry = m_image_list[static_cast<std::size_t>(m_CurrentSelectedRow)];
    m_ui.imageSummary->setText(QString::fromUtf8(
        getImageFullReport(entry.descriptor, entry.metadata, reportFileFacts(entry.path.toUtf8().toStdString()))));
}

void NewSequenceControl::on_dialog_box_button_clicked(QAbstractButton* button)
{
    auto* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
    {
        return;
    }
    const QDialogButtonBox::StandardButton standard = buttonBox->standardButton(button);
    if (standard == QDialogButtonBox::Apply)
    {
        createSequence();
    }
    else if (standard == QDialogButtonBox::Close)
    {
        close();
    }
}
