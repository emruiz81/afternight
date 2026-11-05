// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "StarReductionProcess.h"

#include "app/ProcessLauncher.h"
#include "controls/IAppContext.h"
#include "controls/ImageSelectorTooltipBuilder.h"
#include "controls/ImageViewControl.h"
#include "controls/ProgressDialogControl.h"
#include "controls/ProcessButtonIcons.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/ProcessCommonUtils.h"
#include "processes/StarlessGeneratorRegistry.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageReads.h"

#include <AfterNightEngine/Algorithms/Color/ColorTransform.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageTransfer.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightPlatform/Logging.h>

#include <QAbstractButton>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSize>
#include <QThreadPool>

#include <algorithm>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace
{
const QString kTransferMethod = QStringLiteral("Transfer Method");
const QString kHaloMethod = QStringLiteral("Halo Method");
const QString kStarMethod = QStringLiteral("Star Method");
const QString kLightnessMethod = QStringLiteral("Lightness Method");

const QString kStrongMode = QStringLiteral("Strong");
const QString kModerateMode = QStringLiteral("Moderate");
const QString kSoftMode = QStringLiteral("Soft");

const QString kAutoName = QStringLiteral("<Auto>");

QString defaultReducedName(const ImageViewControl* control)
{
    if (!control)
    {
        return QStringLiteral("Star Reduced");
    }
    return QString::fromUtf8(control->getUniqueName()).trimmed() + QStringLiteral(" - star reduced");
}

QString defaultStarsName(const ImageViewControl* control)
{
    if (!control)
    {
        return QStringLiteral("Reduced Stars");
    }
    return QString::fromUtf8(control->getUniqueName()).trimmed() + QStringLiteral(" - reduced stars");
}

QString outputNameOrDefault(const QString& text, const QString& default_name)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || trimmed == kAutoName)
    {
        return default_name;
    }
    return trimmed;
}

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// The session every Controls operation runs in, or null when the application has none.
[[nodiscard]] const an::consumer::EngineSession* currentSession()
{
    auto* context = an::ui::appContext();
    auto* provider = context != nullptr ? context->getEngineSessionProvider() : nullptr;
    return provider != nullptr && provider->engineSession().valid() ? &provider->engineSession() : nullptr;
}

/// A working image of a representation, described as the samples a reduction reads, uninitialized.
struct WorkingImage
{
    std::variant<std::monostate, an::Image<float>, an::Image<double>> image;
    void* samples = nullptr;
    std::optional<an::ImageLayout> layout;
};

[[nodiscard]] an::Result<WorkingImage> workingImage(const an::EngineContext& engine,
                                                    const an::ImageDescriptor& described)
{
    WorkingImage made;
    an::Status status = an::Status::success();
    auto create = [&]<typename WorkT>() {
        an::Result<an::Image<WorkT>> created =
            an::Image<WorkT>::create(engine, described, an::StorageArrangement::Planar,
                                     an::StorageInitialization::Uninitialized, an::ImageMetadata{});
        if (!created.hasValue())
        {
            status = created.status();
            return;
        }
        an::Image<WorkT>& image = made.image.template emplace<an::Image<WorkT>>(std::move(*created));
        const std::optional<an::ImageWriteAccess<WorkT>> access = image.write();
        if (!access.has_value())
        {
            status = refusal(an::ErrorCode::NotInitialized, "the working image holds nothing");
            return;
        }
        made.samples = access->view().data();
        made.layout = access->view().layout();
    };
    if (described.sampleType() == an::SampleType::Float64)
    {
        create.template operator()<double>();
    }
    else
    {
        create.template operator()<float>();
    }
    if (status.failed())
    {
        return status;
    }
    return made;
}

/// The description a reduction plans over: the target's samples as the working representation holds them, and for a
/// lightness reduction of a colour target the colour space and domain the lightness is read in - sRGB, or linear for
/// a target that says it is linear.
[[nodiscard]] an::Result<an::ImageDescriptor>
reductionDescriptor(const an::ImageDescriptor& target, an::SampleType working, const an::StarReductionRequest& request)
{
    an::Result<an::ImageDescriptor> described = an::describeSamplesAs(target, working);
    if (!described.hasValue())
    {
        return described;
    }
    if (request.method != an::StarReductionMethod::Lightness || !process_utils::describesColourImage(target))
    {
        // The working samples are fractions of full scale, whatever domain the stored ones were in.
        an::ImageDescriptorRequest normalized = described->toRequest();
        normalized.numericDomain = an::NumericDomain::NormalizedLinear;
        const std::optional<an::ImageDescriptor> restated = an::ImageDescriptor::create(normalized);
        if (!restated.has_value())
        {
            return refusal(an::ErrorCode::InvalidArgument, "the target cannot be described as unit fractions");
        }
        return *restated;
    }
    const std::optional<an::ColorSpace> space = an::colorSpaceOfEncoding(request.lightnessEncoding);
    const std::optional<an::NumericDomain> domain = an::numericDomainOfEncoding(request.lightnessEncoding);
    if (!space.has_value() || !domain.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the lightness encoding is not a complete colour description");
    }
    an::ImageDescriptorRequest restated = described->toRequest();
    restated.colorSpace = *space;
    restated.numericDomain = *domain;
    const std::optional<an::ImageDescriptor> lightnessDescribed = an::ImageDescriptor::create(restated);
    if (!lightnessDescribed.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the target cannot be described in the lightness's encoding");
    }
    return *lightnessDescribed;
}

/// Brings a normalized copy into a working representation: the copy itself when it is in it already, a converted
/// image otherwise, kept by `holder`.
[[nodiscard]] an::Result<std::pair<void*, an::ImageLayout>> asWorking(const an::ui::ExecutionScope& scope,
                                                                      process_utils::NormalizedCopy& copy,
                                                                      const an::ImageDescriptor& workingDescribed,
                                                                      std::optional<WorkingImage>& holder)
{
    if (copy.samples.sampleType() == workingDescribed.sampleType())
    {
        return std::pair<void*, an::ImageLayout>{copy.samples.samples(), copy.samples.layout()};
    }
    an::Result<WorkingImage> made = workingImage(scope.engine(), workingDescribed);
    if (!made.hasValue())
    {
        return made.status();
    }
    holder.emplace(std::move(*made));
    an::ConversionCounters counters;
    const an::Status converted = an::convertImageSamples(
        scope.execution(), copy.samples.sampleType(), copy.samples.samples(), copy.samples.layout(),
        workingDescribed.sampleType(), holder->samples, *holder->layout, an::ConversionPolicy{}, counters);
    if (converted.failed())
    {
        return converted;
    }
    return std::pair<void*, an::ImageLayout>{holder->samples, *holder->layout};
}

} // namespace

namespace an::ui::processes::star_reduction_detail
{

an::StarReductionMethod methodFromText(const QString& text)
{
    if (text == kTransferMethod)
    {
        return an::StarReductionMethod::Transfer;
    }
    if (text == kHaloMethod)
    {
        return an::StarReductionMethod::Halo;
    }
    if (text == kLightnessMethod)
    {
        return an::StarReductionMethod::Lightness;
    }
    return an::StarReductionMethod::Star;
}

an::StarReductionStrength strengthFromText(const QString& text)
{
    if (text == kModerateMode)
    {
        return an::StarReductionStrength::Moderate;
    }
    if (text == kSoftMode)
    {
        return an::StarReductionStrength::Soft;
    }
    return an::StarReductionStrength::Strong;
}

} // namespace an::ui::processes::star_reduction_detail

namespace detail = an::ui::processes::star_reduction_detail;

struct StarReductionProcess::Impl
{
    an::ui::processes::StarReductionProcessUiElements ui;
    std::vector<an::ui::starless_generators::StarlessGeneratorDescriptor> starlessGenerators;
    bool ignoreStarlessSelector = false;
    bool pendingGeneratedStarlessSelection = false;
    bool buildingProtectionMask = false;
    QString generatedSmallMaskName;

    /// The reduced-stars image an Apply made, waiting to be opened once the result lands.
    process_utils::StagedOutputs stagedOutputs;
};

StarReductionProcess::StarReductionProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<StarReductionProcessParams>(control, getProcessNameStatic()),
      m_impl(std::make_unique<Impl>())
{
    m_impl->ui = an::ui::processes::buildStarReductionProcessUi(this);
    setProcessDescription(QStringLiteral(
        "Reduce star intensity from a target image using a matching starless image and optional small-star mask."));

    m_SupportTargetViewControlSelection = true;
    m_SupportPerChannelProcessing = false;
    m_FixedLayout = false;
    m_ShowWaitingSpinner = false;

    m_ImagePreviewLayout = m_impl->ui.previewLayout;
    m_MainHorizontalLayout = m_impl->ui.horizontalLayoutMain;

    initializeStandardProcess(m_impl->ui.horizontalLayoutMain, m_impl->ui.previewLayout, m_impl->ui.preview,
                              m_impl->ui.buttonBox, m_impl->ui.targetImageSelector, nullptr,
                              {.supportPerChannel = false, .showWaitingSpinner = false});

    QObject::connect(m_impl->ui.starlessImageSelector, &QComboBox::currentTextChanged, this,
                     &StarReductionProcess::on_starlessImageSelector_currentTextChanged);
    QObject::connect(m_impl->ui.starlessGeneratorSelector, &QComboBox::currentTextChanged, this,
                     &StarReductionProcess::on_starlessGeneratorSelector_currentTextChanged);
    QObject::connect(m_impl->ui.method, &QComboBox::currentTextChanged, this,
                     &StarReductionProcess::on_method_currentTextChanged);
    QObject::connect(m_impl->ui.stretchFactor, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                     [this](double) { settingsUpdated(); });
    QObject::connect(m_impl->ui.mode, &QComboBox::currentTextChanged, this,
                     [this](const QString&) { settingsUpdated(); });
    QObject::connect(m_impl->ui.iterations, qOverload<int>(&QSpinBox::valueChanged), this,
                     [this](int) { settingsUpdated(); });
    QObject::connect(m_impl->ui.reduceTargetImage, &QCheckBox::checkStateChanged, this,
                     &StarReductionProcess::on_output_option_changed);
    QObject::connect(m_impl->ui.createNewImage, &QCheckBox::checkStateChanged, this,
                     &StarReductionProcess::on_output_option_changed);
    QObject::connect(m_impl->ui.createStarsImage, &QCheckBox::checkStateChanged, this,
                     &StarReductionProcess::on_output_option_changed);
    QObject::connect(m_impl->ui.enableReverseStretch, &QCheckBox::checkStateChanged, this,
                     &StarReductionProcess::on_output_option_changed);
    QObject::connect(m_impl->ui.newImageId, &QLineEdit::textChanged, this,
                     [this](const QString&) { settingsUpdated(); });
    QObject::connect(m_impl->ui.starImageId, &QLineEdit::textChanged, this,
                     [this](const QString&) { settingsUpdated(); });
    QObject::connect(m_impl->ui.generateStarlessView, &QPushButton::clicked, this,
                     &StarReductionProcess::on_generateStarlessView_clicked);
    QObject::connect(m_impl->ui.initializeSmallStarProtection, &QPushButton::clicked, this,
                     &StarReductionProcess::on_initializeSmallStarProtection_clicked);
    QObject::connect(m_impl->ui.enableSmallStarProtection, &QCheckBox::checkStateChanged, this,
                     &StarReductionProcess::on_enableSmallStarProtection_clicked);
    {
        const QSignalBlocker method_blocker(m_impl->ui.method);
        const QSignalBlocker mode_blocker(m_impl->ui.mode);
        m_impl->ui.method->addItems({kTransferMethod, kHaloMethod, kStarMethod, kLightnessMethod});
        m_impl->ui.method->setCurrentText(kStarMethod);
        m_impl->ui.mode->addItems({kStrongMode, kModerateMode, kSoftMode});
        m_impl->ui.mode->setCurrentText(kStrongMode);
#if QT_CONFIG(tooltip)
        auto set_item_tooltip = [](QComboBox* combo, const QString& text, const QString& tooltip) {
            if (!combo)
            {
                return;
            }
            const int index = combo->findText(text);
            if (index >= 0)
            {
                combo->setItemData(index, tooltip, Qt::ToolTipRole);
            }
        };
        set_item_tooltip(m_impl->ui.method, kTransferMethod,
                         tr("Uses a midtones transfer curve between the target and starless image. Useful for a "
                            "controlled, PixInsight-style reduction with the stretch factor."));
        set_item_tooltip(
            m_impl->ui.method, kHaloMethod,
            tr("Targets the halo component around stars with a transfer-curve correction. It can be useful "
               "when the stars themselves are acceptable but their glow is too strong."));
        set_item_tooltip(
            m_impl->ui.method, kStarMethod,
            tr("Iteratively recombines the target with the starless image. This is the recommended general "
               "method for reducing normal stars."));
        set_item_tooltip(m_impl->ui.method, kLightnessMethod,
                         tr("Uses CIE lightness for RGB images, or mono intensity for grayscale images, to guide the "
                            "iterative reduction. It is often gentler on bright structures."));
        set_item_tooltip(
            m_impl->ui.mode, kStrongMode,
            tr("Applies the full Star or Lightness method result. Best when stars need a clear reduction."));
        set_item_tooltip(m_impl->ui.mode, kModerateMode,
                         tr("Blends back some original star intensity after the iterative step. A balanced choice for "
                            "most images."));
        set_item_tooltip(m_impl->ui.mode, kSoftMode,
                         tr("Uses the gentlest post-processing blend. Best for subtle cleanup or when you want to keep "
                            "star color and core structure."));
#endif // QT_CONFIG(tooltip)
    }

    loadStarlessImageList();
    loadStarlessGenerators();
    m_impl->ui.starlessImageSelector->setCurrentText(NO_IMAGE_STR);
    updateParameterControlState();
    updateOutputControlState();
    updateStarlessGeneratorControlState();
    validateParameters();

    setExecutionAutostretchSupported(true);
    setupPreviewControl(true, QSize(1024, 768), QStringLiteral("Star Reduction Preview"));
    refreshTheme();
}

std::string StarReductionProcess::getProcessNameStatic()
{
    return "Star Reduction";
}

bool StarReductionProcess::requiresExclusiveAccessStatic()
{
    return false;
}

void StarReductionProcess::refreshTheme()
{
    ImageRTPreviewControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_impl->ui.buttonBox, iconProvider.get());
        m_impl->ui.generateStarlessView->setIcon(iconProvider->getIcon(QStringLiteral("stars")));
        m_impl->ui.initializeSmallStarProtection->setIcon(iconProvider->getIcon(QStringLiteral("mask")));
    }
}

void StarReductionProcess::onViewControlCreated(const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlCreated(ctrl);
    if (isStarlessViewSupported(ctrl))
    {
        m_impl->ui.starlessImageSelector->addItem(QString::fromUtf8(ctrl->getUniqueName()));
        const int idx = m_impl->ui.starlessImageSelector->count() - 1;
        m_impl->ui.starlessImageSelector->setItemData(idx, an::ui::buildImageSelectorTooltip(ctrl), Qt::ToolTipRole);
        if (m_impl->pendingGeneratedStarlessSelection)
        {
            m_impl->pendingGeneratedStarlessSelection = false;
            m_impl->ui.starlessImageSelector->setCurrentText(QString::fromUtf8(ctrl->getUniqueName()));
        }
    }
}

void StarReductionProcess::onViewControlDestroyed(const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlDestroyed(ctrl);

    const QString name = QString::fromUtf8(ctrl->getUniqueName());
    const int idx = m_impl->ui.starlessImageSelector->findText(name);
    if (idx >= 0)
    {
        if (m_impl->ui.starlessImageSelector->currentText() == name)
        {
            m_impl->ui.starlessImageSelector->setCurrentText(NO_IMAGE_STR);
        }
        m_impl->ui.starlessImageSelector->removeItem(idx);
    }
}

void StarReductionProcess::onViewControlRenamed(const std::string& newName, const std::string& oldName,
                                                const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlRenamed(newName, oldName, ctrl);

    const QString old_name = QString::fromUtf8(oldName);
    const int idx = m_impl->ui.starlessImageSelector->findText(old_name);
    if (idx < 0)
    {
        return;
    }

    const QString current = m_impl->ui.starlessImageSelector->currentText();
    const QString new_name = QString::fromUtf8(newName);
    m_impl->ignoreStarlessSelector = true;
    m_impl->ui.starlessImageSelector->removeItem(idx);
    if (isStarlessViewSupported(ctrl))
    {
        m_impl->ui.starlessImageSelector->addItem(new_name);
        const int new_idx = m_impl->ui.starlessImageSelector->count() - 1;
        m_impl->ui.starlessImageSelector->setItemData(new_idx, an::ui::buildImageSelectorTooltip(ctrl),
                                                      Qt::ToolTipRole);
        if (current == old_name)
        {
            m_impl->ui.starlessImageSelector->setCurrentText(new_name);
        }
    }
    m_impl->ignoreStarlessSelector = false;
}

void StarReductionProcess::onViewControlUpdated(const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlUpdated(ctrl);
    if (ctrl == m_image_view_control ||
        m_impl->ui.starlessImageSelector->currentText() == QString::fromUtf8(ctrl->getUniqueName()))
    {
        const QString selected = m_impl->ui.starlessImageSelector->currentText();
        loadStarlessImageList();
        if (m_impl->ui.starlessImageSelector->findText(selected) >= 0)
        {
            m_impl->ui.starlessImageSelector->setCurrentText(selected);
        }
        else
        {
            m_impl->ui.starlessImageSelector->setCurrentText(NO_IMAGE_STR);
        }
    }
}

StarReductionProcess::~StarReductionProcess() = default;

bool StarReductionProcess::isPreviewEnabled() const
{
    return m_impl->ui.preview->isChecked();
}

void StarReductionProcess::enablePreview(const bool enabled)
{
    m_impl->ui.preview->setChecked(enabled);
}

void StarReductionProcess::settingsUpdatedImpl()
{
    validateParameters();
}

void StarReductionProcess::enableProcessControlsImpl(const bool enabled)
{
    validateParameters();
    if (auto* ok = m_impl->ui.buttonBox->button(QDialogButtonBox::Ok))
    {
        ok->setEnabled(enabled && ok->isEnabled());
    }
}

void StarReductionProcess::resetSettings()
{
    ImageRTPreviewControl::resetSettings();
    m_impl->ui.method->setCurrentText(kStarMethod);
    m_impl->ui.mode->setCurrentText(kStrongMode);
    m_impl->ui.iterations->setValue(1);
    m_impl->ui.stretchFactor->setValue(0.2);
    m_impl->ui.reduceTargetImage->setChecked(false);
    m_impl->ui.createNewImage->setChecked(true);
    m_impl->ui.newImageId->setText(kAutoName);
    m_impl->ui.createStarsImage->setChecked(true);
    m_impl->ui.enableReverseStretch->setChecked(false);
    m_impl->ui.starImageId->setText(kAutoName);
    updateParameterControlState();
    updateOutputControlState();
}

void StarReductionProcess::previewControlStarted()
{
    if (m_rt_preview_view_control)
    {
        m_rt_preview_view_control->setRTAlternatePreviewLabels("PREVIEW", "TARGET IMAGE");
    }
}

void StarReductionProcess::executeProcessStarted(const RTPreviewMode previewMode, const bool sequenceMode)
{
    if (previewMode == RTPreviewMode::NoPreview && !sequenceMode)
    {
        m_impl->stagedOutputs.clear();
    }
}

void StarReductionProcess::executeProcessCompletedImpl(const RTPreviewMode previewMode, const bool sequenceMode)
{
    if (sequenceMode || previewMode != RTPreviewMode::NoPreview)
    {
        return;
    }
    process_utils::openStagedOutputs(m_impl->stagedOutputs.take(), m_image_view_control);
}

void StarReductionProcess::getBatchExecutionParameters(bool& createNewImage, bool& closeAtEnd,
                                                       bool& executeInBackground, std::string& newImageName)
{
    const StarReductionProcessParams params = getParams();
    createNewImage = params.createNewImage || !params.reduceTargetImage;
    closeAtEnd = false;
    executeInBackground = true;
    newImageName =
        outputNameOrDefault(params.newImageId, defaultReducedName(m_image_view_control)).toUtf8().toStdString();
}

an::Status StarReductionProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                         an::ui::ProcessDestination& destination,
                                                         const an::ui::ProcessRun& run,
                                                         const StarReductionProcessParams& params,
                                                         an::ui::ExecutionScope& scope)
{
    if (!params.starless)
    {
        // The preview shows the target until a starless image is chosen; an Apply needs one.
        return run.isForPreview()
                   ? an::Status::success()
                   : refusal(an::ErrorCode::InvalidArgument, "select a starless image of the target to reduce against");
    }
    const std::optional<an::ImageDescriptor> targetDescribed = source.descriptor();
    if (!targetDescribed.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the target holds nothing");
    }
    const an::EngineContext& engine = scope.engine();
    const an::ExecutionContext& execution = scope.execution();

    // The starless image, read through the lease the run took before it was admitted, and cropped to the part of the
    // target a run over a part reads - the preview area, or the part a frame over what the preview view shows runs
    // over - as the framework cropped the target.
    an::Result<an::consumer::AssetReadLease> starlessLease =
        an::ui::readBeside(scope, *params.starless, execution.stopToken());
    if (!starlessLease.hasValue())
    {
        return starlessLease.status();
    }
    an::ui::ProcessSource starlessSource = an::ui::ProcessSource::of(*starlessLease);
    std::optional<an::consumer::ImageAsset> croppedStarless;
    std::optional<an::consumer::AssetReadLease> croppedLease;
    const std::optional<an::ImageDescriptor> starlessDescribed = starlessSource.descriptor();
    if (!starlessDescribed.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the starless image holds nothing");
    }
    const QRect part = !run.part.isEmpty() ? run.part : (run.isForPreview() ? params.previewArea : QRect());
    if (!part.isEmpty() && (starlessDescribed->extent().width() != targetDescribed->extent().width() ||
                            starlessDescribed->extent().height() != targetDescribed->extent().height()))
    {
        an::Result<an::consumer::ImageAsset> cropped = an::ui::cropProcessRegion(
            scope.session(), engine, execution, starlessLease->image(), starlessLease->validity(), part);
        if (!cropped.hasValue())
        {
            return cropped.status();
        }
        croppedStarless.emplace(std::move(*cropped));
        an::Result<an::consumer::AssetReadLease> lease = croppedStarless->read(execution.stopToken());
        if (!lease.hasValue())
        {
            return lease.status();
        }
        croppedLease.emplace(std::move(*lease));
        starlessSource = an::ui::ProcessSource::of(*croppedLease);
    }
    const std::optional<an::ImageDescriptor> pairedDescribed = starlessSource.descriptor();
    if (!pairedDescribed.has_value() || pairedDescribed->extent().width() != targetDescribed->extent().width() ||
        pairedDescribed->extent().height() != targetDescribed->extent().height() ||
        pairedDescribed->extent().channelCount() != targetDescribed->extent().channelCount())
    {
        return refusal(an::ErrorCode::IncompatibleArguments,
                       "the starless image does not have the target's size and channels");
    }

    // Both images in fractions of full scale and one working representation, every sample held to the unit interval.
    an::Result<process_utils::NormalizedSamples> target =
        process_utils::openNormalizedDestination(scope, source, destination);
    if (!target.hasValue())
    {
        return target.status();
    }
    an::Result<process_utils::NormalizedCopy> starless = process_utils::normalizedCopyOf(scope, starlessSource);
    if (!starless.hasValue())
    {
        return starless.status();
    }
    an::StarReductionRequest request = params.engine;
    if (request.method == an::StarReductionMethod::Lightness && process_utils::describesColourImage(*targetDescribed))
    {
        request.lightnessEncoding =
            an::ui::processChannelScopeRequest(an::ChannelScope::CieLightness, *targetDescribed).sourceEncoding;
    }
    const an::Result<an::ImageDescriptor> planned =
        reductionDescriptor(*targetDescribed, target->sampleType(), request);
    if (!planned.hasValue())
    {
        return planned.status();
    }
    std::optional<WorkingImage> starlessHolder;
    an::Result<std::pair<void*, an::ImageLayout>> starlessWorking =
        asWorking(scope, *starless, *planned, starlessHolder);
    if (!starlessWorking.hasValue())
    {
        return starlessWorking.status();
    }
    if (an::Status held = process_utils::holdToUnitInterval(engine, execution, target->sampleType(), target->samples(),
                                                            target->layout());
        held.failed())
    {
        return held;
    }
    if (an::Status held = process_utils::holdToUnitInterval(engine, execution, planned->sampleType(),
                                                            starlessWorking->first, starlessWorking->second);
        held.failed())
    {
        return held;
    }

    const an::Result<an::StarReductionPlan> plan = an::planStarReduction(*planned, *planned, request);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::Result<WorkingImage> reduced = workingImage(engine, *planned);
    if (!reduced.hasValue())
    {
        return reduced.status();
    }
    const an::Result<an::StarReductionResult> ran =
        an::reduceStars(execution, target->samples(), target->layout(), starlessWorking->first, starlessWorking->second,
                        reduced->samples, *reduced->layout, *plan);
    if (!ran.hasValue())
    {
        return ran.status();
    }
    if (an::Status copied = an::copyImageSamples(execution, target->sampleType(), reduced->samples, *reduced->layout,
                                                 target->samples(), target->layout());
        copied.failed())
    {
        return copied;
    }
    if (an::Status committed = target->commit(execution); committed.failed())
    {
        return committed;
    }
    if (run.isForPreview())
    {
        return an::Status::success();
    }

    // The stars the reduction removed, extracted from the result as the target's mask weighs it.
    if (params.createStarsImage && !run.isForSequence())
    {
        an::Result<an::ui::ProcessDestination> weighed = an::ui::ProcessDestination::copyOf(
            scope.session(), engine, execution, an::ui::ProcessSource::of(destination.asset()));
        if (!weighed.hasValue())
        {
            return weighed.status();
        }
        if (params.mask)
        {
            an::Result<an::consumer::AssetReadLease> maskLease =
                an::ui::readBeside(scope, *params.mask, execution.stopToken());
            if (!maskLease.hasValue())
            {
                return maskLease.status();
            }
            const an::Status blended = an::ui::blendProcessResult(
                engine, execution, source, *weighed,
                an::ui::ProcessBlendRequest{
                    .modulation = 1.0, .mask = &maskLease->image(), .invertMask = params.maskInverted});
            if (blended.failed())
            {
                return blended;
            }
        }
        an::Result<process_utils::NormalizedSamples> weighedSamples =
            process_utils::openNormalizedDestination(scope, source, *weighed);
        if (!weighedSamples.hasValue())
        {
            return weighedSamples.status();
        }
        if (an::Status held = process_utils::holdToUnitInterval(engine, execution, weighedSamples->sampleType(),
                                                                weighedSamples->samples(), weighedSamples->layout());
            held.failed())
        {
            return held;
        }
        // The stars image is described as the target is, in its representation.
        const an::ImageDescriptor& starsDescribed = *targetDescribed;
        an::Result<an::ImageMetadata> starsMetadata = process_utils::derivedImageMetadata(
            source.metadata(), starsDescribed,
            params.reverseStretch ? "reduced-stars-reverse-stretch" : "reduced-stars");
        if (!starsMetadata.hasValue())
        {
            return starsMetadata.status();
        }
        an::Result<an::consumer::ImageAsset> stars =
            process_utils::createDerivedImage(scope, starsDescribed, std::move(*starsMetadata));
        if (!stars.hasValue())
        {
            return stars.status();
        }
        {
            an::Result<an::consumer::AssetWriteLease> starsLease = stars->write();
            if (!starsLease.hasValue())
            {
                return starsLease.status();
            }
            const an::Status extracted = starsLease->visitWrite([&](auto&& access) -> an::Status {
                const an::ImageLayout starsLayout = access.view().layout();
                an::Result<process_utils::NormalizedSamples> starsSamples = process_utils::NormalizedSamples::open(
                    engine, execution, starsDescribed, access.view().data(), starsLayout, an::KernelValidityView{});
                if (!starsSamples.hasValue())
                {
                    return starsSamples.status();
                }
                const an::Result<an::StarReductionResult> result = an::extractReducedStars(
                    execution, weighedSamples->samples(), weighedSamples->layout(), starlessWorking->first,
                    starlessWorking->second, starsSamples->samples(), starsSamples->layout(), *plan,
                    an::ReducedStarExtractionRequest{.reverseStretch = params.reverseStretch,
                                                     .stretchFactor = params.engine.stretchFactor});
                if (!result.hasValue())
                {
                    return result.status();
                }
                return starsSamples->commit(execution);
            });
            if (extracted.failed())
            {
                return extracted;
            }
        }
        m_impl->stagedOutputs.stage(process_utils::StagedOutput{
            .asset = std::move(*stars),
            .name = params.starsImageName,
            .history = fmt::format("Star Reduction: Reduced-stars image created from '{}'", params.targetName)});
    }

    return process_utils::recordNonlinearResult(
        scope, destination,
        process_utils::NonlinearResultRecord{
            .stateOperation = "star-reduction",
            .engineOperation = "an::reduceStars",
            .detail = fmt::format("{}, {}, {} iterations, stretch {}, against '{}'",
                                  an::starReductionMethodName(params.engine.method),
                                  an::starReductionStrengthName(params.engine.strength), params.engine.iterations,
                                  params.engine.stretchFactor, params.starlessImageName.toStdString())});
}

an::ui::PreviewScaling StarReductionProcess::previewScaling() const
{
    // Every sample is reduced from the target's and the starless image's samples at the same place - its lightness
    // weight from the same pixel - so an interactive frame runs at full scale over the part of the target the preview
    // view shows, the starless image cut to the same part, with no reach around it.
    return an::ui::PreviewScaling::NativeRegion;
}

an::ui::ProcessStep StarReductionProcess::describeTypedProcessStep(const StarReductionProcessParams& params) const
{
    return an::ui::ProcessStep{.name = "Star Reduction",
                               .description = "Star reduction using " + params.starlessImageName.toStdString()};
}

// The starless image and the mask: other views' images the body reads, leased before the run is admitted.
std::vector<std::shared_ptr<const an::consumer::ImageAsset>>
StarReductionProcess::typedImagesRead(const StarReductionProcessParams& params) const
{
    return {params.starless, params.mask};
}

StarReductionProcessParams StarReductionProcess::getParams() const
{
    StarReductionProcessParams params;
    params.targetImageName = m_impl->ui.targetImageSelector->currentText();
    params.starlessImageName = m_impl->ui.starlessImageSelector->currentText();
    if (params.starlessImageName != NO_IMAGE_STR && !params.starlessImageName.trimmed().isEmpty())
    {
        if (auto* starless = process_utils::findViewControlByName(params.starlessImageName);
            starless != nullptr && starless->hasImage())
        {
            params.starless = starless->document().asset();
        }
    }
    params.engine.method = detail::methodFromText(m_impl->ui.method->currentText());
    params.engine.strength = detail::strengthFromText(m_impl->ui.mode->currentText());
    params.engine.iterations = static_cast<std::uint32_t>(m_impl->ui.iterations->value());
    params.engine.stretchFactor = m_impl->ui.stretchFactor->value();
    params.reduceTargetImage = m_impl->ui.reduceTargetImage->isChecked();
    params.createNewImage = m_impl->ui.createNewImage->isChecked();
    params.newImageId = m_impl->ui.newImageId->text();
    params.createStarsImage = m_impl->ui.createStarsImage->isChecked();
    params.reverseStretch = m_impl->ui.enableReverseStretch->isChecked();
    params.starImageId = m_impl->ui.starImageId->text();
    params.previewArea = previewArea();
    if (m_image_view_control != nullptr && m_image_view_control->isMasked() && m_image_view_control->isMaskEnabled() &&
        m_SupportMasking)
    {
        if (ImageViewControl* mask = m_image_view_control->getCurrentMask(); mask != nullptr && mask->hasImage())
        {
            params.mask = mask->document().asset();
            params.maskInverted = m_image_view_control->isMaskInverted();
        }
    }
    params.starsImageName = outputNameOrDefault(params.starImageId, defaultStarsName(m_image_view_control));
    params.targetName = m_image_view_control != nullptr ? m_image_view_control->getUniqueName() : std::string();
    return params;
}

void StarReductionProcess::on_targetImageSelector_currentTextChangedInternal(const QString& /*str*/)
{
    const QString selected = m_impl->ui.starlessImageSelector->currentText();
    loadStarlessImageList();
    if (m_impl->ui.starlessImageSelector->findText(selected) >= 0)
    {
        m_impl->ui.starlessImageSelector->setCurrentText(selected);
    }
    else
    {
        m_impl->ui.starlessImageSelector->setCurrentText(NO_IMAGE_STR);
    }
    validateParameters();
    settingsUpdated();
}

void StarReductionProcess::on_starlessImageSelector_currentTextChanged(const QString& /*str*/)
{
    if (m_impl->ignoreStarlessSelector)
    {
        return;
    }
    validateParameters();
    settingsUpdated();
}

void StarReductionProcess::on_method_currentTextChanged(const QString& /*str*/)
{
    updateParameterControlState();
    settingsUpdated();
}

void StarReductionProcess::on_output_option_changed(Qt::CheckState /*state*/)
{
    updateOutputControlState();
    settingsUpdated();
}

void StarReductionProcess::on_starlessGeneratorSelector_currentTextChanged(const QString& /*str*/)
{
    updateStarlessGeneratorControlState();
}

void StarReductionProcess::on_generateStarlessView_clicked()
{
    loadStarlessGenerators();

    const QString process_id = m_impl->ui.starlessGeneratorSelector->currentData(Qt::UserRole).toString();
    const auto generator_it = std::find_if(
        m_impl->starlessGenerators.begin(), m_impl->starlessGenerators.end(),
        [&process_id](const auto& generator) { return QString::fromUtf8(generator.process_id) == process_id; });
    if (generator_it == m_impl->starlessGenerators.end() || !generator_it->available)
    {
        notifyErrorMessageAsync(QStringLiteral("Star Reduction"),
                                QStringLiteral("No configured starless generator is available."));
        return;
    }

    if (generator_it->is_native_starnet)
    {
        // StarNet is opened configured as the reduction wants it, and hands back the starless view it makes.
        QPointer<StarReductionProcess> guard(this);
        an::ui::ProcessLauncher::openIntegratedStarlessGeneration(
            m_image_view_control, [guard](ImageViewControl* starlessView) {
                if (!guard || !starlessView)
                {
                    return;
                }

                const QString starless_name = QString::fromUtf8(starlessView->getUniqueName());
                if (guard->m_impl->ui.starlessImageSelector->findText(starless_name) < 0)
                {
                    guard->m_impl->ui.starlessImageSelector->addItem(starless_name);
                    const int idx = guard->m_impl->ui.starlessImageSelector->count() - 1;
                    guard->m_impl->ui.starlessImageSelector->setItemData(
                        idx, an::ui::buildImageSelectorTooltip(starlessView), Qt::ToolTipRole);
                }
                guard->m_impl->ui.starlessImageSelector->setCurrentText(starless_name);
                guard->validateParameters();
                guard->settingsUpdated();
            });
        return;
    }

    const auto* descriptor = an::ui::starless_generators::findStarlessGeneratorProcess(generator_it->process_id);
    if (!descriptor)
    {
        notifyErrorMessageAsync(QStringLiteral("Star Reduction"),
                                QStringLiteral("The selected starless generator is no longer registered."));
        return;
    }

    std::string error;
    bool launched = false;
    if (descriptor->launch_single_image_transient_checked)
    {
        launched = descriptor->launch_single_image_transient_checked(m_image_view_control, &error);
    }
    else if (descriptor->launch_single_image_checked)
    {
        launched = descriptor->launch_single_image_checked(m_image_view_control, &error);
    }
    else if (descriptor->launch_single_image)
    {
        descriptor->launch_single_image(m_image_view_control);
        launched = true;
    }

    if (!launched)
    {
        m_impl->pendingGeneratedStarlessSelection = false;
        notifyErrorMessageAsync(
            QStringLiteral("Star Reduction"),
            QString::fromUtf8(error.empty() ? "Could not launch the selected starless generator." : error.c_str()));
        return;
    }

    m_impl->pendingGeneratedStarlessSelection = true;
}

void StarReductionProcess::on_initializeSmallStarProtection_clicked()
{
    if (m_ExecutionInProgress || m_impl->buildingProtectionMask)
    {
        return;
    }

    const StarReductionProcessParams params = getParams();
    const an::consumer::EngineSession* session = currentSession();
    const std::shared_ptr<const an::consumer::ImageAsset> target =
        m_image_view_control != nullptr && m_image_view_control->hasImage() ? m_image_view_control->document().asset()
                                                                            : nullptr;
    if (!params.starless || !target || session == nullptr)
    {
        notifyErrorMessageAsync(
            QStringLiteral("Star Reduction"),
            QStringLiteral("Select a compatible starless image before creating a protection mask."));
        return;
    }

    const QString maskName = defaultReducedName(m_image_view_control) + QStringLiteral(" mask");
    const std::string history =
        fmt::format("Star Reduction: Small-star mask created from '{}'", m_image_view_control->getUniqueName());
    m_impl->buildingProtectionMask = true;
    m_impl->ui.initializeSmallStarProtection->setEnabled(false);

    // The mask is built on a worker under an operation of its own and lands on the GUI thread, where it becomes the
    // target's mask while the window is still open and is let go otherwise.
    QPointer<StarReductionProcess> guard(this);
    const std::shared_ptr<const an::consumer::ImageAsset> starless = params.starless;
    // The window's stop reaches the leases and the operation: at the application's exit the mask ends at its next stop
    // point rather than being waited for.
    const std::stop_token stop = windowStopToken();
    QThreadPool::globalInstance()->start([session, target, starless, maskName, history, guard, stop]() {
        auto build = [&]() -> an::Result<an::consumer::ImageAsset> {
            // Both images are leased before the mask asks to be admitted, one lease each and in one order: an edit of
            // either view holds its writer while it waits to be admitted.
            an::ui::ImageReads reads;
            reads.name(target);
            reads.name(starless);
            if (const an::Status taken = reads.take(stop); taken.failed())
            {
                return taken;
            }
            an::Result<an::consumer::AssetReadLease> targetLease = reads.find(target->identity())->share();
            an::Result<an::consumer::AssetReadLease> starlessLease = reads.find(starless->identity())->share();
            an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
                *session, an::ui::ExecutionRequest{.stopToken = stop}, an::ui::ExecutionObserver{});
            if (!scope.hasValue())
            {
                return scope.status();
            }
            auto finished = [&scope](an::Status status) {
                scope->finish(status);
                return status;
            };
            const an::ui::ProcessSource targetSource = an::ui::ProcessSource::of(*targetLease);
            an::Result<process_utils::NormalizedCopy> targetCopy =
                process_utils::normalizedCopyOf(*scope, targetSource);
            an::Result<process_utils::NormalizedCopy> starlessCopy =
                process_utils::normalizedCopyOf(*scope, an::ui::ProcessSource::of(*starlessLease));
            if (!targetCopy.hasValue() || !starlessCopy.hasValue())
            {
                return finished(!targetCopy.hasValue() ? targetCopy.status() : starlessCopy.status());
            }
            const std::optional<an::ImageDescriptor> targetDescribed = targetSource.descriptor();
            const an::Result<an::ImageDescriptor> planned =
                reductionDescriptor(*targetDescribed, targetCopy->samples.sampleType(), an::StarReductionRequest{});
            if (!planned.hasValue())
            {
                return finished(planned.status());
            }
            std::optional<WorkingImage> holder;
            an::Result<std::pair<void*, an::ImageLayout>> starlessWorking =
                asWorking(*scope, *starlessCopy, *planned, holder);
            if (!starlessWorking.hasValue())
            {
                return finished(starlessWorking.status());
            }
            for (const auto& [samples, layout] :
                 {std::pair<void*, an::ImageLayout>{targetCopy->samples.samples(), targetCopy->samples.layout()},
                  *starlessWorking})
            {
                if (an::Status held = process_utils::holdToUnitInterval(scope->engine(), scope->execution(),
                                                                        planned->sampleType(), samples, layout);
                    held.failed())
                {
                    return finished(held);
                }
            }
            const an::Result<an::StarReductionPlan> plan = an::planStarReduction(*planned, *planned);
            if (!plan.hasValue())
            {
                return finished(plan.status());
            }
            an::Result<an::ImageMetadata> metadata = process_utils::derivedImageMetadata(
                targetSource.metadata(), plan->maskDescriptor(), "small-star-protection-mask");
            if (!metadata.hasValue())
            {
                return finished(metadata.status());
            }
            an::Result<an::consumer::ImageAsset> mask =
                process_utils::createDerivedImage(*scope, plan->maskDescriptor(), std::move(*metadata));
            if (!mask.hasValue())
            {
                return finished(mask.status());
            }
            {
                an::Result<an::consumer::AssetWriteLease> lease = mask->write();
                if (!lease.hasValue())
                {
                    return finished(lease.status());
                }
                const an::Status built = lease->visitWrite([&](auto&& access) -> an::Status {
                    const an::ImageLayout maskLayout = access.view().layout();
                    const an::Result<an::StarReductionResult> result = an::buildSmallStarProtectionMask(
                        scope->execution(), targetCopy->samples.samples(), targetCopy->samples.layout(),
                        starlessWorking->first, starlessWorking->second, access.view().data(), maskLayout, *plan);
                    return result.hasValue() ? an::Status::success() : result.status();
                });
                if (built.failed())
                {
                    return finished(built);
                }
            }
            scope->finish(an::Status::success());
            return std::move(*mask);
        };
        auto mask = std::make_shared<an::Result<an::consumer::ImageAsset>>(build());
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(
            application,
            [guard, mask, maskName, history]() {
                if (guard)
                {
                    guard->landSmallStarProtectionMask(std::move(*mask), maskName, history);
                }
            },
            Qt::QueuedConnection);
    });
}

void StarReductionProcess::landSmallStarProtectionMask(an::Result<an::consumer::ImageAsset>&& mask,
                                                       const QString& maskName, const std::string& history)
{
    m_impl->buildingProtectionMask = false;
    validateParameters();
    if (!mask.hasValue())
    {
        notifyErrorMessageAsync(QStringLiteral("Star Reduction"), QString::fromUtf8(mask.status().message()));
        return;
    }
    an::Result<an::ui::ImageDocument> document =
        an::ui::ImageDocument::create(std::move(*mask), an::ui::DocumentNames{.displayName = maskName});
    an::ui::IViewFactory* factory = process_utils::currentViewFactory();
    if (!document.hasValue() || factory == nullptr || m_image_view_control == nullptr)
    {
        return;
    }
    ImageViewControl* maskView = factory->createNewImageViewControlFromImage(
        nullptr, std::move(*document), nullptr, history, ViewControlMode::standard, nullptr);
    if (maskView == nullptr)
    {
        return;
    }
    m_impl->generatedSmallMaskName = QString::fromUtf8(maskView->getUniqueName());
    m_image_view_control->setCurrentMask(maskView);
    m_image_view_control->setEnableMask(true);
    m_image_view_control->setInvertMask(false);
    m_image_view_control->setShowMask(false);
    m_impl->ui.enableSmallStarProtection->setChecked(true);
}

void StarReductionProcess::on_enableSmallStarProtection_clicked(const Qt::CheckState state)
{
    const bool enabled = state == Qt::Checked;
    ImageViewControl* current_mask = m_image_view_control->getCurrentMask();
    if (!current_mask && !m_impl->generatedSmallMaskName.isEmpty())
    {
        current_mask = process_utils::findViewControlByName(m_impl->generatedSmallMaskName);
        if (current_mask)
        {
            m_image_view_control->setCurrentMask(current_mask);
        }
    }

    if (current_mask && (m_impl->generatedSmallMaskName.isEmpty() ||
                         QString::fromUtf8(current_mask->getUniqueName()) == m_impl->generatedSmallMaskName))
    {
        m_image_view_control->setEnableMask(enabled);
    }
    settingsUpdated();
}

bool StarReductionProcess::isStarlessViewSupported(const ImageViewControl* ctrl) const
{
    if (!ctrl || ctrl == m_image_view_control || !ctrl->hasImage() || !m_image_view_control ||
        !m_image_view_control->hasImage())
    {
        return false;
    }

    const std::optional<an::ImageDescriptor> target = m_image_view_control->imageDescriptor();
    const std::optional<an::ImageDescriptor> candidate = ctrl->imageDescriptor();
    return target.has_value() && candidate.has_value() && target->extent().width() == candidate->extent().width() &&
           target->extent().height() == candidate->extent().height() &&
           target->extent().channelCount() == candidate->extent().channelCount() &&
           target->channelSchema() == candidate->channelSchema();
}

void StarReductionProcess::loadStarlessImageList()
{
    m_impl->ignoreStarlessSelector = true;
    m_impl->ui.starlessImageSelector->clear();
    m_impl->ui.starlessImageSelector->addItem(NO_IMAGE_STR);

    for (auto* vControl : process_utils::listViewControls())
    {
        if (!isStarlessViewSupported(vControl))
        {
            continue;
        }

        m_impl->ui.starlessImageSelector->addItem(QString::fromUtf8(vControl->getUniqueName()));
        const int idx = m_impl->ui.starlessImageSelector->count() - 1;
        m_impl->ui.starlessImageSelector->setItemData(idx, an::ui::buildImageSelectorTooltip(vControl),
                                                      Qt::ToolTipRole);
    }
    m_impl->ignoreStarlessSelector = false;
}

void StarReductionProcess::loadStarlessGenerators()
{
    const QString selected_id = m_impl->ui.starlessGeneratorSelector->currentData(Qt::UserRole).toString();
    const QSignalBlocker blocker(m_impl->ui.starlessGeneratorSelector);
    m_impl->ui.starlessGeneratorSelector->clear();
    m_impl->starlessGenerators = an::ui::starless_generators::listStarlessGenerators();

    int selected_index = -1;
    int first_available_index = -1;
    for (const auto& generator : m_impl->starlessGenerators)
    {
        const int index = m_impl->ui.starlessGeneratorSelector->count();
        m_impl->ui.starlessGeneratorSelector->addItem(QString::fromUtf8(generator.display_name));
        m_impl->ui.starlessGeneratorSelector->setItemData(index, QString::fromUtf8(generator.process_id), Qt::UserRole);
        QString tooltip = tr("Open %1 to configure and generate a starless helper view for this Star Reduction run.")
                              .arg(QString::fromUtf8(generator.display_name));
        if (!generator.available)
        {
            tooltip = QString::fromUtf8(generator.unavailable_reason);
        }
        m_impl->ui.starlessGeneratorSelector->setItemData(index, tooltip, Qt::ToolTipRole);

        if (QString::fromUtf8(generator.process_id) == selected_id)
        {
            selected_index = index;
        }
        if (generator.available && first_available_index < 0)
        {
            first_available_index = index;
        }
    }

    if (selected_index >= 0)
    {
        m_impl->ui.starlessGeneratorSelector->setCurrentIndex(selected_index);
    }
    else if (first_available_index >= 0)
    {
        m_impl->ui.starlessGeneratorSelector->setCurrentIndex(first_available_index);
    }
    else if (m_impl->ui.starlessGeneratorSelector->count() > 0)
    {
        m_impl->ui.starlessGeneratorSelector->setCurrentIndex(0);
    }

    updateStarlessGeneratorControlState();
}

void StarReductionProcess::updateStarlessGeneratorControlState()
{
    const QString process_id = m_impl->ui.starlessGeneratorSelector->currentData(Qt::UserRole).toString();
    const auto generator_it = std::find_if(
        m_impl->starlessGenerators.begin(), m_impl->starlessGenerators.end(),
        [&process_id](const auto& generator) { return QString::fromUtf8(generator.process_id) == process_id; });

    const bool has_generator = generator_it != m_impl->starlessGenerators.end();
    const bool generator_available = has_generator && generator_it->available;
    m_impl->ui.starlessGeneratorSelector->setEnabled(has_generator);
    m_impl->ui.generateStarlessView->setEnabled(generator_available);

    if (!has_generator)
    {
        m_impl->ui.generateStarlessView->setToolTip(
            tr("No starless generator process is registered. Configure StarNet2 or install an extension that provides "
               "a starless generator."));
        return;
    }

    if (!generator_available)
    {
        m_impl->ui.generateStarlessView->setToolTip(QString::fromUtf8(generator_it->unavailable_reason));
        return;
    }

    m_impl->ui.generateStarlessView->setToolTip(
        tr("Open %1 so you can configure the starless generation settings. After it finishes, Star Reduction will "
           "select the generated starless view automatically.")
            .arg(QString::fromUtf8(generator_it->display_name)));
}

void StarReductionProcess::updateParameterControlState()
{
    const QString method = m_impl->ui.method->currentText();
    const bool iterative_method = method == kStarMethod || method == kLightnessMethod;
    m_impl->ui.stretchFactor->setEnabled(!iterative_method);
    m_impl->ui.mode->setEnabled(iterative_method);
    m_impl->ui.iterations->setEnabled(iterative_method);
}

void StarReductionProcess::updateOutputControlState()
{
    if (m_impl->ui.reduceTargetImage->isChecked() && m_impl->ui.createNewImage->isChecked())
    {
        const QSignalBlocker blocker(m_impl->ui.createNewImage);
        m_impl->ui.createNewImage->setChecked(false);
    }

    if (!m_impl->ui.reduceTargetImage->isChecked() && !m_impl->ui.createNewImage->isChecked())
    {
        const QSignalBlocker blocker(m_impl->ui.createNewImage);
        m_impl->ui.createNewImage->setChecked(true);
    }

    m_impl->ui.newImageId->setEnabled(m_impl->ui.createNewImage->isChecked());
    m_impl->ui.enableReverseStretch->setEnabled(m_impl->ui.createStarsImage->isChecked());
    m_impl->ui.starImageId->setEnabled(m_impl->ui.createStarsImage->isChecked());
}

void StarReductionProcess::validateParameters()
{
    // Nothing is reduced without a starless image: neither the run nor the protection mask is offered before one is
    // chosen.
    const bool has_starless = m_impl->ui.starlessImageSelector->currentText() != NO_IMAGE_STR;
    m_impl->ui.initializeSmallStarProtection->setEnabled(has_starless && !m_impl->buildingProtectionMask);
    if (auto* ok = m_impl->ui.buttonBox->button(QDialogButtonBox::Ok))
    {
        ok->setEnabled(has_starless && !m_ExecutionInProgress);
    }
}
