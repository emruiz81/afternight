// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "SplitCFAProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/ProcessCommonUtils.h"
#include "controls/ProcessButtonIcons.h"

#include <fmt/format.h>

namespace
{

const QString kSplitChannels = QStringLiteral("Split CFA Channels");
const QString kExtractHa = QStringLiteral("Extract Ha");
const QString kExtractHaOiii = QStringLiteral("Extract Ha/OIII");
const QString kExtractGreen = QStringLiteral("Extract Green");

const QString kNoResampling = QStringLiteral("No Resampling");
const QString kUpsampleHa = QStringLiteral("Upsample Ha");
const QString kDownsampleOiii = QStringLiteral("Downsample OIII");

} // namespace

SplitCFAProcess::SplitCFAProcess(ImageViewControl* control) : ImageProcessControl(control, getProcessNameStatic())
{
    setWindowSizeFitMultipliers(1.3, 1.0);
    m_ui = an::ui::processes::buildSplitCFAProcessUi(this);
    setProcessDescription(
        QStringLiteral("Extract CFA channels or derived Ha/OIII/green components from RAW mosaic frames."));

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this, &SplitCFAProcess::on_dialog_box_button_clicked);
    QObject::connect(m_ui.methodSelector, &QComboBox::currentTextChanged, this,
                     &SplitCFAProcess::on_methodSelector_currentTextChanged);

    updateWindowTitle();

    m_SupportTargetViewControlSelection = true;
    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    initialize();
    refreshTheme();
}

std::string SplitCFAProcess::getProcessNameStatic()
{
    return "Split CFA";
}

bool SplitCFAProcess::requiresExclusiveAccessStatic()
{
    return false;
}

SplitCFAProcess::Settings SplitCFAProcess::settings() const
{
    Settings asked;
    const QString method = m_ui.methodSelector->currentText();
    asked.method = method == kExtractHa       ? Method::HydrogenAlpha
                   : method == kExtractHaOiii ? Method::HydrogenAlphaAndOxygenIII
                   : method == kExtractGreen  ? Method::Green
                                              : Method::SplitChannels;
    const QString scaling = m_ui.haOIIIResamplingSelector->currentText();
    asked.scaling = scaling == kUpsampleHa       ? an::NarrowbandExtractionScaling::UpsampleHydrogenAlpha
                    : scaling == kDownsampleOiii ? an::NarrowbandExtractionScaling::DownsampleOxygenIII
                                                 : an::NarrowbandExtractionScaling::Native;
    return asked;
}

SplitCFAProcess::~SplitCFAProcess() {}

void SplitCFAProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();

    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

void SplitCFAProcess::on_methodSelector_currentTextChanged(const QString& text)
{
    m_ui.haOIIIResamplingSelector->setEnabled(text == kExtractHaOiii);
}

void SplitCFAProcess::initialize()
{
    m_ui.methodSelector->addItem(kSplitChannels);
    m_ui.methodSelector->addItem(kExtractHa);
    m_ui.methodSelector->addItem(kExtractHaOiii);
    m_ui.methodSelector->addItem(kExtractGreen);
    m_ui.methodSelector->setCurrentText(kSplitChannels);

    m_ui.haOIIIResamplingSelector->addItem(kNoResampling);
    m_ui.haOIIIResamplingSelector->addItem(kUpsampleHa);
    m_ui.haOIIIResamplingSelector->addItem(kDownsampleOiii);
    m_ui.haOIIIResamplingSelector->setCurrentText(kNoResampling);
}

void SplitCFAProcess::executeImageExtraction()
{
    if (m_ExecutionInProgress || !m_image_view_control->hasImage())
    {
        return;
    }
    const std::string name = m_image_view_control->getUniqueName();
    an::ui::processes::NewViewsRunRequest request;
    request.words = an::ui::processes::GeometryRunWords{
        .title = QStringLiteral("Split CFA"),
        .progressText = QString::fromStdString(fmt::format("Executing split CFA to sequence {}", name)),
        .errorTitle = QStringLiteral("Error running Split CFA"),
        .errorLead = "An error occurred when executing the Split CFA process"};
    const std::optional<an::ImageDescriptor> described = m_image_view_control->imageDescriptor();
    if (an::Status mosaic = an::ui::processes::checkBayerMosaic(described); mosaic.failed())
    {
        an::ui::processes::reportRunFailure(this, request.words, mosaic);
        return;
    }

    // The dialog is read here, on the GUI thread; the run reads only this.
    const Settings asked = settings();
    const auto output = [&request, &name](const std::string& suffix, const std::string& what) {
        request.imageOutputs.push_back(
            {.name = name + " - " + suffix,
             .history = fmt::format("Split CFA: {} extracted from RAW image '{}'", what, name)});
        request.sequenceOutputs.push_back(
            {.name = name + " - " + suffix,
             .history = fmt::format("Split CFA: {} extracted from RAW sequence '{}'", what, name)});
    };
    switch (asked.method)
    {
    case Method::SplitChannels: {
        const std::array<std::string, 4u> planes = an::ui::processes::mosaicPlaneNames(described->cfa());
        for (std::size_t plane = 0u; plane < planes.size(); ++plane)
        {
            output(planes[plane], "CFA" + std::to_string(plane));
        }
        request.operation = [](const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source) {
            return an::ui::processes::splitMosaicPlanes(scope, source);
        };
        break;
    }
    case Method::HydrogenAlpha:
        output("Ha", "Ha");
        request.operation =
            [](const an::ui::ExecutionScope& scope,
               const an::ui::ProcessSource& source) -> an::Result<std::vector<an::consumer::ImageAsset>> {
            an::Result<an::consumer::ImageAsset> made = an::ui::processes::extractHydrogenAlpha(scope, source);
            if (!made.hasValue())
            {
                return made.status();
            }
            std::vector<an::consumer::ImageAsset> one;
            one.push_back(std::move(*made));
            return one;
        };
        break;
    case Method::Green:
        output("Green", "Green");
        request.operation =
            [](const an::ui::ExecutionScope& scope,
               const an::ui::ProcessSource& source) -> an::Result<std::vector<an::consumer::ImageAsset>> {
            an::Result<an::consumer::ImageAsset> made = an::ui::processes::extractGreen(scope, source);
            if (!made.hasValue())
            {
                return made.status();
            }
            std::vector<an::consumer::ImageAsset> one;
            one.push_back(std::move(*made));
            return one;
        };
        break;
    case Method::HydrogenAlphaAndOxygenIII:
        output("Ha", "Ha");
        output("Oiii", "OIII");
        request.operation = [scaling = asked.scaling](const an::ui::ExecutionScope& scope,
                                                      const an::ui::ProcessSource& source) {
            return an::ui::processes::extractHydrogenAlphaAndOxygenIII(scope, source, scaling);
        };
        break;
    }
    // A sequence's frames are read as recorded, mosaics.
    request.readDemosaiced = false;
    request.showDemosaiced = false;
    request.inFlight = beginRunInFlight();
    request.admissionHook = admissionHook();

    m_ExecutionInProgress = true;
    // The run's finish reads the view the selector names, so the target is held until the run lands.
    enableProcessControls(false);
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(false);

    LOG_INFO("Executing process '{}' on {} '{}'... ", getProcessName(),
             m_image_view_control->isSequence() ? "sequence" : "image", name);

    QPointer<SplitCFAProcess> guard(this);
    an::ui::processes::runToNewViews(*m_image_view_control, std::move(request),
                                     [guard](const an::ui::processes::GeometryRunOutcome& outcome) {
                                         if (guard)
                                         {
                                             guard->finishRun(outcome);
                                         }
                                     });
}

void SplitCFAProcess::finishRun(const an::ui::processes::GeometryRunOutcome& outcome)
{
    m_ExecutionInProgress = false;
    if (outcome.status.ok() && outcome.published)
    {
        LOG_INFO("Process '{}' executed successfully on '{}'", getProcessName(),
                 m_image_view_control != nullptr ? m_image_view_control->getUniqueName() : std::string());
    }
    if ((outcome.status.ok() && outcome.published) || m_ProcessCloseRequested)
    {
        close();
        return;
    }
    enableProcessControls(true);
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(true);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(true);
}

bool SplitCFAProcess::isViewControlSupported(const ImageViewControl* ctrl)
{
    return ctrl->isCFA() && ctrl->isMono();
}

void SplitCFAProcess::on_dialog_box_button_clicked(QAbstractButton* button)
{
    QDialogButtonBox* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
        return;

    QDialogButtonBox::StandardButton sb = buttonBox->standardButton(button);

    if (sb == QDialogButtonBox::Ok)
    {
        executeImageExtraction();
    }
    else if (sb == QDialogButtonBox::Close)
    {
        this->close();
    }
}
