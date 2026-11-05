// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Profiled Starmask Recombination window: the selectors, the profile and the mask's shape on the GUI thread; the
/// profile read on the thread pool under an operation of its own; and, on the run's worker, the mask painted from the
/// profile's stars by the engine, softened by its exact Gaussian when asked, and the recombination composed through it.

#include "stdafx.hpp"
#include "ProfiledStarmaskRecombinationProcess.h"
#include "controls/IAppContext.h"
#include "controls/ImageSelectorTooltipBuilder.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessButtonIcons.h"
#include "controls/StarsFilterControlV2.h"
#include "processes/CombinationProcessSupport.h"
#include "processes/ProcessCommonUtils.h"
#include "processes/StarProfileStars.h"
#include "session/IEngineSessionProvider.h"
#include "widgets/ImageViewWidget.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Algorithms/Spatial/Gaussian.h>
#include <AfterNightEngine/Algorithms/Stars/StarMaskRecombination.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ImageTransfer.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightPlatform/Logging.h>

#include <QCoreApplication>
#include <QPointer>
#include <QSignalBlocker>
#include <QThreadPool>

#include <array>
#include <atomic>
#include <utility>

namespace
{

constexpr const char* kBinaryAreas = "Binary Areas";

struct ShapeEntry
{
    const char* label;
    an::StarMaskShape shape;
};

constexpr std::array<ShapeEntry, 6u> kShapes{{
    {"Bell", an::StarMaskShape::Bell},
    {"Truncated Bell", an::StarMaskShape::TruncatedBell},
    {"Linear", an::StarMaskShape::Linear},
    {"Truncated Linear", an::StarMaskShape::TruncatedLinear},
    {"Cubic", an::StarMaskShape::Cubic},
    {"Quadratic", an::StarMaskShape::Quadratic},
}};

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// Runs a function on the GUI thread through the event loop, never waited on.
void postToGui(std::function<void()> function)
{
    if (QCoreApplication* application = QCoreApplication::instance())
    {
        QMetaObject::invokeMethod(application, std::move(function), Qt::QueuedConnection);
    }
}

/// The mask painted from the profile's stars: a Float32 grey image of the starless image's size, every star a filled
/// ellipse of its fit scaled and softened, or a shaped area; held as coverages in the unit interval.
[[nodiscard]] an::Result<an::consumer::ImageAsset> paintProfileMask(const an::ui::ExecutionScope& scope,
                                                                    const an::ImageDescriptor& starless,
                                                                    const ProfiledStarmaskRecombinationParams& params)
{
    const an::Result<an::ImageDescriptor> described =
        process_utils::compositionDescriptor(starless, 1u, an::SampleType::Float32, scope.engine().limits());
    if (!described.hasValue())
    {
        return described.status();
    }
    const an::ExecutionContext& execution = scope.execution();
    const std::span<const an::DetectedStar> stars = *params.profileStars;
    an::Result<an::consumer::ImageAsset> painted = process_utils::composeImage(
        scope, *described, an::ImageMetadata{}, [&](void* samples, const an::ImageLayout& layout) -> an::Status {
            const an::Result<an::StarMaskResult> ran =
                params.binaryAreas
                    ? an::paintEllipticalStarMask(execution, stars,
                                                  an::StarMaskEllipseRequest{.sizeScale = params.scaleFactor},
                                                  an::SampleType::Float32, samples, layout)
                    : an::paintShapedStarMask(execution, stars, params.shape, an::SampleType::Float32, samples, layout);
            return ran.hasValue() ? an::Status::success() : ran.status();
        });
    if (!painted.hasValue() || !params.binaryAreas || params.softenEdges <= 0.0)
    {
        return painted;
    }
    // The filled ellipses softened by the exact Gaussian of the stated deviation.
    an::GaussianRequest request;
    request.sigmaX = params.softenEdges;
    request.sigmaY = params.softenEdges;
    const an::Result<an::GaussianPlan> plan = an::planGaussianBlur(request);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::Result<an::consumer::AssetReadLease> lease = painted->read(execution.stopToken());
    if (!lease.hasValue())
    {
        return lease.status();
    }
    an::Result<std::unique_ptr<process_utils::BoundImage>> bound =
        process_utils::BoundImage::bind(an::ui::ProcessSource::of(*lease));
    if (!bound.hasValue())
    {
        return bound.status();
    }
    return process_utils::composeImage(
        scope, *described, an::ImageMetadata{}, [&](void* samples, const an::ImageLayout& layout) -> an::Status {
            const an::Result<an::SpatialFilterResult> ran = an::blurImageGaussian(
                execution, an::SampleType::Float32, (*bound)->samples(), (*bound)->layout(), samples, layout, *plan);
            return ran.hasValue() ? an::Status::success() : ran.status();
        });
}

} // namespace

namespace an::ui::processes::profiled_recombination_detail
{

std::optional<an::StarMaskShape> starMaskShapeFromLabel(const QString& label)
{
    for (const ShapeEntry& entry : kShapes)
    {
        if (label == QLatin1String(entry.label))
        {
            return entry.shape;
        }
    }
    return std::nullopt;
}

} // namespace an::ui::processes::profiled_recombination_detail

/// A profile read on its way: the generation a landing is checked against and the stop the window's close requests.
struct ProfiledStarmaskRecombinationProcess::ProfileLoad
{
    std::atomic<std::uint64_t> generation{0u};
    bool loading = false;
};

ProfiledStarmaskRecombinationProcess::ProfiledStarmaskRecombinationProcess(QWidget* parent)
    : an::ui::TypedProcessControl<ProfiledStarmaskRecombinationParams>(nullptr, getProcessNameStatic()),
      m_load(std::make_shared<ProfileLoad>())
{
    Q_UNUSED(parent);

    m_ui = an::ui::processes::buildProfiledStarmaskRecombinationProcessUi(this);
    setProcessDescription(
        QStringLiteral("Recombine stars using profiled selections with mask shaping and edge controls."));

    m_ui.groupBox->setFlat(true);
    m_ui.groupBox_2->setFlat(true);
    m_ui.maskGenerationGroupBox->setFlat(true);

    m_SupportTargetViewControlSelection = false;
    m_SupportPerChannelProcessing = false;
    m_ShowWaitingSpinner = true;
    m_SupportMasking = false;
    m_ImagePreviewLayout = m_ui.verticalLayout_2;
    m_MainHorizontalLayout = m_ui.horizontalLayoutMain;

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.verticalLayout_2, m_ui.preview, nullptr, nullptr, nullptr,
                              {.supportPerChannel = false, .showWaitingSpinner = true});

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &ProfiledStarmaskRecombinationProcess::on_dialog_box_button_clicked);
    for (QComboBox* selector : {m_ui.starmaskImage, m_ui.starlessImage})
    {
        QObject::connect(selector, &QComboBox::currentTextChanged, this, [this](const QString&) {
            validateSelectedImages();
            settingsUpdated();
        });
    }
    QObject::connect(m_ui.starsProfile, &QLineEdit::textChanged, this,
                     &ProfiledStarmaskRecombinationProcess::on_starsProfile_Changed);
    QObject::connect(m_ui.openStarsProfileButton, &QPushButton::released, this,
                     &ProfiledStarmaskRecombinationProcess::on_openStarsProfileButtonPushed);
    QObject::connect(m_ui.reloadStarsProfileButton, &QPushButton::released, this,
                     [this]() { on_starsProfile_Changed(m_ui.starsProfile->text()); });
    QObject::connect(m_ui.clearProfileButton, &QPushButton::released, this,
                     [this]() { m_ui.starsProfile->setText(QString()); });
    QObject::connect(m_ui.starsFilterButton, &QPushButton::released, this,
                     &ProfiledStarmaskRecombinationProcess::on_starsFilterButtonPushed);
    QObject::connect(m_ui.showStarProfiles, &QCheckBox::checkStateChanged, this,
                     [this](Qt::CheckState) { showProfileStars(); });
    QObject::connect(m_ui.invertSelection, &QCheckBox::checkStateChanged, this,
                     [this](Qt::CheckState) { settingsUpdated(); });
    QObject::connect(m_ui.shapeSelector, &QComboBox::currentTextChanged, this,
                     &ProfiledStarmaskRecombinationProcess::on_shapeSelector_currentTextChanged);

    INIT_D_SPINBOX_CTRL(scaleFactorControl, m_ui.scaleFactorValue, m_ui.scaleFactorSlider, [this](double value) {
        if (m_starSelectionController && m_ui.showStarProfiles->isChecked())
        {
            m_starSelectionController->setStarSelectionScaleFactor(static_cast<float>(value));
        }
        settingsUpdated();
    });
    INIT_D_SPINBOX_CTRL(tightenEdgesControl, m_ui.tightenEdgesValue, m_ui.tightenEdgesSlider,
                        [this](double) { settingsUpdated(); });
    INIT_D_SPINBOX_CTRL(softenEdgesControl, m_ui.softenEdgesValue, m_ui.softenEdgesSlider,
                        [this](double) { settingsUpdated(); });
    INIT_D_SPINBOX_CTRL(percentPixelIncreaseControl, m_ui.percentPixelIncreaseValue, m_ui.percentPixelIncreaseSlider,
                        [this](double) { settingsUpdated(); });
    INIT_SPINBOX_CTRL(pixelIncreaseControl, m_ui.pixelIncreaseValue, m_ui.pixelIncreaseSlider,
                      [this](int) { settingsUpdated(); });

    setWindowTitle(QStringLiteral("Profiled Starmask Recombination"));

    setExecutionAutostretchSupported(true);
    refreshTheme();
    initialize();
}

ProfiledStarmaskRecombinationProcess::~ProfiledStarmaskRecombinationProcess()
{
    m_load->generation.fetch_add(1u);
    detachCurrentFilter();
}

std::string ProfiledStarmaskRecombinationProcess::getProcessNameStatic()
{
    return "Profiled Starmask Recombination";
}

void ProfiledStarmaskRecombinationProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
        m_ui.clearProfileButton->setIcon(iconProvider->getIcon(QStringLiteral("close")));
        m_ui.reloadStarsProfileButton->setIcon(iconProvider->getIcon(QStringLiteral("refresh")));
    }
}

void ProfiledStarmaskRecombinationProcess::addViewToSelectors(const ImageViewControl* ctrl)
{
    const QString imageName = QString::fromUtf8(ctrl->getUniqueName());
    for (QComboBox* selector : {m_ui.starmaskImage, m_ui.starlessImage})
    {
        if (selector->findText(imageName) == -1)
        {
            selector->addItem(imageName);
            selector->setItemData(selector->count() - 1, an::ui::buildImageSelectorTooltip(ctrl), Qt::ToolTipRole);
        }
    }
}

void ProfiledStarmaskRecombinationProcess::onViewControlCreated(const ImageViewControl* ctrl)
{
    addViewToSelectors(ctrl);
}

void ProfiledStarmaskRecombinationProcess::onViewControlDestroyed(const ImageViewControl* ctrl)
{
    const QString imageName = QString::fromUtf8(ctrl->getUniqueName());
    for (QComboBox* selector : {m_ui.starmaskImage, m_ui.starlessImage})
    {
        const int idx = selector->findText(imageName);
        if (idx != -1)
        {
            if (selector->currentText() == imageName)
            {
                selector->setCurrentText(NO_IMAGE_STR);
            }
            selector->removeItem(idx);
        }
    }
    validateSelectedImages();
}

void ProfiledStarmaskRecombinationProcess::onViewControlRenamed(const std::string& newName, const std::string& oldName,
                                                                const ImageViewControl* ctrl)
{
    const QString newImageName = QString::fromUtf8(newName);
    const QString oldImageName = QString::fromUtf8(oldName);
    for (QComboBox* selector : {m_ui.starmaskImage, m_ui.starlessImage})
    {
        const int idx = selector->findText(oldImageName);
        if (idx != -1)
        {
            selector->setItemText(idx, newImageName);
            selector->setItemData(idx, an::ui::buildImageSelectorTooltip(ctrl), Qt::ToolTipRole);
        }
    }
    validateSelectedImages();
}

void ProfiledStarmaskRecombinationProcess::onViewControlUpdated(const ImageViewControl* ctrl)
{
    if (ctrl == m_image_view_control || m_ui.starmaskImage->currentText() == QString::fromUtf8(ctrl->getUniqueName()))
    {
        settingsUpdated();
    }
}

an::ui::StarListPtr ProfiledStarmaskRecombinationProcess::getCurrentStarList()
{
    return m_profileStars;
}

void ProfiledStarmaskRecombinationProcess::setCurrentStarList(const an::ui::StarListPtr& starList, bool fullUpdate)
{
    m_profileStars = starList ? starList : std::make_shared<const an::ui::StarList>();
    profileChanged();
    if (fullUpdate)
    {
        settingsUpdated();
    }
}

ImageViewControl* ProfiledStarmaskRecombinationProcess::getViewControl()
{
    return m_rt_preview_view_control ? m_rt_preview_view_control : m_image_view_control;
}

void ProfiledStarmaskRecombinationProcess::updateStarSelection(const an::ui::StarListPtr& starSelection)
{
    if (m_starSelectionController)
    {
        m_starSelectionController->updateStarSelection(starSelection);
    }
}

bool ProfiledStarmaskRecombinationProcess::isForProfiledStarmaskRecombination()
{
    return true;
}

bool ProfiledStarmaskRecombinationProcess::isPreviewEnabled()
{
    return m_ui.preview->isChecked();
}

void ProfiledStarmaskRecombinationProcess::onStarSelected(size_t /*idx*/) {}

double ProfiledStarmaskRecombinationProcess::getCurrentScaleFactor() const
{
    return scaleFactorControl->getValue();
}

void ProfiledStarmaskRecombinationProcess::onApplicationClose()
{
    close();
}

bool ProfiledStarmaskRecombinationProcess::isProfileLoading() const noexcept
{
    return m_load->loading;
}

std::weak_ptr<const void> ProfiledStarmaskRecombinationProcess::profileLoadHandle() const noexcept
{
    return m_load;
}

std::size_t ProfiledStarmaskRecombinationProcess::profileStarCount() const
{
    return m_profileStars ? m_profileStars->size() : 0u;
}

void ProfiledStarmaskRecombinationProcess::initialize()
{
    for (QComboBox* selector : {m_ui.starmaskImage, m_ui.starlessImage})
    {
        const QSignalBlocker blocker(selector);
        selector->addItem(NO_IMAGE_STR);
    }
    for (auto* viewControl : process_utils::listViewControls())
    {
        if (viewControl != nullptr && viewControl->hasImage())
        {
            addViewToSelectors(viewControl);
        }
    }
    for (QComboBox* selector : {m_ui.starmaskImage, m_ui.starlessImage})
    {
        selector->setCurrentText(NO_IMAGE_STR);
    }

    softenEdgesControl->setValue(2.0);
    percentPixelIncreaseControl->setValue(0.5);
    pixelIncreaseControl->setValue(0);
    scaleFactorControl->setValue(1.0);
    tightenEdgesControl->setValue(0.0);

    m_ui.shapeSelector->addItem(QLatin1String(kBinaryAreas));
    for (const ShapeEntry& entry : kShapes)
    {
        m_ui.shapeSelector->addItem(QLatin1String(entry.label));
    }
    m_ui.shapeSelector->setCurrentText(QLatin1String(kBinaryAreas));
    on_shapeSelector_currentTextChanged(m_ui.shapeSelector->currentText());

    profileChanged();
    validateSelectedImages();
    adjustSize();
    resize(m_DefaultSizeNoPreview);
}

void ProfiledStarmaskRecombinationProcess::validateSelectedImages()
{
    const QString starlessStr = m_ui.starlessImage->currentText();
    const QString starmaskStr = m_ui.starmaskImage->currentText();
    ImageViewControl* starless = starlessStr == NO_IMAGE_STR || starlessStr.isEmpty()
                                     ? nullptr
                                     : process_utils::findViewControlByName(starlessStr);
    if (starless != nullptr && starless != m_image_view_control)
    {
        changeTargetViewControl(starless);
    }
    ImageViewControl* stars = starmaskStr == NO_IMAGE_STR || starmaskStr.isEmpty()
                                  ? nullptr
                                  : process_utils::findViewControlByName(starmaskStr);
    const bool ready = starless != nullptr && starless == m_image_view_control && stars != nullptr && stars->hasImage();
    m_ui.preview->setEnabled(ready);
    if (ready)
    {
        if (isPreviewEnabled())
        {
            createPreviewControl();
        }
    }
    else if (isPreviewEnabled())
    {
        destroyPreviewControl();
    }
    m_ui.starsFilterButton->setEnabled(ready && profileStarCount() > 0u);
    if (!ready && m_CurrentFilter)
    {
        m_CurrentFilter->forceClose();
        m_CurrentFilter = nullptr;
    }
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(ready);
}

void ProfiledStarmaskRecombinationProcess::profileChanged()
{
    const bool loaded = profileStarCount() > 0u;
    m_ui.filteredStarsTxt->setText(QString::number(profileStarCount()));
    m_ui.starsFilterButton->setEnabled(loaded && m_image_view_control != nullptr &&
                                       m_ui.starmaskImage->currentText() != NO_IMAGE_STR);
    m_ui.filteredStarsTxt->setEnabled(loaded);
    scaleFactorControl->setEnabled(loaded);
    tightenEdgesControl->setEnabled(loaded);
    m_ui.showStarProfiles->setEnabled(loaded);
    m_ui.invertSelection->setEnabled(loaded);
    m_ui.reloadStarsProfileButton->setEnabled(!m_ui.starsProfile->text().isEmpty());
    m_ui.clearProfileButton->setEnabled(!m_ui.starsProfile->text().isEmpty());
    m_ui.maskGenerationGroupBox->setEnabled(loaded);
    showProfileStars();
}

void ProfiledStarmaskRecombinationProcess::showProfileStars()
{
    if (!m_starSelectionController)
    {
        return;
    }
    const bool show = m_ui.showStarProfiles->isChecked() && profileStarCount() > 0u;
    m_starSelectionController->enableStarSelection(show);
    if (show)
    {
        m_starSelectionController->setStarSelectionScaleFactor(static_cast<float>(scaleFactorControl->getValue()));
        m_starSelectionController->updateStarSelection(m_profileStars);
    }
}

void ProfiledStarmaskRecombinationProcess::on_dialog_box_button_clicked(QAbstractButton* button)
{
    QDialogButtonBox* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
    {
        return;
    }
    const QDialogButtonBox::StandardButton sb = buttonBox->standardButton(button);
    if (sb == QDialogButtonBox::Ok)
    {
        m_IgnoreLastImageUpdatedEvent = true;
        applyProcess();
    }
    else if (sb == QDialogButtonBox::Reset)
    {
        scaleFactorControl->setValue(1.0);
        tightenEdgesControl->setValue(0.0);
        m_ui.invertSelection->setChecked(false);
        softenEdgesControl->setValue(2.0);
        percentPixelIncreaseControl->setValue(0.5);
        pixelIncreaseControl->setValue(0);
        m_ui.shapeSelector->setCurrentText(QLatin1String(kBinaryAreas));
    }
    else if (sb == QDialogButtonBox::Close)
    {
        this->close();
    }
}

void ProfiledStarmaskRecombinationProcess::on_starsProfile_Changed(const QString& text)
{
    if (m_CurrentFilter)
    {
        m_CurrentFilter->forceClose();
        m_CurrentFilter = nullptr;
    }
    if (text.isEmpty())
    {
        m_load->generation.fetch_add(1u);
        m_load->loading = false;
        m_profileStars = std::make_shared<const an::ui::StarList>();
        profileChanged();
        settingsUpdated();
        return;
    }
    loadProfile(text);
}

void ProfiledStarmaskRecombinationProcess::loadProfile(const QString& path)
{
    auto* context = an::ui::appContext();
    an::ui::IEngineSessionProvider* provider = context ? context->getEngineSessionProvider() : nullptr;
    if (provider == nullptr)
    {
        return;
    }
    // The file is read on the thread pool under an operation of its own; the latest request wins.
    const std::uint64_t generation = m_load->generation.fetch_add(1u) + 1u;
    m_load->loading = true;
    const an::consumer::EngineSession* session = &provider->engineSession();
    QPointer<ProfiledStarmaskRecombinationProcess> guard(this);
    // The worker holds what it shares with the window: the window may go while the worker has yet to start.
    const std::shared_ptr<ProfileLoad> load = m_load;
    const std::stop_token stop = windowStopToken();
    const std::string file = path.toUtf8().toStdString();
    QThreadPool::globalInstance()->start([session, guard, load, generation, stop, file]() {
        an::Result<an::ui::StarList> stars = refusal(an::ErrorCode::Cancelled, "the read was stopped");
        an::Result<an::ui::ExecutionScope> admitted = an::ui::ExecutionScope::admit(
            *session, an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Preview, .stopToken = stop},
            an::ui::ExecutionObserver{});
        if (!admitted.hasValue())
        {
            stars = admitted.status();
        }
        else if (load->generation.load() == generation)
        {
            stars = process_utils::readProfileStars(admitted->execution(), file);
            admitted->finish(stars.hasValue() ? an::Status::success() : stars.status());
        }
        auto landing = std::make_shared<an::Result<an::ui::StarList>>(std::move(stars));
        postToGui([guard, load, generation, landing]() {
            if (!guard || load->generation.load() != generation)
            {
                return;
            }
            load->loading = false;
            if (!landing->hasValue())
            {
                guard->m_profileStars = std::make_shared<const an::ui::StarList>();
                guard->profileChanged();
                guard->notifyErrorMessage("Load Star Profile",
                                          QStringLiteral("Could NOT load the star profile file. Error message: ") +
                                              QString::fromUtf8(landing->status().message()));
                guard->settingsUpdated();
                return;
            }
            guard->m_profileStars = std::make_shared<const an::ui::StarList>(std::move(**landing));
            guard->profileChanged();
            guard->settingsUpdated();
        });
    });
}

void ProfiledStarmaskRecombinationProcess::on_openStarsProfileButtonPushed()
{
    const QStringList selectedFiles = process_utils::selectFiles(this, QStringLiteral("Load star profile..."),
                                                                 QStringLiteral("__PROF_STAR_MASK_RECOMB_FOLDER__"),
                                                                 {QStringLiteral("Files (*.anspv1;*.anspv2)")});
    if (!selectedFiles.empty())
    {
        m_ui.starsProfile->setText(selectedFiles.back());
    }
}

void ProfiledStarmaskRecombinationProcess::on_starsFilterButtonPushed()
{
    auto* filter = new StarsFilterControlV2(this);
    filter->show();
}

void ProfiledStarmaskRecombinationProcess::on_shapeSelector_currentTextChanged(const QString& text)
{
    const bool binary = text == QLatin1String(kBinaryAreas);
    softenEdgesControl->setEnabled(binary);
    percentPixelIncreaseControl->setEnabled(!binary);
    pixelIncreaseControl->setEnabled(!binary);
    settingsUpdated();
}

bool ProfiledStarmaskRecombinationProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void ProfiledStarmaskRecombinationProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void ProfiledStarmaskRecombinationProcess::settingsUpdatedImpl()
{
    m_ui.filteredStarsTxt->setText(QString::number(profileStarCount()));
}

void ProfiledStarmaskRecombinationProcess::getBatchExecutionParameters(bool& createNewImage, bool& closeAtEnd,
                                                                       bool& executeInBackground,
                                                                       std::string& newImageName)
{
    createNewImage = true;
    closeAtEnd = true;
    executeInBackground = true;
    newImageName = fmt::format("{} - {} - MERGED", m_image_view_control ? m_image_view_control->getUniqueName() : "",
                               m_ui.starmaskImage->currentText().toStdString());
}

void ProfiledStarmaskRecombinationProcess::previewControlStarted()
{
    if (!m_rt_preview_view_control)
    {
        return;
    }
    m_starSelectionController =
        std::make_unique<ImageStarSelectionController>(m_rt_preview_view_control->getScrollArea(), this);
    m_starSelectionController->setListenerEnabled(true);
    m_starSelectionController->attachToViewWidget(m_rt_preview_view_control->getViewWidget());
    m_starSelectionController->setInputCaptureEnabled(false);
    if (auto* widget = m_starSelectionController->getViewWidget())
    {
        widget->enableQuickZoom(true);
    }
    showProfileStars();
    if (m_CurrentFilter && m_CurrentFilter->isPreviewEnabled() && isPreviewEnabled())
    {
        notifyFilterUpdated();
    }
}

void ProfiledStarmaskRecombinationProcess::previewControlClosed()
{
    if (m_starSelectionController)
    {
        m_starSelectionController->destroy();
        m_starSelectionController.reset();
    }
}

an::Status ProfiledStarmaskRecombinationProcess::executeTypedProcessImpl(
    const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination, const an::ui::ProcessRun& run,
    const ProfiledStarmaskRecombinationParams& params, an::ui::ExecutionScope& scope)
{
    if (!params.stars)
    {
        return run.isForPreview() ? an::Status::success()
                                  : refusal(an::ErrorCode::InvalidArgument, "select the star image to put back");
    }
    const std::optional<an::ImageDescriptor> starlessDescribed = source.descriptor();
    if (!starlessDescribed.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the starless image holds nothing");
    }
    an::Result<process_utils::LeasedImage> stars =
        process_utils::LeasedImage::read(scope, *params.stars, run, params.previewArea, *starlessDescribed);
    if (!stars.hasValue())
    {
        return stars.status();
    }
    const an::ui::ProcessSource starsSource = stars->source();
    const std::optional<an::ImageDescriptor> starsDescribed = starsSource.descriptor();
    if (!starsDescribed.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the star image holds nothing");
    }
    if (!process_utils::sameSize(*starlessDescribed, *starsDescribed) ||
        starlessDescribed->extent().channelCount() != starsDescribed->extent().channelCount())
    {
        return refusal(an::ErrorCode::IncompatibleArguments,
                       "the star image does not have the starless image's size and channels");
    }

    // The mask, painted from the profile's stars over the whole starless image and cropped to a preview's area as the
    // framework cropped the target; none when the profile holds no star.
    std::optional<an::consumer::ImageAsset> mask;
    std::optional<process_utils::LeasedImage> maskLeased;
    std::optional<an::ui::ProcessSource> maskSource;
    if (params.profileStars && !params.profileStars->empty())
    {
        const std::optional<an::ImageDescriptor> wholeDescribed = params.stars->descriptor();
        if (!wholeDescribed.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the star image holds nothing");
        }
        an::Result<an::consumer::ImageAsset> painted = paintProfileMask(scope, *wholeDescribed, params);
        if (!painted.hasValue())
        {
            return painted.status();
        }
        mask.emplace(std::move(*painted));
        an::Result<process_utils::LeasedImage> leased =
            process_utils::LeasedImage::read(scope, *mask, run, params.previewArea, *starlessDescribed);
        if (!leased.hasValue())
        {
            return leased.status();
        }
        maskLeased.emplace(std::move(*leased));
        maskSource.emplace(maskLeased->source());
    }

    const an::Result<an::StarMaskRecombinationPlan> plan = an::planStarMaskRecombination(
        an::StarMaskRecombinationRequest{.edgeTightening = params.tightenEdges, .invertMask = params.invertSelection});
    if (!plan.hasValue())
    {
        return plan.status();
    }
    // The result is in the higher of the two images' representations; an integer image is recombined in fractions of
    // full scale through a working copy, and the result narrowed back.
    const an::SampleType resultType = an::starMaskRecombinationSampleType(*plan, *starlessDescribed, *starsDescribed);
    const an::SampleType working = process_utils::compositionWorkingType(resultType);
    // Two physical images keep their sum above one; any other pair is added as fractions of full scale.
    const process_utils::PhysicalReading reading =
        process_utils::recombinationReading(*starlessDescribed, *starsDescribed);
    an::Result<std::unique_ptr<process_utils::CompositionSource>> starless =
        process_utils::bindForComposition(scope, source, working, reading);
    if (!starless.hasValue())
    {
        return starless.status();
    }
    an::Result<std::unique_ptr<process_utils::CompositionSource>> starsBound =
        process_utils::bindForComposition(scope, starsSource, working, reading);
    if (!starsBound.hasValue())
    {
        return starsBound.status();
    }
    std::unique_ptr<process_utils::CompositionSource> maskBound;
    if (maskSource.has_value())
    {
        an::Result<std::unique_ptr<process_utils::CompositionSource>> bound =
            process_utils::bindForComposition(scope, *maskSource, working, process_utils::PhysicalReading::AsFractions);
        if (!bound.hasValue())
        {
            return bound.status();
        }
        maskBound = std::move(*bound);
    }
    const an::Result<an::ImageDescriptor> described = process_utils::compositionDescriptor(
        *starlessDescribed, starlessDescribed->extent().channelCount(), resultType, scope.engine().limits());
    if (!described.hasValue())
    {
        return described.status();
    }
    an::Result<an::ImageMetadata> metadata = process_utils::recombinedResultMetadata(
        source.metadata(), *described,
        process_utils::CombinationRecord{.stateOperation = "profiled-starmask-recombination",
                                         .engineOperation = "an::recombineStarMask",
                                         .detail = describeTypedProcessStep(params).description,
                                         .linearity = process_utils::CombinationLinearity::Nonlinear});
    if (!metadata.hasValue())
    {
        return metadata.status();
    }
    const an::StarMaskRecombinationSource starlessBound{.descriptor = &(*starless)->bound->descriptor(),
                                                        .samples = (*starless)->bound->samples(),
                                                        .layout = &(*starless)->bound->layout(),
                                                        .validity = (*starless)->bound->validity()};
    const an::StarMaskRecombinationSource starsSourceBound{.descriptor = &(*starsBound)->bound->descriptor(),
                                                           .samples = (*starsBound)->bound->samples(),
                                                           .layout = &(*starsBound)->bound->layout(),
                                                           .validity = (*starsBound)->bound->validity()};
    std::optional<an::StarMaskRecombinationSource> maskSourceBound;
    if (maskBound)
    {
        maskSourceBound.emplace(an::StarMaskRecombinationSource{.descriptor = &maskBound->bound->descriptor(),
                                                                .samples = maskBound->bound->samples(),
                                                                .layout = &maskBound->bound->layout(),
                                                                .validity = maskBound->bound->validity()});
    }
    const an::ExecutionContext& execution = scope.execution();
    an::Result<an::consumer::ImageAsset> composed = process_utils::composeNarrowed(
        scope, *described, std::move(*metadata), [&](void* samples, const an::ImageLayout& layout) -> an::Status {
            const an::Result<an::StarMaskRecombinationResult> ran =
                an::recombineStarMask(execution, *plan, starlessBound, starsSourceBound,
                                      maskSourceBound.has_value() ? &*maskSourceBound : nullptr, samples, layout);
            return ran.hasValue() ? an::Status::success() : ran.status();
        });
    if (!composed.hasValue())
    {
        return composed.status();
    }
    return destination.replace(std::move(*composed));
}

an::ui::PreviewScaling ProfiledStarmaskRecombinationProcess::previewScaling() const
{
    // The mask is painted from the profile's stars over the whole image and cut with it, and the stars are put back
    // pixel by pixel: an interactive frame runs at full scale over the part of the target the preview view shows, the
    // stars and the mask cut to the same part.
    return an::ui::PreviewScaling::NativeRegion;
}

std::uint32_t
ProfiledStarmaskRecombinationProcess::typedPreviewRegionMargin(const ProfiledStarmaskRecombinationParams& params) const
{
    // Tightening the edges reads the three by three Sobel window around each pixel; nothing else reads a neighbour.
    return params.tightenEdges > 0.0 ? 1u : 0u;
}

an::ui::ProcessStep
ProfiledStarmaskRecombinationProcess::describeTypedProcessStep(const ProfiledStarmaskRecombinationParams& params) const
{
    const std::size_t stars = params.profileStars ? params.profileStars->size() : 0u;
    return an::ui::ProcessStep{
        .name = getProcessName(),
        .description = fmt::format(
            "ProfiledStarmaskRecombinationProcess: Image created from starless image '{}' and starmask image '{}' "
            "using operation add; {} profiled stars as {}{}{}{}",
            params.starlessName.toStdString(), params.starsName.toStdString(), stars,
            params.binaryAreas
                ? fmt::format("binary areas scaled {:.2f}, softened {:.2f}", params.scaleFactor, params.softenEdges)
                : fmt::format("{} areas, percent increase {:.2f}, pixel increase {:.0f}",
                              params.shapeName.toStdString(), params.shape.sizeScale, params.shape.sizeIncrease),
            params.tightenEdges > 0.0 ? fmt::format(", edges tightened {:.2f}", params.tightenEdges) : std::string(),
            params.invertSelection ? ", mask inverted" : "", stars == 0u ? ", unmasked" : "")};
}

// The stars: other views' images the body reads, leased before the run is admitted.
std::vector<std::shared_ptr<const an::consumer::ImageAsset>>
ProfiledStarmaskRecombinationProcess::typedImagesRead(const ProfiledStarmaskRecombinationParams& params) const
{
    return {params.stars};
}

ProfiledStarmaskRecombinationParams ProfiledStarmaskRecombinationProcess::getParams() const
{
    ProfiledStarmaskRecombinationParams params;
    params.starlessName = m_ui.starlessImage->currentText();
    params.starsName = m_ui.starmaskImage->currentText();
    if (params.starsName != NO_IMAGE_STR && !params.starsName.isEmpty())
    {
        if (auto* stars = process_utils::findViewControlByName(params.starsName); stars != nullptr && stars->hasImage())
        {
            params.stars = stars->document().asset();
        }
    }
    params.profileStars = m_profileStars;
    params.shapeName = m_ui.shapeSelector->currentText();
    const std::optional<an::StarMaskShape> shape =
        an::ui::processes::profiled_recombination_detail::starMaskShapeFromLabel(params.shapeName);
    params.binaryAreas = !shape.has_value();
    params.scaleFactor = scaleFactorControl->getValue();
    params.softenEdges = softenEdgesControl->getValue();
    params.shape.shape = shape.value_or(an::StarMaskShape::TruncatedBell);
    params.shape.minimumSize = 0.0;
    params.shape.maximumSize = 100.0;
    params.shape.sizeScale = (std::max)(percentPixelIncreaseControl->getValue(), 0.01);
    params.shape.sizeIncrease = static_cast<double>(pixelIncreaseControl->getValue());
    params.tightenEdges = tightenEdgesControl->getValue();
    params.invertSelection = m_ui.invertSelection->isChecked();
    params.previewArea = previewArea();
    return params;
}
