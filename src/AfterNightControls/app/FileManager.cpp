// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/FileManager.h"

#include "app/FileDialogUtils.h"
#include "app/FilePolicies.h"
#include "app/FileWorkflowRunner.h"
#include "app/AfterNightAppUiBuilder.h"

#include "controls/AutostretchApply.h"
#include "controls/DashboardUtils.h"
#include "controls/FolderTrackerPersistence.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewFiles.h"
#include "controls/ProgressDialogControl.h"
#include "controls/ThumbnailPersistence.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "session/IEngineSessionProvider.h"

#include "app/AppContextHelpers.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>
#include <AfterNightEngine/Algorithms/Kernel/SamplingKernel.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <AfterNightPlatform/Logging.h>

#include <QFile>
#include <QPointer>
#include <QThreadPool>
#include <QUuid>

#include <filesystem>
#include <map>
#include <type_traits>
#include <variant>

namespace
{

constexpr size_t DEFAULT_RECENT_FILES_LIMIT = 20;

GlobalSettingsPersistence* resolveGlobalSettings(const an::ui::FileManager::Dependencies& deps)
{
    return deps.globalSettingsPersistence;
}

QWidget* resolveDialogParent(const an::ui::FileManager::Dependencies& deps)
{
    if (auto* mainWindow = process_utils::currentMainWindow())
    {
        return mainWindow;
    }

    return qobject_cast<QWidget*>(deps.signalContext);
}

QObject* resolveSignalContext(const an::ui::FileManager::Dependencies& deps)
{
    if (deps.signalContext)
    {
        return deps.signalContext;
    }

    return process_utils::currentGuiThreadTarget();
}

void persistLastOpenDirectory(const an::ui::FileManager::Dependencies& deps, const QString& path)
{
    if (deps.persistLastOpenDirectory)
    {
        deps.persistLastOpenDirectory(path);
        return;
    }

    if (deps.lastOpenDirectory)
    {
        *deps.lastOpenDirectory = path.toUtf8().toStdString();
    }
}

QString resolveThumbnailsDirPath(const an::ui::FileManager::Dependencies& deps)
{
    return deps.thumbnailsDirPath ? deps.thumbnailsDirPath() : QString();
}

/// A failure the file manager decided, which no engine call produced.
an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// What reducing an image to its thumbnail does: the picture is restated and its solution no longer places it, while
/// the cards the file stated - a camera's EXIF record among them - still describe it and every written sample is valid.
constexpr an::OperationEffect kThumbnailEffect{.description = an::DescriptionEffect::Restates,
                                               .solution = an::SolutionEffect::Invalidates,
                                               .codecMetadata = an::CodecMetadataEffect::Preserves,
                                               .outputIntent = an::OutputIntentEffect::Withdraws,
                                               .validity = {.policy = an::ValidityPolicy::Preserve}};

/// Whether an image is linear data a thumbnail stretches: its processing-state cards record it linear, or it is a
/// camera RAW frame, whose codes are linear.
bool thumbnailIsLinear(const an::ImageMetadata& metadata)
{
    return an::readProcessingStateCards(metadata.codec()).linearity == an::RecordedLinearity::Linear ||
           metadata.acquisition().rawCamera().has_value();
}

/// The thumbnail's picture of a typed image: reduced to the thumbnail's size with a Lanczos kernel, a mosaic described
/// as the grey picture its codes make. Null when the image needs neither.
template <typename SampleT>
an::Result<std::optional<an::consumer::AnyImage>> reduceForThumbnail(const an::consumer::OperationScope& scope,
                                                                     const an::Image<SampleT>& image,
                                                                     const an::ValidityMask* validity)
{
    using Reduced = std::optional<an::consumer::AnyImage>;
    const std::optional<an::ImageDescriptor> stored = image.descriptor();
    const auto view = image.view();
    if (!stored.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    const QSize sourceSize(static_cast<int>(stored->extent().width()), static_cast<int>(stored->extent().height()));
    const QSize thumbnailSize = an::ui::recentFileThumbnailSize(sourceSize);
    if (thumbnailSize == sourceSize && !stored->isMosaiced())
    {
        return Reduced{};
    }
    an::ImageDescriptor picture = *stored;
    if (stored->isMosaiced())
    {
        // A thumbnail is a picture of the codes, not a mosaic to be resolved.
        an::ImageDescriptorRequest grey = stored->toRequest();
        grey.cfa = an::CfaDescriptor{};
        const std::optional<an::ImageDescriptor> described = an::ImageDescriptor::create(grey);
        if (!described.has_value())
        {
            return refusal(an::ErrorCode::IncompatibleArguments, "the mosaic has no grey description");
        }
        picture = *described;
    }
    an::ResizeRequest request;
    request.width = static_cast<std::uint32_t>(thumbnailSize.width());
    request.height = static_cast<std::uint32_t>(thumbnailSize.height());
    request.sampling.interpolation = an::KernelInterpolation::Lanczos4;
    const an::Result<an::GeometryPlan> plan = an::planResize(picture, request);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::Result<an::Image<SampleT>> reduced =
        an::Image<SampleT>::createDestination(scope.engine(), plan->destinationDescriptor(),
                                              an::StorageArrangement::Planar, an::StorageInitialization::Zeroed, image);
    if (!reduced.hasValue())
    {
        return reduced.status();
    }
    an::Result<an::ValidityMask> written =
        an::ValidityMask::create(scope.engine(), plan->destinationDescriptor().extent());
    if (!written.hasValue())
    {
        return written.status();
    }
    std::optional<an::ValidityMaskView<std::uint8_t>> writtenView = written->view();
    if (!writtenView.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the thumbnail's mask holds nothing");
    }
    std::optional<an::ValidityMaskView<const std::uint8_t>> sourceMask;
    if (validity != nullptr)
    {
        sourceMask = validity->view();
    }
    an::GeometrySelection selection;
    selection.validity = image.metadata().semantics().validity();
    selection.mask = sourceMask.has_value() ? &*sourceMask : nullptr;
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = reduced->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the thumbnail holds nothing");
        }
        const an::Result<an::GeometryResult> ran =
            an::transformImageGeometry(scope.execution(), an::kSampleTypeOf<SampleT>, view->data(), view->layout(),
                                       access->view().data(), access->view().layout(), *plan, selection, *writtenView);
        if (!ran.hasValue())
        {
            return ran.status();
        }
    }
    const an::Result<an::MetadataTransition> change =
        an::operationTransition(kThumbnailEffect, an::OperationOutcome{.operation = "an::transformImageGeometry",
                                                                       .detail = "the recent file's thumbnail"});
    if (!change.hasValue())
    {
        return change.status();
    }
    if (an::Status stated = reduced->transition(*change); stated.failed())
    {
        return stated;
    }
    return Reduced(an::consumer::AnyImage(std::move(*reduced)));
}

class ScopedTopLevelFlagRestorer
{
public:
    explicit ScopedTopLevelFlagRestorer(Qt::WindowFlags clearFlag = Qt::WindowStaysOnTopHint) : m_clearFlag(clearFlag)
    {
        const auto topLevels = QApplication::topLevelWidgets();
        for (QWidget* w : topLevels)
        {
            if (!w || !w->isVisible())
            {
                continue;
            }

            const Qt::WindowFlags flags = w->windowFlags();
            if (flags & clearFlag)
            {
                m_originalFlags[w] = flags;
                w->setWindowFlags(flags & ~clearFlag);
                w->hide();
                w->show();
            }
        }
    }

    ~ScopedTopLevelFlagRestorer()
    {
        for (auto it = m_originalFlags.begin(); it != m_originalFlags.end(); ++it)
        {
            QWidget* w = it.key();
            const Qt::WindowFlags flags = it.value();
            if (w)
            {
                w->setWindowFlags(flags);
                w->hide();
                w->show();
            }
        }
    }

private:
    QMap<QWidget*, Qt::WindowFlags> m_originalFlags;
    Qt::WindowFlags m_clearFlag;
};

template <typename SettingsT>
static void initializeImageFileDialog(const std::string& defaultDir, const std::string& defaultFile,
                                      QFileDialog& dialog, const QFileDialog::AcceptMode acceptMode,
                                      const bool acceptSequences, const int onlySequences,
                                      const QString& sourceSequencePath, const bool acceptMultipleFiles,
                                      SettingsT* globalSettingsPersistence)
{
    if (acceptMode == QFileDialog::AcceptOpen)
    {
        an::ui::dialogs::applyCompactOpenFilterPresentation(dialog);
    }

    if (defaultDir.empty())
    {
        const QStringList picturesLocations = QStandardPaths::standardLocations(QStandardPaths::PicturesLocation);
        dialog.setDirectory(picturesLocations.isEmpty() ? QDir::currentPath() : picturesLocations.last());
    }
    else
    {
        dialog.setDirectory(QString::fromUtf8(defaultDir));
    }

    // A save names a file that need not exist yet; an open chooses one or more that do.
    if (acceptMode == QFileDialog::AcceptSave)
    {
        dialog.setFileMode(QFileDialog::AnyFile);
    }
    else
    {
        dialog.setFileMode(acceptMultipleFiles ? QFileDialog::ExistingFiles : QFileDialog::ExistingFile);
    }

    QStringList allSupportedPatterns;
    QStringList nameFilters;

    if (!onlySequences)
    {
        nameFilters.append("PNG files (*.png)");
        nameFilters.append("JPEG files (*.jpg *.jpeg)");
        nameFilters.append("TIFF files (*.tif *.tiff)");
        nameFilters.append("FITS files (*.fit *.fits)");
        nameFilters.append("XISF files (*.xisf)");

        if (acceptMode != QFileDialog::AcceptSave)
        {
            nameFilters.append("Canon RAW (*.crw *.cr2 *.cr3)");
            nameFilters.append("Nikon RAW (*.nef)");
            nameFilters.append("Sony RAW (*.arw *.sr2 *.srf)");
            nameFilters.append("Adobe DNG (*.dng)");
            nameFilters.append("Kodak RAW (*.dcr *.k25 *.kdc)");
            nameFilters.append("Epson RAW (*.erf)");
            nameFilters.append("Minolta RAW (*.mrw)");
            nameFilters.append("Olympus RAW (*.orf)");
            nameFilters.append("Pentax RAW (*.pef)");
            nameFilters.append("Fuji RAW (*.raf)");
            nameFilters.append("Panasonic RAW (*.raw)");
            nameFilters.append("Sigma RAW (*.x3f)");

            allSupportedPatterns += an::ui::dialogs::supportedImageOpenNameFilterPatterns();
        }
        else
        {
            allSupportedPatterns << "*.png" << "*.jpg" << "*.jpeg" << "*.tif" << "*.tiff" << "*.fit" << "*.fits"
                                 << "*.xisf";
        }
    }

    if (acceptSequences)
    {
        if (acceptMode == QFileDialog::AcceptSave)
        {
            if (onlySequences == 1)
            {
                nameFilters.append("SER sequence (*.ser)");
            }
            else if (onlySequences == 2)
            {
                nameFilters.append("AfterNight sequence (*.anseq)");
            }
            else if (onlySequences == 3)
            {
                const QString videoSuffix = an::ui::resolveSequenceSaveSuffix(onlySequences, sourceSequencePath, {});
                nameFilters.append(QObject::tr("Video sequence (*.%1)").arg(videoSuffix));
            }
        }
        else
        {
            nameFilters.append("SER sequence (*.ser)");
            nameFilters.append("AVI sequence (*.avi)");
            nameFilters.append("MP4 sequence (*.mp4)");
            nameFilters.append("MOV sequence (*.mov)");
            nameFilters.append("MKV sequence (*.mkv)");
            nameFilters.append("MPG sequence (*.mpg)");
            nameFilters.append("WEBM sequence (*.webm)");
            nameFilters.append("MPEG sequence (*.mpeg)");
            nameFilters.append("AfterNight sequence (*.anseq)");
        }

        allSupportedPatterns += an::ui::dialogs::supportedSequenceOpenNameFilterPatterns();
    }

    const QString allSupportedFilter = an::ui::dialogs::supportedFormatsNameFilter(allSupportedPatterns);
    nameFilters.prepend(allSupportedFilter);

    dialog.setNameFilters(nameFilters);
    dialog.selectNameFilter(allSupportedFilter);
    dialog.setAcceptMode(acceptMode);

    if (acceptMode == QFileDialog::AcceptSave)
    {
        if (onlySequences == 1)
        {
            dialog.setDefaultSuffix("ser");
        }
        else if (onlySequences == 2)
        {
            dialog.setDefaultSuffix("anseq");
        }
        else if (onlySequences == 3)
        {
            const QString videoSuffix = an::ui::resolveSequenceSaveSuffix(onlySequences, sourceSequencePath, {});
            dialog.setDefaultSuffix(videoSuffix);
            dialog.selectNameFilter(QObject::tr("Video sequence (*.%1)").arg(videoSuffix));
        }
        else
        {
            QString defaultFormat = QString(SettingsDefaults::GENERAL_DEFAULT_SAVE_FORMAT);
            if (globalSettingsPersistence)
            {
                defaultFormat = globalSettingsPersistence->select(
                    SettingsKeys::GENERAL_DEFAULT_SAVE_FORMAT, QString(SettingsDefaults::GENERAL_DEFAULT_SAVE_FORMAT));
            }

            QString suffix = an::ui::resolveSequenceSaveSuffix(onlySequences, {}, defaultFormat);
            QString filterToSelect;

            if (defaultFormat == "tiff")
            {
                suffix = "tiff";
                filterToSelect = "TIFF files (*.tif *.tiff)";
            }
            else if (defaultFormat == "fits")
            {
                suffix = "fits";
                filterToSelect = "FITS files (*.fit *.fits)";
            }
            else if (defaultFormat == "png")
            {
                suffix = "png";
                filterToSelect = "PNG files (*.png)";
            }
            else if (defaultFormat == "xisf")
            {
                suffix = "xisf";
                filterToSelect = "XISF files (*.xisf)";
            }
            else if (defaultFormat == "jpeg" || defaultFormat == "jpg")
            {
                suffix = "jpg";
                filterToSelect = "JPEG files (*.jpg *.jpeg)";
            }

            dialog.setDefaultSuffix(suffix);

            if (!filterToSelect.isEmpty())
            {
                dialog.selectNameFilter(filterToSelect);
            }
        }

        dialog.selectFile(QString::fromUtf8(defaultFile));
    }
}

} // anonymous namespace

namespace an::ui
{

an::Result<std::shared_ptr<const an::consumer::ImageAsset>>
renderRecentFileThumbnail(const an::consumer::EngineSession& session,
                          const std::shared_ptr<const an::consumer::ImageAsset>& image, std::stop_token stop)
{
    if (image == nullptr)
    {
        return refusal(an::ErrorCode::NotInitialized, "there is no image to make a thumbnail of");
    }
    bool linear = false;
    std::shared_ptr<const an::consumer::ImageAsset> picture = image;
    {
        an::Result<an::consumer::AssetReadLease> lease = image->read(stop);
        if (!lease.hasValue())
        {
            return lease.status();
        }
        linear = lease->visit([](const auto& typed) { return thumbnailIsLinear(typed.metadata()); });
        an::Result<an::consumer::OperationScope> scope = session.admit(
            an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Preview, .stopToken = stop});
        if (!scope.hasValue())
        {
            return scope.status();
        }
        an::Result<std::optional<an::consumer::AnyImage>> reduced =
            lease->visit([&](const auto& typed) { return reduceForThumbnail(*scope, typed, lease->validity()); });
        if (!reduced.hasValue())
        {
            return reduced.status();
        }
        scope->end();
        if (reduced->has_value())
        {
            an::Result<an::consumer::ImageAsset> adopted =
                an::consumer::ImageAsset::adopt(session, std::move(**reduced));
            if (!adopted.hasValue())
            {
                return adopted.status();
            }
            picture = std::make_shared<const an::consumer::ImageAsset>(std::move(*adopted));
        }
    }
    if (!linear)
    {
        return picture;
    }
    an::Result<an::consumer::AssetReadLease> lease = picture->read(stop);
    if (!lease.hasValue())
    {
        return lease.status();
    }
    // Each colour channel takes its own stretch, as the view's autostretch shows an image unless asked to link, so the
    // thumbnail's colours are the ones the view shows.
    an::ui::AutostretchApplySettings settings;
    settings.linkedChannels = false;
    an::Result<an::consumer::ImageAsset> stretched =
        an::ui::applyAutostretchToCopy(session, lease->image(), lease->validity(), settings, stop);
    if (!stretched.hasValue())
    {
        return stretched.status();
    }
    return std::shared_ptr<const an::consumer::ImageAsset>(
        std::make_shared<const an::consumer::ImageAsset>(std::move(*stretched)));
}

an::Status writeRecentFileThumbnail(const an::consumer::EngineSession& session,
                                    const std::shared_ptr<const an::consumer::ImageAsset>& image, const QString& path,
                                    std::stop_token stop)
{
    an::Result<std::shared_ptr<const an::consumer::ImageAsset>> thumbnail =
        renderRecentFileThumbnail(session, image, stop);
    if (!thumbnail.hasValue())
    {
        return thumbnail.status();
    }
    an::Result<an::OutputIntent> intent =
        an::OutputIntent::create(an::OutputIntentRequest{.sampleType = an::SampleType::UInt8,
                                                         .compression = an::OutputCompression::Jpeg,
                                                         .compressionLevel = kRecentFileThumbnailQuality,
                                                         .preserveSourceCards = false});
    if (!intent.hasValue())
    {
        return intent.status();
    }
    return writeViewImage(session, **thumbnail, path, *intent, stop);
}

/// The thumbnails asked for, by file: a thumbnail lands only while it is the last one asked for its file.
struct FileManager::ThumbnailRequests
{
    std::map<std::string, std::uint64_t> latest;
    std::uint64_t next = 0u;
};

FileManager::FileManager(const Dependencies& deps) : m_deps(deps), m_thumbnails(std::make_shared<ThumbnailRequests>())
{
}

FileManager::~FileManager() = default;

void FileManager::setPersistenceDependencies(ThumbnailPersistence* thumbnailPersistence,
                                             FolderTrackerPersistence* folderTrackerPersistence,
                                             GlobalSettingsPersistence* globalSettingsPersistence)
{
    m_deps.thumbnailPersistence = thumbnailPersistence;
    m_deps.folderTrackerPersistence = folderTrackerPersistence;
    m_deps.globalSettingsPersistence = globalSettingsPersistence;
}

FileWorkflowRunner::Dependencies FileManager::workflowDependencies() const
{
    FileWorkflowRunner::Dependencies workflow;
    workflow.context = resolveSignalContext(m_deps);
    workflow.dialogParent = resolveDialogParent(m_deps);
    workflow.globalSettingsPersistence = resolveGlobalSettings(m_deps);
    workflow.rawOpenPolicy = m_deps.rawOpenPolicy;
    const Dependencies deps = m_deps;
    workflow.persistLastOpenDirectory = [deps](const QString& path) { persistLastOpenDirectory(deps, path); };
    workflow.switchToDockManager = deps.switchToDockManager;
    // Every call below comes on the GUI thread from a run that ends when the signal context goes, as this manager does.
    workflow.addRecentFile = [this](const std::string& fileName,
                                    const std::shared_ptr<const an::consumer::ImageAsset>& image) {
        addRecentFile(fileName, image);
    };
    workflow.removeRecentFile = [this](const std::string& fileName) { removeRecentFile(fileName); };
    workflow.updateImageViewControlList = deps.updateImageViewControlList;
    workflow.openFinished = deps.openFinished;
    workflow.saveFinished = deps.saveFinished;
    return workflow;
}

void FileManager::openFiles(const QStringList& files, const QString& directory) const
{
    QStringList supportedFiles;
    supportedFiles.reserve(files.size());
    for (const auto& filePath : files)
    {
        if (an::ui::isLikelySupportedImagePath(filePath))
        {
            supportedFiles.append(filePath);
        }
    }

    if (supportedFiles.isEmpty())
    {
        return;
    }

    // Update path immediately so the next dialog starts from the right place, even if the open has not ended yet.
    persistLastOpenDirectory(m_deps, directory);

    auto* progress = new ProgressDialogControl(resolveDialogParent(m_deps), "File Open");
    progress->setProgressBarRange(0, supportedFiles.size() >= 3 ? 100 : 0);
    progress->show();

    FileWorkflowRunner::openFiles(workflowDependencies(), supportedFiles, directory, progress);
}

void FileManager::openDroppedFiles(const QStringList& files) const
{
    if (files.isEmpty())
    {
        return;
    }

    QStringList supportedFiles;
    for (const auto& filePath : files)
    {
        if (an::ui::isLikelySupportedImagePath(filePath))
        {
            supportedFiles.append(filePath);
        }
    }

    if (supportedFiles.isEmpty())
    {
        return;
    }

    openFiles(supportedFiles, QFileInfo(supportedFiles.back()).dir().absolutePath());
}

void FileManager::openFile() const
{
    if (!m_deps.lastOpenDirectory)
    {
        return;
    }

    QFileDialog dialog(resolveDialogParent(m_deps), QObject::tr("Open File"));
    if (auto* settings = resolveGlobalSettings(m_deps))
    {
        initializeImageFileDialog(*m_deps.lastOpenDirectory, std::string(), dialog, QFileDialog::AcceptOpen, true, 0,
                                  {}, true, settings);
        an::ui::dialogs::applyDialogBackendPreference(dialog, settings);
    }
    else
    {
        initializeImageFileDialog(*m_deps.lastOpenDirectory, std::string(), dialog, QFileDialog::AcceptOpen, true, 0,
                                  {}, true, process_utils::currentGlobalSettings());
        an::ui::dialogs::applyDialogBackendPreference(dialog, process_utils::currentGlobalSettings());
    }

    {
        ScopedTopLevelFlagRestorer _;

        if (dialog.exec() == QDialog::Rejected)
        {
            return;
        }
    }

    const QStringList selectedFiles = dialog.selectedFiles();
    const bool hasUnsupportedFile =
        std::any_of(selectedFiles.cbegin(), selectedFiles.cend(),
                    [](const QString& filePath) { return !an::ui::isLikelySupportedImagePath(filePath); });
    if (hasUnsupportedFile)
    {
        process_utils::notifyErrorMessage(
            resolveDialogParent(m_deps), QObject::tr("Unsupported File Type"),
            QObject::tr("AfterNight can only open supported image, RAW, and video files."));
        return;
    }

    openFiles(selectedFiles, dialog.directory().absolutePath());
}

void FileManager::openRecentSingleFile(const QString& filePath) const
{
    auto* progress = new ProgressDialogControl(resolveDialogParent(m_deps), "File Open");

    progress->setProgressBarRange(0, 0);
    progress->show();

    FileWorkflowRunner::openRecentFile(workflowDependencies(), filePath, progress);
}

void FileManager::saveFile() const
{
    if (!m_deps.lastOpenDirectory)
    {
        return;
    }

    auto* control = process_utils::currentActiveViewControl();

    if (!control || !control->hasImage())
    {
        process_utils::notifyErrorMessage(resolveDialogParent(m_deps), "File save error", "No selected image to save");
        return;
    }

    const auto controlName = control->getUniqueName();
    const std::string message = fmt::format("Save image <{}> as...", controlName);

    QFileDialog dialog(control, QString::fromUtf8(message));
    int sequenceType = 0;
    if (control->isSequence())
    {
        if (control->isSERSequence())
        {
            sequenceType = 1;
        }
        else if (control->isVideoSequence())
        {
            sequenceType = 3;
        }
        else
        {
            sequenceType = 2;
        }
    }

    const std::optional<DocumentSequence> sequence =
        control->isSequence() ? control->document().sequence() : std::nullopt;
    const QString sourceSequencePath = sequence.has_value() ? sequence->sourcePath : QString();

    if (auto* settings = resolveGlobalSettings(m_deps))
    {
        initializeImageFileDialog(*m_deps.lastOpenDirectory, controlName, dialog, QFileDialog::AcceptSave,
                                  control->isSequence(), sequenceType, sourceSequencePath, false, settings);
        an::ui::dialogs::applyDialogBackendPreference(dialog, settings);
    }
    else
    {
        initializeImageFileDialog(*m_deps.lastOpenDirectory, controlName, dialog, QFileDialog::AcceptSave,
                                  control->isSequence(), sequenceType, sourceSequencePath, false,
                                  process_utils::currentGlobalSettings());
        an::ui::dialogs::applyDialogBackendPreference(dialog, process_utils::currentGlobalSettings());
    }

    {
        ScopedTopLevelFlagRestorer _;

        if (dialog.exec() == QDialog::Rejected)
        {
            return;
        }
    }

    // Update path immediately so the next dialog starts from the right place.
    const QString selectedDir = dialog.directory().absolutePath();
    persistLastOpenDirectory(m_deps, selectedDir);

    auto* progress = new ProgressDialogControl(resolveDialogParent(m_deps), "Save File");
    progress->setProgressBarRange(0, 0);
    progress->show();

    FileWorkflowRunner::saveFile(workflowDependencies(), *control, dialog.selectedFiles(), selectedDir, progress);
}

void FileManager::saveViewToFile(ImageViewControl& view, const QString& path) const
{
    auto* progress = new ProgressDialogControl(resolveDialogParent(m_deps), "Save File");
    progress->setProgressBarRange(0, 0);
    progress->show();

    FileWorkflowRunner::saveFile(workflowDependencies(), view, QStringList{path}, QFileInfo(path).absolutePath(),
                                 progress);
}

size_t FileManager::getRecentFilesLimit() const
{
    if (m_deps.globalSettingsPersistence)
    {
        const QString limitStr = m_deps.globalSettingsPersistence->select(SettingsKeys::GENERAL_RECENT_FILES_LIMIT,
                                                                          QString::number(DEFAULT_RECENT_FILES_LIMIT));
        bool ok = false;
        const int limit = limitStr.toInt(&ok);
        if (ok && limit > 0)
        {
            return static_cast<size_t>(limit);
        }
    }

    return DEFAULT_RECENT_FILES_LIMIT;
}

void FileManager::persistRecentFiles() const
{
    if (!m_deps.localDataPath || !m_deps.recentFiles)
    {
        return;
    }

    QFile file(*m_deps.localDataPath + "/recent_files");
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out.setGenerateByteOrderMark(false);

    for (const auto& item : *m_deps.recentFiles)
    {
        out << QString::fromUtf8(item);
        out << '\n';
    }

    file.close();
}

void FileManager::loadRecentFilesFromDisk() const
{
    if (!m_deps.localDataPath || !m_deps.recentFiles)
    {
        return;
    }

    QFile file(*m_deps.localDataPath + "/recent_files");
    bool recentFilesSanitized = false;

    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QTextStream in(&file);
        in.setEncoding(QStringConverter::Utf8);

        const size_t recentFilesLimit = getRecentFilesLimit();
        QString line = in.readLine();
        while (!line.isNull())
        {
            const QString normalizedPath = DashboardUtils::normalizeRecentFilePath(line);
            recentFilesSanitized = recentFilesSanitized || (normalizedPath != line);

            if (!normalizedPath.isEmpty())
            {
                const std::string normalizedPathStd = normalizedPath.toUtf8().toStdString();
                const auto duplicateIt =
                    std::find(m_deps.recentFiles->begin(), m_deps.recentFiles->end(), normalizedPathStd);
                if (duplicateIt == m_deps.recentFiles->end())
                {
                    m_deps.recentFiles->push_back(normalizedPathStd);
                    if (m_deps.recentFiles->size() >= recentFilesLimit)
                    {
                        break;
                    }
                }
                else
                {
                    recentFilesSanitized = true;
                }
            }
            else
            {
                recentFilesSanitized = true;
            }

            line = in.readLine();
        }

        file.close();
    }

    if (recentFilesSanitized)
    {
        persistRecentFiles();
    }
}

void FileManager::updateRecentFilesMenuEntries() const
{
    QObject* signalContext = resolveSignalContext(m_deps);
    if (!m_deps.ui || !m_deps.recentFiles || !signalContext)
    {
        return;
    }

    m_deps.ui->menuOpen_Recent->clear();
    if (m_deps.recentFiles->empty())
    {
        m_deps.ui->menuOpen_Recent->setEnabled(false);
        return;
    }

    m_deps.ui->menuOpen_Recent->setEnabled(true);

    for (const auto& recentFileName : *m_deps.recentFiles)
    {
        const auto recentFilePath = QString::fromUtf8(recentFileName);
        auto* action = m_deps.ui->menuOpen_Recent->addAction(recentFilePath);
        QObject::connect(action, &QAction::triggered, signalContext,
                         [this, recentFilePath]() { openRecentSingleFile(recentFilePath); });
    }

    m_deps.ui->menuOpen_Recent->addSeparator();
    auto* clearAction = m_deps.ui->menuOpen_Recent->addAction("Clear recent files list");

    QObject::connect(clearAction, &QAction::triggered, signalContext, [this]() { clearRecentFilesList(); });
}

void FileManager::addRecentFile(const std::string& fileName,
                                const std::shared_ptr<const an::consumer::ImageAsset>& image) const
{
    if (!m_deps.recentFiles)
    {
        return;
    }

    // A file on another machine is never a recent file. It is judged as given and as normalized, since normalizing
    // turns a Windows network path's backslashes into slashes and, elsewhere, folds its leading pair into one.
    const QString requestedFileName = QString::fromUtf8(fileName);
    if (DashboardUtils::isNetworkPath(requestedFileName))
    {
        return;
    }
    const QString normalizedFileName = DashboardUtils::normalizeRecentFilePath(requestedFileName);
    if (normalizedFileName.isEmpty() || DashboardUtils::isNetworkPath(normalizedFileName))
    {
        return;
    }

    const std::string normalizedFileNameStd = normalizedFileName.toUtf8().toStdString();

    if (!m_deps.recentFiles->empty() && m_deps.recentFiles->front() == normalizedFileNameStd)
    {
        updatePersistedRecentFileThumbnail(normalizedFileNameStd, image);
        if (m_deps.reloadRecentFilesThumbnails)
        {
            m_deps.reloadRecentFilesThumbnails();
        }
        return;
    }

    auto it = std::find(m_deps.recentFiles->begin(), m_deps.recentFiles->end(), normalizedFileNameStd);
    if (it != m_deps.recentFiles->end())
    {
        m_deps.recentFiles->erase(it);
    }

    m_deps.recentFiles->push_front(normalizedFileNameStd);

    const size_t recentFilesLimit = getRecentFilesLimit();
    while (m_deps.recentFiles->size() > recentFilesLimit)
    {
        removeRecentFileThumbnail(m_deps.recentFiles->back());
        m_deps.recentFiles->pop_back();
    }

    persistRecentFiles();
    updateRecentFilesMenuEntries();
    persistRecentFileThumbnail(normalizedFileNameStd, image);

    if (m_deps.reloadRecentFilesThumbnails)
    {
        m_deps.reloadRecentFilesThumbnails();
    }
}

void FileManager::removeRecentFile(const std::string& fileName) const
{
    if (!m_deps.recentFiles)
    {
        return;
    }

    const QString normalizedFileName = DashboardUtils::normalizeRecentFilePath(QString::fromUtf8(fileName));
    if (normalizedFileName.isEmpty())
    {
        return;
    }

    const std::string normalizedFileNameStd = normalizedFileName.toUtf8().toStdString();
    auto it = std::find(m_deps.recentFiles->begin(), m_deps.recentFiles->end(), normalizedFileNameStd);
    if (it == m_deps.recentFiles->end())
    {
        return;
    }

    m_deps.recentFiles->erase(it);

    persistRecentFiles();
    updateRecentFilesMenuEntries();
    removeRecentFileThumbnail(normalizedFileNameStd);

    if (m_deps.reloadRecentFilesThumbnails)
    {
        m_deps.reloadRecentFilesThumbnails();
    }
}

void FileManager::persistRecentFileThumbnail(const std::string& fileName,
                                             const std::shared_ptr<const an::consumer::ImageAsset>& image) const
{
    if (!m_deps.thumbnailPersistence || !image)
    {
        return;
    }

    const auto path = resolveThumbnailsDirPath(m_deps);
    auto* provider = process_utils::currentEngineSessionProvider();
    if (path.trimmed().isEmpty() || provider == nullptr)
    {
        return;
    }
    const QString finalPath = QDir(path).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".jpg");
    const std::uint64_t request = ++m_thumbnails->next;
    m_thumbnails->latest[fileName] = request;

    // Rendered and written on a worker, recorded on the GUI thread; a thumbnail superseded meanwhile is removed
    // instead.
    const an::consumer::EngineSession* session = &provider->engineSession();
    QPointer<QObject> guard(resolveSignalContext(m_deps));
    std::weak_ptr<ThumbnailRequests> requests = m_thumbnails;
    QThreadPool::globalInstance()->start([this, guard, requests, session, image, fileName, finalPath, request]() {
        const an::Status written = writeRecentFileThumbnail(*session, image, finalPath, {});
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(
            application,
            [this, guard, requests, fileName, finalPath, request, written]() {
                const std::shared_ptr<ThumbnailRequests> current = requests.lock();
                if (!guard || current == nullptr)
                {
                    QFile::remove(finalPath);
                    return;
                }
                if (written.failed())
                {
                    LOG_WARNING("Could not write the thumbnail of '{}': {}", fileName, written.message());
                    return;
                }
                const auto latest = current->latest.find(fileName);
                if (latest == current->latest.end() || latest->second != request || !m_deps.thumbnailPersistence)
                {
                    QFile::remove(finalPath);
                    return;
                }
                current->latest.erase(latest);
                const QString name = QString::fromUtf8(fileName);
                const QString previous = m_deps.thumbnailPersistence->select(name);
                m_deps.thumbnailPersistence->upsert(name, finalPath);
                if (!previous.isEmpty() && previous != finalPath)
                {
                    QFile::remove(previous);
                }
                if (m_deps.reloadRecentFilesThumbnails)
                {
                    m_deps.reloadRecentFilesThumbnails();
                }
                if (m_deps.thumbnailRecorded)
                {
                    m_deps.thumbnailRecorded(fileName, finalPath);
                }
            },
            Qt::QueuedConnection);
    });
}

void FileManager::removeRecentFileThumbnail(const std::string& fileName) const
{
    if (!m_deps.thumbnailPersistence)
    {
        return;
    }

    // A thumbnail still on its way for the file is superseded, and removed when it lands.
    m_thumbnails->latest.erase(fileName);

    QString qFileName = QString::fromUtf8(fileName);
    const QString persistedFile = m_deps.thumbnailPersistence->select(qFileName);

    if (!persistedFile.isEmpty() && QFileInfo::exists(persistedFile))
    {
        QFile::remove(persistedFile);
    }

    m_deps.thumbnailPersistence->remove(qFileName);
}

void FileManager::clearRecentFilesThumbnails() const
{
    if (!m_deps.thumbnailPersistence)
    {
        return;
    }

    m_thumbnails->latest.clear();

    const auto path = resolveThumbnailsDirPath(m_deps);
    if (path.trimmed().isEmpty())
    {
        return;
    }

    std::error_code ec;
    std::filesystem::directory_iterator it(std::filesystem::path(path.toStdWString()), ec);
    for (std::filesystem::directory_iterator end; !ec && it != end; it.increment(ec))
    {
        std::error_code removeEc;
        std::filesystem::remove(it->path(), removeEc);
    }

    m_deps.thumbnailPersistence->clearAll();
}

void FileManager::updatePersistedRecentFileThumbnail(const std::string& fileName,
                                                     const std::shared_ptr<const an::consumer::ImageAsset>& image) const
{
    // The new thumbnail replaces the one recorded when it lands, so the dashboard never shows none in between.
    persistRecentFileThumbnail(fileName, image);
}

void FileManager::clearRecentFilesList() const
{
    if (!m_deps.recentFiles)
    {
        return;
    }

    ScopedTopLevelFlagRestorer _;

    QMessageBox msgBox;
    msgBox.setText("Are you sure you want to delete the recent opened files?");
    msgBox.setInformativeText("The history of opened images will be lost");
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::Yes);

    if (msgBox.exec() != QMessageBox::Yes)
    {
        return;
    }

    m_deps.recentFiles->clear();
    persistRecentFiles();
    updateRecentFilesMenuEntries();
    clearRecentFilesThumbnails();

    if (m_deps.notifyRecentFilesListDeleted)
    {
        m_deps.notifyRecentFilesListDeleted();
    }
}

} // namespace an::ui
