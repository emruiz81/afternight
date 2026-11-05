// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Image Compare window: the selectors, the compare modes and the splitter's interaction on the GUI thread; the
/// second image's presentation for the split on the thread pool under an operation of its own, landed only while it is
/// the latest asked for; the two histograms counted by their schedulers; and a run that copies the target.

#include "stdafx.hpp"
#include "ImageCompareProcess.h"
#include "controls/IAppContext.h"
#include "controls/ImageSelectorTooltipBuilder.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewScrollArea.h"
#include "controls/ProcessButtonIcons.h"
#include "processes/ProcessCommonUtils.h"
#include "session/ExecutionScope.h"
#include "session/IEngineSessionProvider.h"
#include "theme/AnDesignTokens.h"
#include "widgets/ChannelDisplayStrategy.h"
#include "widgets/HistogramCompanions.h"
#include "widgets/HistogramWidget.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/PresentationBridge.h"
#include "widgets/SplitComparisonDisplayModule.h"
#include "widgets/ViewPresentation.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QButtonGroup>
#include <QCoreApplication>
#include <QFont>
#include <QLabel>
#include <QMouseEvent>
#include <QPointer>
#include <QSignalBlocker>
#include <QThreadPool>
#include <QVBoxLayout>

#include <array>
#include <atomic>
#include <utility>

namespace
{

[[nodiscard]] QString buildHistogramLabel(const QString& fallbackLabel, const QString& imageName)
{
    if (imageName.isEmpty() || imageName == NO_IMAGE_STR)
    {
        return fallbackLabel;
    }
    return QStringLiteral("%1: %2").arg(fallbackLabel, imageName);
}

/// The histogram resolution the plot selector's item at an index names, in the order the framework fills it.
[[nodiscard]] an::ui::HistogramResolution resolutionAt(int index)
{
    using Resolution = an::ui::PreviewHistogramResolution;
    static constexpr std::array<Resolution, 11u> kResolutions{
        Resolution::Bits6,     Resolution::Bits7,      Resolution::Bits8,      Resolution::Bits9,
        Resolution::Bits10,    Resolution::Bits12,     Resolution::Bits14,     Resolution::Bits16,
        Resolution::Levels100, Resolution::Levels1000, Resolution::Levels10000};
    const std::size_t at = index < 0 ? 2u : static_cast<std::size_t>(index);
    return an::ui::histogramResolutionOf(at < kResolutions.size() ? kResolutions[at] : Resolution::Bits8);
}

/// Runs a function on the GUI thread through the event loop, never waited on.
void postToGui(std::function<void()> function)
{
    if (QCoreApplication* application = QCoreApplication::instance())
    {
        QMetaObject::invokeMethod(application, std::move(function), Qt::QueuedConnection);
    }
}

} // namespace

/// A presentation of the second image on its way: the generation a landing is checked against, whether one is pending
/// and how many have been shown.
struct ImageCompareProcess::OverlayWork
{
    std::atomic<std::uint64_t> generation{0u};
    std::atomic<bool> pending{false};
    std::uint64_t shown = 0u;
};

ImageCompareProcess::ImageCompareProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<std::monostate>(control, getProcessNameStatic()),
      m_overlay(std::make_shared<OverlayWork>())
{
    m_ui = an::ui::processes::buildImageCompareProcessUi(this);
    setProcessDescription(
        QStringLiteral("Compare two images with synchronized histogram preview and alternate-image inspection."));

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.previewLayout, nullptr, nullptr, m_ui.targetImageSelector,
                              nullptr, {.supportPerChannel = false, .showWaitingSpinner = false});

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &ImageCompareProcess::on_dialog_box_button_clicked);
    QObject::connect(m_ui.secondImageSelector, &QComboBox::currentTextChanged, this,
                     &ImageCompareProcess::on_secondImageSelector_currentTextChanged);
    QObject::connect(m_ui.compareModeGroup, &QButtonGroup::idClicked, this,
                     &ImageCompareProcess::on_compareModeChanged);

    updateWindowTitle();

    m_FixedLayout = false;
    setupHistogramPreview(m_ui.chartScrollArea, m_ui.plotResolution, m_ui.zoomIn, m_ui.zoomOut, m_ui.zoomReset,
                          m_ui.gridButton, m_ui.localChannelNormalization, m_ui.showClippingInfo, nullptr,
                          m_ui.showHistogramAreas);
    setupCompareHistogramPanel();
    setupCompareHistogramConnections();

    loadImage2List();
    m_ui.secondImageSelector->setCurrentText(NO_IMAGE_STR);

    m_splitModule = std::make_unique<an::ui::SplitComparisonDisplayModule>();
    m_splitModule->enableDisplayModule(false);

    setExecutionAutostretchSupported(true);
    setupPreviewControl(false);
    updateCompareHistogramPreview();

    refreshTheme();
}

ImageCompareProcess::~ImageCompareProcess()
{
    m_overlay->generation.fetch_add(1u);
    cleanupPreviewBindings();
}

std::string ImageCompareProcess::getProcessNameStatic()
{
    return "Image Compare";
}

bool ImageCompareProcess::requiresExclusiveAccessStatic()
{
    return false;
}

an::ui::processes::ImageCompareMode ImageCompareProcess::compareMode() const noexcept
{
    return m_compareMode;
}

void ImageCompareProcess::setCompareMode(an::ui::processes::ImageCompareMode mode)
{
    if (QAbstractButton* button =
            m_ui.compareModeGroup ? m_ui.compareModeGroup->button(static_cast<int>(mode)) : nullptr)
    {
        button->setChecked(true);
    }
    on_compareModeChanged(static_cast<int>(mode));
}

bool ImageCompareProcess::isOverlayPending() const noexcept
{
    return m_overlay->pending.load();
}

std::uint64_t ImageCompareProcess::overlaysShown() const noexcept
{
    return m_overlay->shown;
}

std::weak_ptr<const void> ImageCompareProcess::overlayWorkHandle() const noexcept
{
    return m_overlay;
}

bool ImageCompareProcess::lensShowsTheSecondImage() const noexcept
{
    return m_lensOnSecond;
}

an::ui::SplitComparisonDisplayModule* ImageCompareProcess::splitModule() const noexcept
{
    return m_splitModule.get();
}

HistogramWidget* ImageCompareProcess::primaryHistogram() const noexcept
{
    return m_primaryHistogramWidget;
}

HistogramWidget* ImageCompareProcess::secondaryHistogram() const noexcept
{
    return m_secondaryHistogramWidget;
}

bool ImageCompareProcess::histogramsIdle() const
{
    return (!m_primaryHistogramCounts || m_primaryHistogramCounts->isIdle()) &&
           (!m_secondaryHistogramCounts || m_secondaryHistogramCounts->isIdle());
}

void ImageCompareProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
    if (m_compareHistogramPanel)
    {
        if (auto* layout = qobject_cast<QVBoxLayout*>(m_compareHistogramPanel->layout()))
        {
            layout->setSpacing(an::ui::tokens().spacing.sm);
            layout->setContentsMargins(0, 0, 0, 0);
        }
    }
    refreshCompareHistogramLabelStyles();
}

void ImageCompareProcess::setupCompareHistogramPanel()
{
    if (!m_ui.chartScrollArea)
    {
        return;
    }
    // The framework's one histogram makes way for a panel of two, one per image, under the same buttons. It stays the
    // window's, hidden: the framework keeps it for the window's life, and a widget taken out with no parent is deleted
    // by nothing.
    if (QWidget* frameworkHistogram = m_ui.chartScrollArea->takeWidget())
    {
        frameworkHistogram->setParent(this);
        frameworkHistogram->hide();
    }
    m_UseHistogramPreviewWidget = false;

    m_compareHistogramPanel = new QWidget(m_ui.chartScrollArea);
    m_compareHistogramPanel->setObjectName(QStringLiteral("compareHistogramPanel"));

    auto* layout = new QVBoxLayout(m_compareHistogramPanel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(an::ui::tokens().spacing.sm);

    auto configureLabel = [](QLabel* label) {
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setMargin(an::ui::tokens().spacing.xs);
        label->setAutoFillBackground(true);
        label->setProperty("anFrameRadius", an::ui::tokens().radius.sm);
        label->setVisible(false);
    };
    auto configureHistogram = [this](HistogramWidget* widget) {
        widget->setMouseTracking(true);
        widget->setParentScrollArea(m_ui.chartScrollArea);
        widget->setInteractiveZoomEnabled(true);
        widget->setGridEnabled(m_ui.gridButton && m_ui.gridButton->isChecked());
        widget->setClippingInfoEnabled(m_ui.showClippingInfo && m_ui.showClippingInfo->isChecked());
        widget->setLocalChannelNormalization(m_ui.localChannelNormalization &&
                                             m_ui.localChannelNormalization->isChecked());
        widget->setAreaModeEnabled(m_ui.showHistogramAreas && m_ui.showHistogramAreas->isChecked());
        widget->setHistogramResolution(resolutionAt(m_ui.plotResolution ? m_ui.plotResolution->currentIndex() : -1));
    };

    m_primaryHistogramLabel = new QLabel(m_compareHistogramPanel);
    configureLabel(m_primaryHistogramLabel);
    layout->addWidget(m_primaryHistogramLabel);

    m_primaryHistogramWidget = new HistogramWidget(m_compareHistogramPanel);
    m_primaryHistogramWidget->setObjectName(QStringLiteral("primaryCompareHistogramWidget"));
    configureHistogram(m_primaryHistogramWidget);
    layout->addWidget(m_primaryHistogramWidget, 1);
    m_primaryHistogramCounts = std::make_unique<an::ui::HistogramCountScheduler>(m_primaryHistogramWidget);

    m_secondaryHistogramLabel = new QLabel(m_compareHistogramPanel);
    configureLabel(m_secondaryHistogramLabel);
    layout->addWidget(m_secondaryHistogramLabel);

    m_secondaryHistogramWidget = new HistogramWidget(m_compareHistogramPanel);
    m_secondaryHistogramWidget->setObjectName(QStringLiteral("secondaryCompareHistogramWidget"));
    configureHistogram(m_secondaryHistogramWidget);
    layout->addWidget(m_secondaryHistogramWidget, 1);
    m_secondaryHistogramCounts = std::make_unique<an::ui::HistogramCountScheduler>(m_secondaryHistogramWidget);

    m_ui.chartScrollArea->setWidget(m_compareHistogramPanel);

    refreshCompareHistogramLabelStyles();
    updateCompareHistogramLabels();
    updateCompareHistogramDisplayChannels(static_cast<int>(ImageCompositionMode::rgb_mode));
}

void ImageCompareProcess::setupCompareHistogramConnections()
{
    if (!m_primaryHistogramWidget || !m_secondaryHistogramWidget)
    {
        return;
    }
    auto applyToBoth = [this](const auto& function) {
        function(m_primaryHistogramWidget);
        function(m_secondaryHistogramWidget);
    };
    QObject::connect(m_ui.zoomIn, &QAbstractButton::clicked, this,
                     [applyToBoth]() { applyToBoth([](HistogramWidget* widget) { widget->zoomIn(); }); });
    QObject::connect(m_ui.zoomOut, &QAbstractButton::clicked, this,
                     [applyToBoth]() { applyToBoth([](HistogramWidget* widget) { widget->zoomOut(); }); });
    QObject::connect(m_ui.zoomReset, &QAbstractButton::clicked, this,
                     [applyToBoth]() { applyToBoth([](HistogramWidget* widget) { widget->resetZoom(); }); });
    QObject::connect(m_ui.gridButton, &QAbstractButton::clicked, this, [applyToBoth](bool checked) {
        applyToBoth([checked](HistogramWidget* widget) { widget->setGridEnabled(checked); });
    });
    QObject::connect(m_ui.localChannelNormalization, &QAbstractButton::clicked, this, [applyToBoth](bool checked) {
        applyToBoth([checked](HistogramWidget* widget) { widget->setLocalChannelNormalization(checked); });
    });
    QObject::connect(m_ui.showClippingInfo, &QAbstractButton::clicked, this, [applyToBoth](bool checked) {
        applyToBoth([checked](HistogramWidget* widget) { widget->setClippingInfoEnabled(checked); });
    });
    QObject::connect(m_ui.showHistogramAreas, &QAbstractButton::clicked, this, [applyToBoth](bool checked) {
        applyToBoth([checked](HistogramWidget* widget) { widget->setAreaModeEnabled(checked); });
    });
    QObject::connect(m_ui.plotResolution, &QComboBox::currentIndexChanged, this, [this](int index) {
        const an::ui::HistogramResolution resolution = resolutionAt(index);
        if (m_primaryHistogramWidget->getHistogramResolution() != resolution)
        {
            m_primaryHistogramWidget->setHistogramResolution(resolution);
            m_primaryHistogramCounts->recount();
        }
        if (m_secondaryHistogramWidget->getHistogramResolution() != resolution)
        {
            m_secondaryHistogramWidget->setHistogramResolution(resolution);
            m_secondaryHistogramCounts->recount();
        }
    });
}

void ImageCompareProcess::refreshCompareHistogramLabelStyles()
{
    const auto& tokenSet = an::ui::tokens();
    auto applyStyle = [&tokenSet](QLabel* label) {
        if (!label)
        {
            return;
        }
        QFont font = tokenSet.typography.caption;
        font.setWeight(QFont::DemiBold);
        label->setFont(font);
        label->setMargin(tokenSet.spacing.xs);
        QPalette palette = label->palette();
        palette.setColor(QPalette::Window, tokenSet.colors.bgCardSecondary);
        palette.setColor(QPalette::Mid, tokenSet.colors.borderSubtle);
        palette.setColor(QPalette::WindowText, tokenSet.colors.textSecondary);
        label->setPalette(palette);
    };
    applyStyle(m_primaryHistogramLabel);
    applyStyle(m_secondaryHistogramLabel);
}

void ImageCompareProcess::updateCompareHistogramPreview()
{
    if (!m_primaryHistogramCounts || !m_secondaryHistogramCounts)
    {
        return;
    }
    updateCompareHistogramLabels();
    m_primaryHistogramCounts->count(primaryHistogramAsset());
    m_secondaryHistogramCounts->count(secondaryHistogramAsset());
    updateCompareHistogramVisibility();
}

void ImageCompareProcess::updateCompareHistogramLabels()
{
    if (m_primaryHistogramLabel)
    {
        m_primaryHistogramLabel->setText(buildHistogramLabel(
            QStringLiteral("Image 1"), m_ui.targetImageSelector ? m_ui.targetImageSelector->currentText() : QString()));
    }
    if (m_secondaryHistogramLabel)
    {
        m_secondaryHistogramLabel->setText(buildHistogramLabel(
            QStringLiteral("Image 2"), m_ui.secondImageSelector ? m_ui.secondImageSelector->currentText() : QString()));
    }
}

void ImageCompareProcess::updateCompareHistogramVisibility()
{
    const bool splitModeEnabled = m_compareMode != an::ui::processes::ImageCompareMode::Toggle;
    const bool hasSecondaryImage = secondaryHistogramAsset() != nullptr;
    const bool showCompareHeaders = splitModeEnabled && hasSecondaryImage;
    if (m_primaryHistogramLabel)
    {
        m_primaryHistogramLabel->setVisible(showCompareHeaders);
    }
    if (m_secondaryHistogramLabel)
    {
        m_secondaryHistogramLabel->setVisible(showCompareHeaders);
    }
    if (m_secondaryHistogramWidget)
    {
        m_secondaryHistogramWidget->setVisible(splitModeEnabled && hasSecondaryImage);
    }
}

void ImageCompareProcess::updateCompareHistogramDisplayChannels(int mode)
{
    auto applyChannels = [mode](HistogramWidget* widget) {
        if (!widget)
        {
            return;
        }
        const auto compositionMode = static_cast<ImageCompositionMode>(mode);
        if (compositionMode == ImageCompositionMode::r_mode)
        {
            widget->setDisplayChannels(true, false, false);
        }
        else if (compositionMode == ImageCompositionMode::g_mode)
        {
            widget->setDisplayChannels(false, true, false);
        }
        else if (compositionMode == ImageCompositionMode::b_mode)
        {
            widget->setDisplayChannels(false, false, true);
        }
        else
        {
            widget->setDisplayChannels(true, true, true);
        }
    };
    applyChannels(m_primaryHistogramWidget);
    applyChannels(m_secondaryHistogramWidget);
}

std::shared_ptr<const an::consumer::ImageAsset> ImageCompareProcess::primaryHistogramAsset() const
{
    if (m_compareMode == an::ui::processes::ImageCompareMode::Toggle && m_rt_preview_view_control &&
        m_rt_preview_view_control->getViewWidget() &&
        m_rt_preview_view_control->getViewWidget()->isAlternateImageRenderEnabled() &&
        m_rt_preview_view_control->hasAlternateImage())
    {
        return m_rt_preview_view_control->alternateAsset();
    }
    if (m_rt_preview_view_control && m_rt_preview_view_control->hasImage())
    {
        return m_rt_preview_view_control->document().asset();
    }
    if (m_image_view_control && m_image_view_control->hasImage())
    {
        return m_image_view_control->document().asset();
    }
    return nullptr;
}

std::shared_ptr<const an::consumer::ImageAsset> ImageCompareProcess::secondaryHistogramAsset() const
{
    if (m_compareMode == an::ui::processes::ImageCompareMode::Toggle)
    {
        return nullptr;
    }
    if (std::shared_ptr<const an::consumer::ImageAsset> second = secondAsset())
    {
        return second;
    }
    if (m_rt_preview_view_control && m_rt_preview_view_control->hasAlternateImage())
    {
        return m_rt_preview_view_control->alternateAsset();
    }
    return nullptr;
}

ImageViewControl* ImageCompareProcess::secondView() const
{
    if (!m_ui.secondImageSelector)
    {
        return nullptr;
    }
    const QString currentText = m_ui.secondImageSelector->currentText();
    if (currentText.isEmpty() || currentText == NO_IMAGE_STR)
    {
        return nullptr;
    }
    return process_utils::findViewControlByName(currentText);
}

std::shared_ptr<const an::consumer::ImageAsset> ImageCompareProcess::secondAsset() const
{
    ImageViewControl* second = secondView();
    return second != nullptr && second->hasImage() ? second->document().asset() : nullptr;
}

void ImageCompareProcess::onViewControlCreated(const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlCreated(ctrl);
    if (isViewControlSupportedCustom(ctrl))
    {
        m_ui.secondImageSelector->addItem(QString::fromUtf8(ctrl->getUniqueName()));
        const int idx = m_ui.secondImageSelector->count() - 1;
        m_ui.secondImageSelector->setItemData(idx, an::ui::buildImageSelectorTooltip(ctrl), Qt::ToolTipRole);
    }
}

void ImageCompareProcess::onViewControlDestroyed(const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlDestroyed(ctrl);
    // The list is rebuilt without the view, whether or not the registry has let it go yet; the selection stays when
    // it is another view, and goes to no image - which clears the split's overlay - when it was this one.
    const QString selected = m_ui.secondImageSelector->currentText();
    const bool selectedGoes = selected == QString::fromUtf8(ctrl->getUniqueName());
    m_IgnoreLastImage2SelectorTextChangedEvent = true;
    m_ui.secondImageSelector->clear();
    loadImage2List(ctrl);
    m_IgnoreLastImage2SelectorTextChangedEvent = false;
    const int idx = selectedGoes ? -1 : m_ui.secondImageSelector->findText(selected);
    if (idx != -1)
    {
        const QSignalBlocker blocker(m_ui.secondImageSelector);
        m_ui.secondImageSelector->setCurrentIndex(idx);
    }
    else
    {
        m_ui.secondImageSelector->setCurrentText(NO_IMAGE_STR);
        on_secondImageSelector_currentTextChanged(NO_IMAGE_STR);
    }
    updateCompareHistogramLabels();
    updateCompareHistogramPreview();
}

void ImageCompareProcess::onViewControlRenamed(const std::string& newName, const std::string& oldName,
                                               const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlRenamed(newName, oldName, ctrl);
    const QString oldNameQt = QString::fromUtf8(oldName);
    const int idx = m_ui.secondImageSelector->findText(oldNameQt);
    if (idx != -1)
    {
        const QString currentText = m_ui.secondImageSelector->currentText();
        const QString newNameQt = QString::fromUtf8(newName);
        m_IgnoreLastImage2SelectorTextChangedEvent = true;
        m_ui.secondImageSelector->removeItem(idx);
        m_ui.secondImageSelector->addItem(newNameQt);
        const int newIdx = m_ui.secondImageSelector->count() - 1;
        m_ui.secondImageSelector->setItemData(newIdx, an::ui::buildImageSelectorTooltip(ctrl), Qt::ToolTipRole);
        if (currentText == oldNameQt)
        {
            m_ui.secondImageSelector->setCurrentText(newNameQt);
        }
        m_IgnoreLastImage2SelectorTextChangedEvent = false;
    }
    updateCompareHistogramLabels();
}

void ImageCompareProcess::onViewControlUpdated(const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlUpdated(ctrl);
    const QString currentSelectedImg = m_ui.secondImageSelector->currentText();
    if (ctrl == m_image_view_control)
    {
        // The selected second image has to still be one of the target's size.
        bool isStillValid = false;
        if (currentSelectedImg != NO_IMAGE_STR)
        {
            if (auto* vControl = process_utils::findViewControlByName(currentSelectedImg))
            {
                isStillValid = isViewControlSupportedCustom(vControl);
            }
        }
        if (!isStillValid)
        {
            m_ui.secondImageSelector->clear();
            loadImage2List();
            m_ui.secondImageSelector->setCurrentText(NO_IMAGE_STR);
        }
    }
    else
    {
        m_IgnoreLastImage2SelectorTextChangedEvent = true;
        m_ui.secondImageSelector->clear();
        loadImage2List();
        m_IgnoreLastImage2SelectorTextChangedEvent = false;
        const int idx = m_ui.secondImageSelector->findText(currentSelectedImg);
        if (idx != -1)
        {
            m_ui.secondImageSelector->setCurrentIndex(idx);
        }
        else
        {
            m_ui.secondImageSelector->setCurrentText(NO_IMAGE_STR);
        }
        if (ctrl == secondView())
        {
            updateSplitOverlayImage();
        }
    }
    updateCompareHistogramLabels();
    updateCompareHistogramPreview();
}

void ImageCompareProcess::onImageUpdated(ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onImageUpdated(ctrl);
    updateCompareHistogramPreview();
}

void ImageCompareProcess::onImageDisplayChannelChanged(ImageViewControl* ctrl, int mode)
{
    ImageRTPreviewControl::onImageDisplayChannelChanged(ctrl, mode);
    updateCompareHistogramDisplayChannels(mode);
}

void ImageCompareProcess::onAlternateImageEnabled(ImageViewControl* ctrl)
{
    Q_UNUSED(ctrl);
    if (m_suppressAlternateCallbacks)
    {
        return;
    }
    if (m_compareMode != an::ui::processes::ImageCompareMode::Toggle && m_splitModule && m_rt_preview_view_control)
    {
        // The toolbar's switch, in a split: swap the sides and keep the split module in charge of the display.
        swapSplitImages();
        m_suppressAlternateCallbacks = true;
        m_rt_preview_view_control->getViewWidget()->enableAlternateImageRendering(false);
        m_suppressAlternateCallbacks = false;
    }
    updateCompareHistogramPreview();
}

void ImageCompareProcess::onAlternateImageDisabled(ImageViewControl* ctrl)
{
    Q_UNUSED(ctrl);
    if (m_suppressAlternateCallbacks)
    {
        return;
    }
    updateCompareHistogramPreview();
}

void ImageCompareProcess::on_dialog_box_button_clicked(QAbstractButton* button)
{
    QDialogButtonBox* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
    {
        return;
    }
    if (buttonBox->standardButton(button) == QDialogButtonBox::Close)
    {
        this->close();
    }
}

void ImageCompareProcess::on_secondImageSelector_currentTextChanged(const QString& str)
{
    Q_UNUSED(str);
    if (m_IgnoreLastImage2SelectorTextChangedEvent || !m_rt_preview_view_control)
    {
        return;
    }
    const std::shared_ptr<const an::consumer::ImageAsset> second = secondAsset();
    if (m_compareMode == an::ui::processes::ImageCompareMode::Toggle)
    {
        m_rt_preview_view_control->setAlternateAsset(
            second ? second
                   : (m_rt_preview_view_control->hasImage() ? m_rt_preview_view_control->document().asset() : nullptr),
            true);
    }
    else
    {
        // The alternate stays set so the toolbar's switch works; the split module shows the second image.
        if (second)
        {
            m_rt_preview_view_control->setAlternateAsset(second, true);
        }
        updateSplitOverlayImage();
    }
    updateCompareHistogramLabels();
    updateCompareHistogramPreview();
}

bool ImageCompareProcess::isPreviewEnabled() const
{
    return true;
}

bool ImageCompareProcess::requiresExclusivePreviewAccess() const
{
    return false;
}

void ImageCompareProcess::enablePreview(bool enabled)
{
    Q_UNUSED(enabled);
}

void ImageCompareProcess::on_targetImageSelector_currentTextChangedInternal(const QString& str)
{
    Q_UNUSED(str);
    m_ui.secondImageSelector->clear();
    loadImage2List();
    m_ui.secondImageSelector->setCurrentText(NO_IMAGE_STR);
    updateCompareHistogramLabels();
    updateCompareHistogramPreview();
}

an::Status ImageCompareProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                        an::ui::ProcessDestination& destination,
                                                        const an::ui::ProcessRun& run, const std::monostate& params,
                                                        an::ui::ExecutionScope& scope)
{
    // The window compares; the destination already is the source's copy.
    Q_UNUSED(source);
    Q_UNUSED(destination);
    Q_UNUSED(run);
    Q_UNUSED(params);
    Q_UNUSED(scope);
    return an::Status::success();
}

std::shared_ptr<const an::consumer::ImageAsset> ImageCompareProcess::getAlternateAsset() const
{
    if (std::shared_ptr<const an::consumer::ImageAsset> second = secondAsset())
    {
        return second;
    }
    return ImageRTPreviewControl::getAlternateAsset();
}

void ImageCompareProcess::resetSettings()
{
    ImageRTPreviewControl::resetSettings();
}

void ImageCompareProcess::settingsUpdatedImpl() {}

void ImageCompareProcess::onDisplayModeChanged(ImageViewMode mode)
{
    ImageRTPreviewControl::onDisplayModeChanged(mode);
    updateSplitOverlayImage();
}

void ImageCompareProcess::executeProcessEndedImpl(const RTPreviewMode previewMode, const bool sequenceMode)
{
    Q_UNUSED(previewMode);
    Q_UNUSED(sequenceMode);
    updateCompareHistogramPreview();
}

void ImageCompareProcess::previewControlStarted()
{
    cleanupPreviewBindings();
    if (m_rt_preview_view_control)
    {
        m_rt_preview_view_control->setRTAlternatePreviewLabels("IMAGE 1", "IMAGE 2");
        if (m_splitModule && m_rt_preview_view_control->getViewWidget())
        {
            m_splitModule->setLabels(QStringLiteral("IMAGE 1"), QStringLiteral("IMAGE 2"));
            m_splitModule->attachToViewWidget(m_rt_preview_view_control->getViewWidget());
            m_rt_preview_view_control->getViewWidget()->installEventFilter(this);
            // The viewport overlay draws the splitter and the labels across the whole canvas, not only the image.
            if (auto* scrollArea = m_rt_preview_view_control->getScrollArea())
            {
                m_splitModule->installViewportOverlay(scrollArea->viewport());
                scrollArea->viewport()->installEventFilter(this);
            }
            setSplitHoverTrackingEnabled(m_compareMode != an::ui::processes::ImageCompareMode::Toggle);
        }
        applyCompareMode();
    }
    updateCompareHistogramPreview();
}

void ImageCompareProcess::previewControlClosed()
{
    cleanupPreviewBindings();
    updateCompareHistogramPreview();
}

bool ImageCompareProcess::isViewControlSupportedCustom(const ImageViewControl* ctrl) const
{
    if (ctrl == nullptr || m_image_view_control == nullptr || !ctrl->hasImage() || !m_image_view_control->hasImage())
    {
        return false;
    }
    const std::shared_ptr<const an::consumer::ImageAsset> candidate = ctrl->document().asset();
    const std::shared_ptr<const an::consumer::ImageAsset> target = m_image_view_control->document().asset();
    if (!candidate || !target)
    {
        return false;
    }
    const std::optional<an::ImageDescriptor> candidateDescribed = candidate->descriptor();
    const std::optional<an::ImageDescriptor> targetDescribed = target->descriptor();
    return candidateDescribed.has_value() && targetDescribed.has_value() &&
           candidateDescribed->extent().width() == targetDescribed->extent().width() &&
           candidateDescribed->extent().height() == targetDescribed->extent().height();
}

void ImageCompareProcess::cleanupPreviewBindings()
{
    setSplitHoverTrackingEnabled(false);
    if (m_rt_preview_view_control)
    {
        if (auto* viewWidget = m_rt_preview_view_control->getViewWidget())
        {
            viewWidget->removeEventFilter(this);
            viewWidget->clearQuickZoomImageOverride();
            m_lensOnSecond = false;
        }
        if (auto* scrollArea = m_rt_preview_view_control->getScrollArea())
        {
            restoreSplitCursor();
            scrollArea->viewport()->removeEventFilter(this);
        }
        m_rt_preview_view_control->setRTPreviewLabelEnabled(true);
    }
    if (m_splitModule)
    {
        m_splitModule->enableDisplayModule(false);
        m_splitModule->clearOverlayImage();
        m_splitModule->setSwapped(false);
        m_splitModule->removeViewportOverlay();
        m_splitModule->detachFromViewWidget();
    }
    m_overlay->generation.fetch_add(1u);
    m_overlay->pending.store(false);
    m_overlayPresented.reset();
    updateCompareHistogramPreview();
}

void ImageCompareProcess::loadImage2List(const ImageViewControl* except)
{
    m_ui.secondImageSelector->addItem(NO_IMAGE_STR);
    for (auto* vControl : process_utils::listViewControls())
    {
        if (vControl == m_image_view_control || vControl == except || !isViewControlSupportedCustom(vControl))
        {
            continue;
        }
        m_ui.secondImageSelector->addItem(QString::fromUtf8(vControl->getUniqueName()));
        const int idx = m_ui.secondImageSelector->count() - 1;
        m_ui.secondImageSelector->setItemData(idx, an::ui::buildImageSelectorTooltip(vControl), Qt::ToolTipRole);
    }
}

void ImageCompareProcess::on_compareModeChanged(int modeId)
{
    const auto newMode = static_cast<an::ui::processes::ImageCompareMode>(modeId);
    if (newMode == m_compareMode)
    {
        return;
    }
    m_compareMode = newMode;
    applyCompareMode();
}

void ImageCompareProcess::applyCompareMode()
{
    using Mode = an::ui::processes::ImageCompareMode;
    if (!m_rt_preview_view_control || !m_splitModule)
    {
        return;
    }
    if (m_compareMode == Mode::Toggle)
    {
        m_splitModule->enableDisplayModule(false);
        m_splitModule->clearOverlayImage();
        m_splitModule->setSwapped(false);
        m_overlay->generation.fetch_add(1u);
        m_overlay->pending.store(false);
        m_overlayPresented.reset();
        if (m_rt_preview_view_control->getViewWidget())
        {
            m_rt_preview_view_control->getViewWidget()->clearQuickZoomImageOverride();
        }
        m_lensOnSecond = false;
        setSplitHoverTrackingEnabled(false);
        m_rt_preview_view_control->setRTPreviewLabelEnabled(true);
        if (m_rt_preview_view_control->getScrollArea())
        {
            restoreSplitCursor();
        }
        const std::shared_ptr<const an::consumer::ImageAsset> second = secondAsset();
        m_rt_preview_view_control->setAlternateAsset(
            second ? second
                   : (m_rt_preview_view_control->hasImage() ? m_rt_preview_view_control->document().asset() : nullptr),
            true);
    }
    else
    {
        // The alternate stays set so the toolbar's switch can see it; its rendering is off, the split module shows
        // the second image.
        if (m_rt_preview_view_control->getViewWidget() &&
            m_rt_preview_view_control->getViewWidget()->isAlternateImageRenderEnabled())
        {
            m_suppressAlternateCallbacks = true;
            m_rt_preview_view_control->getViewWidget()->enableAlternateImageRendering(false);
            m_suppressAlternateCallbacks = false;
        }
        m_rt_preview_view_control->setRTPreviewLabelEnabled(false);
        setSplitHoverTrackingEnabled(true);
        m_splitModule->setOrientation(m_compareMode == Mode::HorizontalSplit ? an::ui::SplitOrientation::Horizontal
                                                                             : an::ui::SplitOrientation::Vertical);
        m_splitModule->enableDisplayModule(true);
        updateSplitOverlayImage();
    }
    updateCompareHistogramPreview();
}

void ImageCompareProcess::updateSplitOverlayImage()
{
    if (!m_splitModule)
    {
        return;
    }
    const std::shared_ptr<const an::consumer::ImageAsset> second = secondAsset();
    if (!second || m_compareMode == an::ui::processes::ImageCompareMode::Toggle)
    {
        // A split on its way no longer applies: its landing shows nothing, and nothing is pending.
        m_overlay->generation.fetch_add(1u);
        m_overlay->pending.store(false);
        m_overlayPresented.reset();
        m_splitModule->clearOverlayImage();
        return;
    }
    auto* context = an::ui::appContext();
    an::ui::IEngineSessionProvider* provider = context ? context->getEngineSessionProvider() : nullptr;
    if (provider == nullptr)
    {
        return;
    }
    // The second image is presented as the preview presents the first - its stretch, balance and channel - on the
    // thread pool under an operation of its own; the latest request wins.
    an::ui::ViewPresentationSettings settings;
    if (ImageViewWidget* previewWidget =
            m_rt_preview_view_control ? m_rt_preview_view_control->getViewWidget() : nullptr)
    {
        settings.request = previewWidget->presentationRequest();
    }
    else
    {
        settings.request.stretch = an::PresentationStretch::Linear;
    }
    settings.request.output = an::PresentationOutput::Interleaved8;
    const std::uint64_t generation = m_overlay->generation.fetch_add(1u) + 1u;
    m_overlay->pending.store(true);
    const an::consumer::EngineSession* session = &provider->engineSession();
    QPointer<ImageCompareProcess> guard(this);
    // The worker holds what it shares with the window: the window may go, as a close takes it, while the worker has yet
    // to start, and the window's stop then ends the worker's reads soon after they begin.
    const std::shared_ptr<OverlayWork> work = m_overlay;
    const std::stop_token stop = windowStopToken();
    QThreadPool::globalInstance()->start([session, guard, work, generation, stop, second, settings]() {
        std::shared_ptr<const an::ui::PresentedImage> presented;
        // The second image is leased before the presentation asks to be admitted: an edit of its view holds the writer
        // while it waits to be admitted.
        an::Result<an::consumer::AssetReadLease> lease = an::Status::failure(an::ErrorCode::Cancelled);
        if (work->generation.load() == generation)
        {
            lease = second->read(stop);
        }
        if (lease.hasValue())
        {
            an::Result<an::ui::ExecutionScope> admitted = an::ui::ExecutionScope::admit(
                *session,
                an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Preview, .stopToken = stop},
                an::ui::ExecutionObserver{});
            if (admitted.hasValue() && work->generation.load() == generation)
            {
                an::Status outcome = an::Status::success();
                an::Result<an::ui::ViewPresentationOutcome> shown = an::ui::presentForView(
                    admitted->engine(), admitted->execution(), *lease, settings, nullptr, nullptr);
                if (shown.hasValue())
                {
                    presented = std::move(shown->image);
                }
                else
                {
                    outcome = shown.status();
                }
                admitted->finish(outcome);
            }
        }
        postToGui([guard, work, generation, presented]() {
            if (!guard)
            {
                return;
            }
            if (work->generation.load() == generation)
            {
                work->pending.store(false);
            }
            guard->landOverlay(generation, presented);
        });
    });
}

void ImageCompareProcess::landOverlay(std::uint64_t generation, std::shared_ptr<const an::ui::PresentedImage> presented)
{
    if (m_overlay->generation.load() != generation || !m_splitModule)
    {
        return;
    }
    if (!presented)
    {
        m_overlayPresented.reset();
        m_splitModule->clearOverlayImage();
        return;
    }
    m_overlayPresented = std::move(presented);
    m_splitModule->setOverlayImage(an::ui::presentedQImage(m_overlayPresented));
    ++m_overlay->shown;
}

void ImageCompareProcess::swapSplitImages()
{
    if (m_splitModule)
    {
        m_splitModule->setSwapped(!m_splitModule->isSwapped());
    }
}

void ImageCompareProcess::updateQuickZoomOverrideForPos(const QPoint& widgetPos)
{
    if (!m_splitModule || !m_rt_preview_view_control || !m_rt_preview_view_control->getViewWidget())
    {
        return;
    }
    auto* viewWidget = m_rt_preview_view_control->getViewWidget();
    m_lensOnSecond = m_splitModule->isPointOnOverlaySide(widgetPos) && m_overlayPresented != nullptr;
    if (m_lensOnSecond)
    {
        viewWidget->setQuickZoomImageOverride(m_overlayPresented);
    }
    else
    {
        viewWidget->clearQuickZoomImageOverride();
    }
}

void ImageCompareProcess::setSplitHoverTrackingEnabled(bool enabled)
{
    if (!m_rt_preview_view_control)
    {
        return;
    }
    auto* viewWidget = m_rt_preview_view_control->getViewWidget();
    if (!viewWidget)
    {
        return;
    }
    QWidget* viewport = splitInteractionViewport();
    if (enabled)
    {
        if (m_splitHoverTrackingEnabled)
        {
            return;
        }
        m_previewMouseTrackingWasEnabled = viewWidget->hasMouseTracking();
        viewWidget->setMouseTracking(true);
        if (viewport)
        {
            m_previewViewportMouseTrackingWasEnabled = viewport->hasMouseTracking();
            viewport->setMouseTracking(true);
        }
        m_splitHoverTrackingEnabled = true;
        return;
    }
    if (!m_splitHoverTrackingEnabled)
    {
        return;
    }
    viewWidget->setMouseTracking(m_previewMouseTrackingWasEnabled);
    if (viewport)
    {
        viewport->setMouseTracking(m_previewViewportMouseTrackingWasEnabled);
    }
    m_splitHoverTrackingEnabled = false;
}

QWidget* ImageCompareProcess::splitInteractionViewport() const
{
    if (!m_rt_preview_view_control || !m_rt_preview_view_control->getScrollArea())
    {
        return nullptr;
    }
    return m_rt_preview_view_control->getScrollArea()->viewport();
}

bool ImageCompareProcess::isSplitInteractionTarget(QObject* watched) const
{
    if (!m_rt_preview_view_control || !m_rt_preview_view_control->getViewWidget())
    {
        return false;
    }
    return watched == m_rt_preview_view_control->getViewWidget() || watched == splitInteractionViewport();
}

QPoint ImageCompareProcess::mapSplitEventPosToWidget(QObject* watched, const QPoint& pos) const
{
    if (!m_rt_preview_view_control || !m_rt_preview_view_control->getViewWidget())
    {
        return pos;
    }
    auto* viewWidget = m_rt_preview_view_control->getViewWidget();
    auto* sourceWidget = qobject_cast<QWidget*>(watched);
    if (!sourceWidget || sourceWidget == viewWidget)
    {
        return pos;
    }
    return viewWidget->mapFrom(sourceWidget, pos);
}

void ImageCompareProcess::applySplitCursor() const
{
    if (!m_rt_preview_view_control || !m_rt_preview_view_control->getScrollArea() || !m_splitModule)
    {
        return;
    }
    const QCursor cursor(m_splitModule->orientation() == an::ui::SplitOrientation::Horizontal ? Qt::SplitHCursor
                                                                                              : Qt::SplitVCursor);
    m_rt_preview_view_control->getScrollArea()->setCursor(cursor);
    if (QWidget* viewport = splitInteractionViewport())
    {
        viewport->setCursor(cursor);
    }
}

void ImageCompareProcess::restoreSplitCursor() const
{
    if (!m_rt_preview_view_control || !m_rt_preview_view_control->getScrollArea())
    {
        return;
    }
    m_rt_preview_view_control->getScrollArea()->restoreDefaultCursor();
    if (QWidget* viewport = splitInteractionViewport())
    {
        viewport->unsetCursor();
    }
}

bool ImageCompareProcess::eventFilter(QObject* watched, QEvent* event)
{
    using Mode = an::ui::processes::ImageCompareMode;
    if (m_compareMode == Mode::Toggle || !m_splitModule || !m_splitModule->isDisplayModuleEnabled())
    {
        return QWidget::eventFilter(watched, event);
    }
    if (!m_rt_preview_view_control || !m_rt_preview_view_control->getViewWidget())
    {
        return QWidget::eventFilter(watched, event);
    }
    if (!isSplitInteractionTarget(watched))
    {
        return QWidget::eventFilter(watched, event);
    }
    auto* viewWidget = m_rt_preview_view_control->getViewWidget();
    const bool fromViewport = watched == splitInteractionViewport();
    switch (event->type())
    {
    case QEvent::MouseButtonPress: {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QPoint widgetPos = mapSplitEventPosToWidget(watched, mouseEvent->pos());
        if (mouseEvent->button() == Qt::LeftButton && m_splitModule->handleMousePress(widgetPos))
        {
            applySplitCursor();
            return true;
        }
        // A left click off the splitter: the lens shows the image of whichever side the cursor is over.
        if (!fromViewport && mouseEvent->button() == Qt::LeftButton)
        {
            updateQuickZoomOverrideForPos(widgetPos);
        }
        // A right click swaps the sides.
        if (!fromViewport && mouseEvent->button() == Qt::RightButton)
        {
            swapSplitImages();
            return true;
        }
        break;
    }
    case QEvent::MouseMove: {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QPoint widgetPos = mapSplitEventPosToWidget(watched, mouseEvent->pos());
        if (m_splitModule->isDragging())
        {
            m_splitModule->handleMouseMove(widgetPos);
            return true;
        }
        if (m_splitModule->isNearSplitter(widgetPos))
        {
            applySplitCursor();
            viewWidget->cancelQuickZoom();
            return true;
        }
        restoreSplitCursor();
        if (!fromViewport)
        {
            updateQuickZoomOverrideForPos(widgetPos);
        }
        break;
    }
    case QEvent::MouseButtonRelease: {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QPoint widgetPos = mapSplitEventPosToWidget(watched, mouseEvent->pos());
        if (mouseEvent->button() == Qt::LeftButton && m_splitModule->handleMouseRelease(widgetPos))
        {
            if (!m_splitModule->isNearSplitter(widgetPos))
            {
                restoreSplitCursor();
            }
            return true;
        }
        if (!fromViewport && mouseEvent->button() == Qt::LeftButton)
        {
            viewWidget->clearQuickZoomImageOverride();
            m_lensOnSecond = false;
        }
        if (!fromViewport && mouseEvent->button() == Qt::RightButton)
        {
            return true;
        }
        break;
    }
    case QEvent::Leave: {
        if (!m_splitModule->isDragging())
        {
            restoreSplitCursor();
        }
        viewWidget->clearQuickZoomImageOverride();
        m_lensOnSecond = false;
        break;
    }
    case QEvent::MouseButtonDblClick: {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QPoint widgetPos = mapSplitEventPosToWidget(watched, mouseEvent->pos());
        if (mouseEvent->button() == Qt::LeftButton && m_splitModule->isNearSplitter(widgetPos))
        {
            m_splitModule->setSplitPosition(0.5);
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}
