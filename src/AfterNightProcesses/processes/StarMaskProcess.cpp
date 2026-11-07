// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "StarMaskProcess.h"
#include "controls/ProcessButtonIcons.h"
#include "controls/ImageViewControl.h"
#include "processes/MaskProcessUtils.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/ProcessCommonUtils.h"

#include "theme/AnDesignTokens.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ImageTransfer.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>

#include <optional>
#include <utility>

namespace
{
constexpr int kStarMaskMinimumHeight = 860;

constexpr int kProfileGaussianIndex = 0;
constexpr int kProfileMoffatIndex = 1;

/// The most stars one mask paints.
constexpr std::uint32_t kMaximumMaskStars = 20000u;

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

} // namespace

namespace an::ui::processes::star_mask_detail
{

an::StarMaskShape shapeFromIndex(const int index) noexcept
{
    switch (index)
    {
    case 0:
        return an::StarMaskShape::Bell;
    case 1:
        return an::StarMaskShape::TruncatedBell;
    case 2:
        return an::StarMaskShape::Linear;
    case 3:
        return an::StarMaskShape::TruncatedLinear;
    case 4:
        return an::StarMaskShape::Cubic;
    case 5:
        return an::StarMaskShape::Quadratic;
    default:
        return an::StarMaskShape::TruncatedBell;
    }
}

an::Result<an::ImageDescriptor> detectionDescriptor(const an::ImageDescriptor& described, const an::SampleType working)
{
    const an::Result<an::ImageDescriptor> restated = an::describeSamplesAs(described, working);
    if (!restated.hasValue())
    {
        return restated.status();
    }
    an::ImageDescriptorRequest request = restated->toRequest();
    request.numericDomain = an::NumericDomain::NormalizedLinear;
    if (const std::optional<an::ImageDescriptor> linear = an::ImageDescriptor::create(request); linear.has_value())
    {
        return *linear;
    }
    // A colour space that has no linear form is not one a search for stars reads.
    request.colorSpace = an::ColorSpace::Unspecified;
    if (const std::optional<an::ImageDescriptor> linear = an::ImageDescriptor::create(request); linear.has_value())
    {
        return *linear;
    }
    return refusal(an::ErrorCode::InvalidArgument, "the image cannot be described for a star search");
}

} // namespace an::ui::processes::star_mask_detail

namespace detail = an::ui::processes::star_mask_detail;

StarMaskProcess::StarMaskProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<StarMaskParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildStarMaskProcessUi(this);
    setProcessDescription(QStringLiteral("Create a tunable stellar mask from detection, shape, and growth controls."));

    buildDetectionSettingsUi();

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.verticalLayout_2, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, nullptr, false, true);

    QObject::connect(m_ui.starShapeSelector, &QComboBox::currentTextChanged, this, [this]() { settingsUpdated(); });
    QObject::connect(m_convergenceValue, &QSpinBox::valueChanged, this, [this]() { settingsUpdated(); });
    QObject::connect(m_relaxChecks, &QCheckBox::checkStateChanged, this, [this]() { settingsUpdated(); });
    QObject::connect(m_useLightness, &QCheckBox::checkStateChanged, this, [this]() { settingsUpdated(); });
    QObject::connect(m_profileTypeSelector, &QComboBox::currentTextChanged, this, [this]() {
        updateDetectionSettingsState();
        settingsUpdated();
    });
    QObject::connect(m_minimumBetaValue, &QDoubleSpinBox::valueChanged, this, [this]() { settingsUpdated(); });
    QObject::connect(m_amplitudeRangeGroup, &QGroupBox::toggled, this, [this]() { settingsUpdated(); });
    QObject::connect(m_roundnessRangeGroup, &QGroupBox::toggled, this, [this]() { settingsUpdated(); });
    QObject::connect(m_shapedAreasGroup, &QGroupBox::toggled, this, [this]() { settingsUpdated(); });

    auto updated = [this]() { settingsUpdated(); };
    INIT_D_SPINBOX_CTRL(percentIncreaseControl, m_ui.percentIncreaseValue, m_ui.percentIncreaseSlider, updated);
    INIT_SPINBOX_CTRL(pixelIncreaseControl, m_ui.pixelIncreaseValue, m_ui.pixelIncreaseSlider, updated);
    INIT_SPINBOX_CTRL(minSizeControl, m_ui.minSizeValue, m_ui.minSizeSlider, updated);
    INIT_SPINBOX_CTRL(maxSizeControl, m_ui.maxSizeValue, m_ui.maxSizeSlider, updated);

    updateWindowTitle();

    m_ui.starShapeSelector->addItem("Bell");
    m_ui.starShapeSelector->addItem("Truncated Bell");
    m_ui.starShapeSelector->addItem("Linear");
    m_ui.starShapeSelector->addItem("Truncated Linear");
    m_ui.starShapeSelector->addItem("Cubic");
    m_ui.starShapeSelector->addItem("Quadratic");

    m_profileTypeSelector->addItem("Gaussian");
    m_profileTypeSelector->addItem("Moffat");

    m_ui.starShapeSelector->setCurrentText("Truncated Bell");
    m_profileTypeSelector->setCurrentIndex(kProfileGaussianIndex);

    m_SupportMasking = false;

    setExecutionAutostretchSupported(true);
    setupPreviewControl(true, QSize(600, 600), "Preview: Star Mask");
    resetSettings();

    refreshTheme();
}

std::string StarMaskProcess::getProcessNameStatic()
{
    return "Star Mask";
}

bool StarMaskProcess::requiresExclusiveAccessStatic()
{
    return false;
}

void StarMaskProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

StarMaskProcess::~StarMaskProcess() = default;

void StarMaskProcess::buildDetectionSettingsUi()
{
    const auto& t = an::ui::tokens();

    m_detectionGroup = new QGroupBox(QStringLiteral("Detection"), this);
    m_detectionGroup->setObjectName(QStringLiteral("detection"));
    auto* detectionLayout = new QVBoxLayout(m_detectionGroup);
    detectionLayout->setSpacing(t.spacing.md);
    detectionLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(t.spacing.sm);
    grid->setVerticalSpacing(t.spacing.sm);

    // The engine searches a box of at most thirty-two pixels either side of a star.
    auto* radiusLabel = new QLabel(QStringLiteral("Radius"), m_detectionGroup);
    auto* radiusValue = new QSpinBox(m_detectionGroup);
    auto* radiusSlider = new QSlider(Qt::Horizontal, m_detectionGroup);
    radiusValue->setObjectName(QStringLiteral("radiusValue"));
    radiusValue->setMinimum(3);
    radiusValue->setMaximum(32);
    radiusSlider->setMinimum(3);
    radiusSlider->setMaximum(32);
    radiusValue->setMinimumWidth(80);
    radiusValue->setToolTip(QStringLiteral("Half the width of the box a star has to be the brightest point of."));

    auto* thresholdLabel = new QLabel(QStringLiteral("Threshold"), m_detectionGroup);
    auto* thresholdValue = new QDoubleSpinBox(m_detectionGroup);
    auto* thresholdSlider = new QSlider(Qt::Horizontal, m_detectionGroup);
    thresholdValue->setObjectName(QStringLiteral("thresholdValue"));
    thresholdValue->setMinimum(0.01);
    thresholdValue->setMaximum(15.0);
    thresholdValue->setSingleStep(0.05);
    thresholdValue->setMinimumWidth(80);
    thresholdSlider->setMinimum(10);
    thresholdSlider->setMaximum(15000);
    thresholdValue->setToolTip(QStringLiteral("How many noise deviations a star stands above the sky."));

    grid->addWidget(radiusLabel, 0, 0, 1, 1);
    grid->addWidget(radiusValue, 0, 1, 1, 1);
    grid->addWidget(radiusSlider, 0, 2, 1, 1);
    grid->addWidget(thresholdLabel, 1, 0, 1, 1);
    grid->addWidget(thresholdValue, 1, 1, 1, 1);
    grid->addWidget(thresholdSlider, 1, 2, 1, 1);
    grid->setColumnStretch(0, 2);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 7);
    detectionLayout->addLayout(grid);

    m_amplitudeRangeGroup = new QGroupBox(QStringLiteral("Amplitude Range"), m_detectionGroup);
    m_amplitudeRangeGroup->setObjectName(QStringLiteral("amplitudeRange"));
    m_amplitudeRangeGroup->setCheckable(true);
    m_amplitudeRangeGroup->setChecked(false);
    auto* amplitudeLayout = new QGridLayout(m_amplitudeRangeGroup);
    amplitudeLayout->setContentsMargins(t.spacing.md, t.spacing.sm, t.spacing.md, t.spacing.sm);
    amplitudeLayout->setHorizontalSpacing(t.spacing.sm);
    amplitudeLayout->setVerticalSpacing(t.spacing.sm);

    auto* amplitudeMinimumLabel = new QLabel(QStringLiteral("Minimum"), m_amplitudeRangeGroup);
    auto* amplitudeMinimumValue = new QDoubleSpinBox(m_amplitudeRangeGroup);
    auto* amplitudeMinimumSlider = new QSlider(Qt::Horizontal, m_amplitudeRangeGroup);
    amplitudeMinimumValue->setObjectName(QStringLiteral("amplitudeMinimumValue"));
    amplitudeMinimumValue->setMinimum(0.0);
    amplitudeMinimumValue->setMaximum(0.99);
    amplitudeMinimumValue->setSingleStep(0.05);
    amplitudeMinimumValue->setMinimumWidth(80);
    amplitudeMinimumSlider->setMinimum(0);
    amplitudeMinimumSlider->setMaximum(990);

    auto* amplitudeMaximumLabel = new QLabel(QStringLiteral("Maximum"), m_amplitudeRangeGroup);
    auto* amplitudeMaximumValue = new QDoubleSpinBox(m_amplitudeRangeGroup);
    auto* amplitudeMaximumSlider = new QSlider(Qt::Horizontal, m_amplitudeRangeGroup);
    amplitudeMaximumValue->setObjectName(QStringLiteral("amplitudeMaximumValue"));
    amplitudeMaximumValue->setMinimum(0.01);
    amplitudeMaximumValue->setMaximum(1.0);
    amplitudeMaximumValue->setSingleStep(0.05);
    amplitudeMaximumValue->setMinimumWidth(80);
    amplitudeMaximumSlider->setMinimum(10);
    amplitudeMaximumSlider->setMaximum(1000);

    amplitudeLayout->addWidget(amplitudeMinimumLabel, 0, 0, 1, 1);
    amplitudeLayout->addWidget(amplitudeMinimumValue, 0, 1, 1, 1);
    amplitudeLayout->addWidget(amplitudeMinimumSlider, 0, 2, 1, 1);
    amplitudeLayout->addWidget(amplitudeMaximumLabel, 1, 0, 1, 1);
    amplitudeLayout->addWidget(amplitudeMaximumValue, 1, 1, 1, 1);
    amplitudeLayout->addWidget(amplitudeMaximumSlider, 1, 2, 1, 1);
    amplitudeLayout->setColumnStretch(1, 1);
    amplitudeLayout->setColumnStretch(2, 8);
    detectionLayout->addWidget(m_amplitudeRangeGroup);

    // A star has some roundness: the engine refuses a threshold of none.
    auto* roundnessLayout = new QHBoxLayout();
    roundnessLayout->setSpacing(t.spacing.sm);
    auto* roundnessLabel = new QLabel(QStringLiteral("Roundness Threshold"), m_detectionGroup);
    auto* roundnessValue = new QDoubleSpinBox(m_detectionGroup);
    auto* roundnessSlider = new QSlider(Qt::Horizontal, m_detectionGroup);
    roundnessValue->setObjectName(QStringLiteral("roundnessValue"));
    roundnessValue->setMinimum(0.01);
    roundnessValue->setMaximum(0.95);
    roundnessValue->setSingleStep(0.05);
    roundnessValue->setMinimumWidth(80);
    roundnessSlider->setMinimum(10);
    roundnessSlider->setMaximum(950);
    roundnessLayout->addWidget(roundnessLabel);
    roundnessLayout->addWidget(roundnessValue);
    roundnessLayout->addWidget(roundnessSlider);
    roundnessLayout->setStretch(1, 1);
    roundnessLayout->setStretch(2, 8);
    detectionLayout->addLayout(roundnessLayout);

    m_roundnessRangeGroup = new QGroupBox(QStringLiteral("Roundness Range"), m_detectionGroup);
    m_roundnessRangeGroup->setObjectName(QStringLiteral("roundnessRange"));
    m_roundnessRangeGroup->setCheckable(true);
    m_roundnessRangeGroup->setChecked(false);
    auto* roundnessRangeLayout = new QHBoxLayout(m_roundnessRangeGroup);
    roundnessRangeLayout->setSpacing(t.spacing.sm);
    roundnessRangeLayout->setContentsMargins(t.spacing.md, t.spacing.sm, t.spacing.md, t.spacing.sm);

    auto* roundnessMaximumLabel = new QLabel(QStringLiteral("Maximum"), m_roundnessRangeGroup);
    auto* roundnessMaximumValue = new QDoubleSpinBox(m_roundnessRangeGroup);
    auto* roundnessMaximumSlider = new QSlider(Qt::Horizontal, m_roundnessRangeGroup);
    roundnessMaximumValue->setObjectName(QStringLiteral("roundnessMaximumValue"));
    roundnessMaximumValue->setMinimum(0.01);
    roundnessMaximumValue->setMaximum(1.0);
    roundnessMaximumValue->setSingleStep(0.05);
    roundnessMaximumValue->setMinimumWidth(80);
    roundnessMaximumSlider->setMinimum(10);
    roundnessMaximumSlider->setMaximum(1000);
    roundnessRangeLayout->addWidget(roundnessMaximumLabel);
    roundnessRangeLayout->addWidget(roundnessMaximumValue);
    roundnessRangeLayout->addWidget(roundnessMaximumSlider);
    roundnessRangeLayout->setStretch(1, 1);
    roundnessRangeLayout->setStretch(2, 8);
    detectionLayout->addWidget(m_roundnessRangeGroup);

    auto* convergenceLayout = new QHBoxLayout();
    convergenceLayout->setSpacing(t.spacing.sm);
    auto* convergenceLabel = new QLabel(QStringLiteral("Convergence"), m_detectionGroup);
    m_convergenceValue = new QSpinBox(m_detectionGroup);
    m_convergenceValue->setObjectName(QStringLiteral("convergenceValue"));
    m_convergenceValue->setMinimum(1);
    m_convergenceValue->setMaximum(3);
    m_convergenceValue->setMinimumWidth(80);
    m_relaxChecks = new QCheckBox(QStringLiteral("Relax PSF Checks"), m_detectionGroup);
    m_relaxChecks->setObjectName(QStringLiteral("relaxChecks"));
    convergenceLayout->addWidget(convergenceLabel);
    convergenceLayout->addWidget(m_convergenceValue);
    convergenceLayout->addStretch(1);
    convergenceLayout->addWidget(m_relaxChecks);
    convergenceLayout->addStretch(2);
    detectionLayout->addLayout(convergenceLayout);

    // A Moffat profile's exponent is positive and at most the engine's ceiling.
    auto* profileLayout = new QHBoxLayout();
    profileLayout->setSpacing(t.spacing.sm);
    auto* profileTypeLabel = new QLabel(QStringLiteral("Profile Type"), m_detectionGroup);
    m_profileTypeSelector = new QComboBox(m_detectionGroup);
    m_profileTypeSelector->setObjectName(QStringLiteral("profileTypeSelector"));
    auto* minimumBetaLabel = new QLabel(QStringLiteral("Minimum Beta"), m_detectionGroup);
    m_minimumBetaValue = new QDoubleSpinBox(m_detectionGroup);
    m_minimumBetaValue->setObjectName(QStringLiteral("minimumBetaValue"));
    m_minimumBetaValue->setMinimum(0.05);
    m_minimumBetaValue->setMaximum(an::kMoffatBetaCeiling);
    m_minimumBetaValue->setSingleStep(0.05);
    m_minimumBetaValue->setMinimumWidth(80);
    profileLayout->addWidget(profileTypeLabel);
    profileLayout->addWidget(m_profileTypeSelector);
    profileLayout->addWidget(minimumBetaLabel);
    profileLayout->addWidget(m_minimumBetaValue);
    profileLayout->addStretch(2);
    detectionLayout->addLayout(profileLayout);

    m_useLightness = new QCheckBox(QStringLiteral("Use Lightness"), m_detectionGroup);
    m_useLightness->setObjectName(QStringLiteral("useLightness"));
    detectionLayout->addWidget(m_useLightness);

    m_ui.verticalLayout->insertWidget(0, m_detectionGroup);

    m_shapedAreasGroup = new QGroupBox(QStringLiteral("Shaped Areas"), this);
    m_shapedAreasGroup->setObjectName(QStringLiteral("shapedAreas"));
    m_shapedAreasGroup->setCheckable(true);
    m_shapedAreasGroup->setChecked(true);
    auto* shapedLayout = new QVBoxLayout(m_shapedAreasGroup);
    shapedLayout->setSpacing(t.spacing.sm);
    shapedLayout->setContentsMargins(t.spacing.md, t.spacing.md, t.spacing.md, t.spacing.md);

    auto* shapedGrid = new QGridLayout();
    shapedGrid->setHorizontalSpacing(t.spacing.sm);
    shapedGrid->setVerticalSpacing(t.spacing.sm);

    m_ui.gridLayout->removeWidget(m_ui.label_5);
    m_ui.gridLayout->removeWidget(m_ui.percentIncreaseValue);
    m_ui.gridLayout->removeWidget(m_ui.percentIncreaseSlider);
    m_ui.gridLayout->removeWidget(m_ui.label_7);
    m_ui.gridLayout->removeWidget(m_ui.pixelIncreaseValue);
    m_ui.gridLayout->removeWidget(m_ui.pixelIncreaseSlider);
    m_ui.gridLayout->removeWidget(m_ui.label_6);
    m_ui.gridLayout->removeWidget(m_ui.minSizeValue);
    m_ui.gridLayout->removeWidget(m_ui.minSizeSlider);
    m_ui.gridLayout->removeWidget(m_ui.label_8);
    m_ui.gridLayout->removeWidget(m_ui.maxSizeValue);
    m_ui.gridLayout->removeWidget(m_ui.maxSizeSlider);

    shapedGrid->addWidget(m_ui.label_5, 0, 0, 1, 1);
    shapedGrid->addWidget(m_ui.percentIncreaseValue, 0, 1, 1, 1);
    shapedGrid->addWidget(m_ui.percentIncreaseSlider, 0, 2, 1, 1);
    shapedGrid->addWidget(m_ui.label_7, 1, 0, 1, 1);
    shapedGrid->addWidget(m_ui.pixelIncreaseValue, 1, 1, 1, 1);
    shapedGrid->addWidget(m_ui.pixelIncreaseSlider, 1, 2, 1, 1);
    shapedGrid->addWidget(m_ui.label_6, 2, 0, 1, 1);
    shapedGrid->addWidget(m_ui.minSizeValue, 2, 1, 1, 1);
    shapedGrid->addWidget(m_ui.minSizeSlider, 2, 2, 1, 1);
    shapedGrid->addWidget(m_ui.label_8, 3, 0, 1, 1);
    shapedGrid->addWidget(m_ui.maxSizeValue, 3, 1, 1, 1);
    shapedGrid->addWidget(m_ui.maxSizeSlider, 3, 2, 1, 1);
    shapedGrid->setColumnStretch(0, 2);
    shapedGrid->setColumnStretch(1, 1);
    shapedGrid->setColumnStretch(2, 7);
    shapedLayout->addLayout(shapedGrid);

    auto* shapeLayout = new QHBoxLayout();
    shapeLayout->setSpacing(t.spacing.sm);
    m_ui.horizontalLayout->removeWidget(m_ui.label_9);
    m_ui.horizontalLayout->removeWidget(m_ui.starShapeSelector);
    shapeLayout->addWidget(m_ui.label_9);
    shapeLayout->addWidget(m_ui.starShapeSelector, 1);
    shapedLayout->addLayout(shapeLayout);

    // The shaped areas hold both rows now, and a layout taken out of its parent belongs to nothing that deletes it.
    m_ui.verticalLayout->removeItem(m_ui.gridLayout);
    m_ui.verticalLayout->removeItem(m_ui.horizontalLayout);
    delete m_ui.gridLayout;
    m_ui.gridLayout = nullptr;
    delete m_ui.horizontalLayout;
    m_ui.horizontalLayout = nullptr;
    m_ui.verticalLayout->insertWidget(1, m_shapedAreasGroup);

    auto updated = [this]() { settingsUpdated(); };
    INIT_SPINBOX_CTRL(radiusControl, radiusValue, radiusSlider, updated);
    INIT_D_SPINBOX_CTRL(thresholdControl, thresholdValue, thresholdSlider, updated);
    INIT_D_SPINBOX_CTRL(amplitudeMinimumControl, amplitudeMinimumValue, amplitudeMinimumSlider, updated);
    INIT_D_SPINBOX_CTRL(amplitudeMaximumControl, amplitudeMaximumValue, amplitudeMaximumSlider, updated);
    INIT_D_SPINBOX_CTRL(roundnessControl, roundnessValue, roundnessSlider, updated);
    INIT_D_SPINBOX_CTRL(roundnessMaximumControl, roundnessMaximumValue, roundnessMaximumSlider, updated);
}

void StarMaskProcess::updateDetectionSettingsState()
{
    if (m_minimumBetaValue != nullptr && m_profileTypeSelector != nullptr)
    {
        m_minimumBetaValue->setEnabled(m_profileTypeSelector->isEnabled() &&
                                       m_profileTypeSelector->currentIndex() == kProfileMoffatIndex);
    }

    setMinimumHeight(kStarMaskMinimumHeight);
    if (height() < kStarMaskMinimumHeight)
    {
        resize(width(), kStarMaskMinimumHeight);
    }
}

an::Status StarMaskProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                    an::ui::ProcessDestination& destination,
                                                    const an::ui::ProcessRun& run, const StarMaskParams& params,
                                                    an::ui::ExecutionScope& scope)
{
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    const an::EngineContext& engine = scope.engine();
    const an::ExecutionContext& execution = scope.execution();
    // The stars are found in fractions of full scale: in a colour image's CIE lightness when asked, in its green
    // channel otherwise, and in a grey image's only channel.
    const bool colour = process_utils::describesColourImage(*described);
    const bool lightness = params.useLightness && colour;
    std::optional<an::consumer::ImageAsset> lightnessImage;
    std::optional<an::consumer::AssetReadLease> lightnessLease;
    an::Result<process_utils::NormalizedCopy> searched =
        refusal(an::ErrorCode::NotInitialized, "nothing was read for the search");
    if (lightness)
    {
        an::Result<an::consumer::ImageAsset> extracted = process_utils::extractLightness(scope, source);
        if (!extracted.hasValue())
        {
            return extracted.status();
        }
        lightnessImage.emplace(std::move(*extracted));
        an::Result<an::consumer::AssetReadLease> lease = lightnessImage->read();
        if (!lease.hasValue())
        {
            return lease.status();
        }
        lightnessLease.emplace(std::move(*lease));
        searched = process_utils::normalizedCopyOf(scope, an::ui::ProcessSource::of(*lightnessLease));
    }
    else
    {
        searched = process_utils::normalizedCopyOf(scope, source);
    }
    if (!searched.hasValue())
    {
        return searched.status();
    }
    const process_utils::NormalizedSamples& samples = searched->samples;
    const an::Result<an::ImageDescriptor> searchDescribed =
        lightness ? process_utils::derivedImageDescriptor(*described, 1u, samples.sampleType(), engine.limits())
                  : detail::detectionDescriptor(*described, samples.sampleType());
    if (!searchDescribed.hasValue())
    {
        return searchDescribed.status();
    }
    // The frame view borrows its layout, so the layout it is given outlives the search.
    const an::ImageLayout searchLayout = samples.layout();
    const an::KernelValidityView validity = process_utils::skippingNonFinite(samples.validity());
    const an::Result<an::StarFrameView> frame = an::StarFrameView::create(samples.samples(), searchLayout.totalBytes(),
                                                                          searchLayout, *searchDescribed, validity);
    if (!frame.hasValue())
    {
        return frame.status();
    }
    an::StarDetectionRequest request = params.detection;
    request.fullScale = 1.0;
    request.maximumStars = kMaximumMaskStars;
    const std::uint32_t channel = colour && !lightness ? 1u : 0u;
    const an::Result<an::StarDetectionPlan> plan =
        an::planStarDetection(request, *searchDescribed, searchLayout, channel);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::StarDetectionResult detected;
    if (an::Status found = an::detectStars(execution, *frame, *plan, detected); found.failed())
    {
        return found;
    }

    // The mask, a grey image of the target's representation, painted in fractions of full scale.
    const an::Result<an::ImageDescriptor> maskDescribed =
        process_utils::derivedImageDescriptor(*described, 1u, described->sampleType(), engine.limits());
    if (!maskDescribed.hasValue())
    {
        return maskDescribed.status();
    }
    an::Result<an::ImageMetadata> metadata =
        process_utils::derivedImageMetadata(source.metadata(), *maskDescribed, "star-mask");
    if (!metadata.hasValue())
    {
        return metadata.status();
    }
    an::Result<an::consumer::ImageAsset> mask =
        process_utils::createDerivedImage(scope, *maskDescribed, std::move(*metadata));
    if (!mask.hasValue())
    {
        return mask.status();
    }
    {
        an::Result<an::consumer::AssetWriteLease> lease = mask->write();
        if (!lease.hasValue())
        {
            return lease.status();
        }
        const an::Status painted = lease->visitWrite([&](auto&& access) -> an::Status {
            const an::ImageLayout layout = access.view().layout();
            an::Result<process_utils::NormalizedSamples> output = process_utils::NormalizedSamples::open(
                engine, execution, *maskDescribed, access.view().data(), layout, an::KernelValidityView{});
            if (!output.hasValue())
            {
                return output.status();
            }
            const an::Result<an::StarMaskResult> result =
                params.shapedAreas
                    ? an::paintShapedStarMask(execution, detected.stars, params.shape, output->sampleType(),
                                              output->samples(), output->layout())
                    : an::paintEllipticalStarMask(execution, detected.stars, an::StarMaskEllipseRequest{},
                                                  output->sampleType(), output->samples(), output->layout());
            if (!result.hasValue())
            {
                return result.status();
            }
            return output->commit(execution);
        });
        if (painted.failed())
        {
            return painted;
        }
    }
    if (an::Status replaced = destination.replace(std::move(*mask)); replaced.failed())
    {
        return replaced;
    }
    if (run.isForPreview())
    {
        return an::Status::success();
    }
    return process_utils::recordNonlinearResult(
        destination,
        process_utils::NonlinearResultRecord{.stateOperation = "star-mask",
                                             .engineOperation = params.shapedAreas ? "an::paintShapedStarMask"
                                                                                   : "an::paintEllipticalStarMask",
                                             .detail = describeTypedProcessStep(params).description,
                                             .restatedValidity = an::ValidityDescriptor::allValid()});
}

an::ui::ProcessStep StarMaskProcess::describeTypedProcessStep(const StarMaskParams& params) const
{
    const an::StarDetectionRequest& detection = params.detection;
    return an::ui::ProcessStep{
        .name = getProcessName(),
        .description = fmt::format(
            "Created in {} mode. Radius: {}, Threshold: {:.2f}, Profile: {}{}. Percent Increase: {:.2f}, Pixel "
            "Increase: {}, Min Size: {}, Max Size: {}, Star Shape: {}",
            params.shapedAreas ? "shaped areas" : "binary areas", detection.searchRadius, detection.noiseSigma,
            detection.model == an::PsfModel::MoffatFreeBeta ? "Moffat" : "Gaussian",
            params.useLightness ? ", on the lightness" : "", params.shape.sizeScale, params.shape.sizeIncrease,
            params.shape.minimumSize, params.shape.maximumSize, params.shapeName)};
}

bool StarMaskProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void StarMaskProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void StarMaskProcess::resetSettings()
{
    m_shapedAreasGroup->setChecked(true);

    percentIncreaseControl->setValue(0.5);
    pixelIncreaseControl->setValue(0);
    minSizeControl->setValue(2);
    maxSizeControl->setValue(25);

    radiusControl->setValue(5);
    thresholdControl->setValue(1.0);
    amplitudeMinimumControl->setValue(0.0);
    amplitudeMaximumControl->setValue(1.0);
    m_amplitudeRangeGroup->setChecked(false);

    roundnessControl->setValue(0.5);
    roundnessMaximumControl->setValue(1.0);
    m_roundnessRangeGroup->setChecked(false);

    m_convergenceValue->setValue(1);
    m_relaxChecks->setChecked(false);
    m_profileTypeSelector->setCurrentIndex(kProfileGaussianIndex);
    m_minimumBetaValue->setValue(1.5);
    m_useLightness->setChecked(false);

    m_ui.starShapeSelector->setCurrentText("Truncated Bell");

    updateDetectionSettingsState();

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

ProgressDialogControl* StarMaskProcess::getProgressDialogControl(const bool forSequence)
{
    auto* progress = new ProgressDialogControl(
        this, QString::fromUtf8(fmt::format("{} <{}>", getProcessName(), getParentViewControl()->getUniqueName())),
        forSequence);
    progress->setProgressBarRange(0, 0);
    return progress;
}

void StarMaskProcess::settingsUpdatedImpl()
{
    if (auto* applyButton = m_ui.buttonBox->button(QDialogButtonBox::Apply))
    {
        applyButton->setEnabled(true);
    }
}

void StarMaskProcess::enableProcessControlsImpl(bool enabled)
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    m_shapedAreasGroup->setEnabled(enabled);
    radiusControl->setEnabled(enabled);
    thresholdControl->setEnabled(enabled);
    amplitudeMinimumControl->setEnabled(enabled);
    amplitudeMaximumControl->setEnabled(enabled);
    roundnessControl->setEnabled(enabled);
    roundnessMaximumControl->setEnabled(enabled);
    m_convergenceValue->setEnabled(enabled);
    m_relaxChecks->setEnabled(enabled);
    m_profileTypeSelector->setEnabled(enabled);
    m_useLightness->setEnabled(enabled);
    m_amplitudeRangeGroup->setEnabled(enabled);
    m_roundnessRangeGroup->setEnabled(enabled);

    updateDetectionSettingsState();
}

void StarMaskProcess::getBatchExecutionParameters(bool& createNewImage, bool& closeAtEnd, bool& executeInBackground,
                                                  std::string& newImageName)
{
    createNewImage = true;
    closeAtEnd = true;
    executeInBackground = true;

    newImageName = m_image_view_control->getUniqueName() + " - STARMASK";
}

StarMaskParams StarMaskProcess::getParams() const
{
    StarMaskParams params;
    params.shapedAreas = m_shapedAreasGroup->isChecked();
    params.shape.shape = detail::shapeFromIndex(m_ui.starShapeSelector->currentIndex());
    params.shape.sizeScale = percentIncreaseControl->getValue();
    params.shape.sizeIncrease = static_cast<double>(pixelIncreaseControl->getValue());
    params.shape.minimumSize = static_cast<double>(minSizeControl->getValue());
    params.shape.maximumSize = static_cast<double>(maxSizeControl->getValue());
    params.shapeName = m_ui.starShapeSelector->currentText().toStdString();

    an::StarDetectionRequest& detection = params.detection;
    detection.searchRadius = static_cast<std::uint32_t>(radiusControl->getValue());
    detection.noiseSigma = thresholdControl->getValue();
    detection.minimumRoundness = roundnessControl->getValue();
    detection.maximumRoundness = m_roundnessRangeGroup->isChecked() ? roundnessMaximumControl->getValue() : 1.0;
    if (m_amplitudeRangeGroup->isChecked())
    {
        detection.minimumAmplitude = amplitudeMinimumControl->getValue();
        detection.maximumAmplitude = amplitudeMaximumControl->getValue();
    }
    detection.convergence = static_cast<std::uint32_t>(m_convergenceValue->value());
    detection.relaxedChecks = m_relaxChecks->isChecked();
    detection.model = m_profileTypeSelector->currentIndex() == kProfileMoffatIndex ? an::PsfModel::MoffatFreeBeta
                                                                                   : an::PsfModel::Gaussian;
    detection.minimumBeta = m_minimumBetaValue->value();
    params.useLightness = m_useLightness->isChecked();
    return params;
}
