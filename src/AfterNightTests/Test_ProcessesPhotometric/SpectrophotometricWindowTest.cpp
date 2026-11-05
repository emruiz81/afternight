// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The spectrophotometric colour calibration window: the launch through its registry descriptor, the readiness it
/// decides on a worker and what it offers and refuses, the gains and the neutral sky an Apply writes over a field whose
/// spectra are planted, the mask, the history step, the result's metadata and cards, the engine request the dialog
/// states, its defaults and what Reset restores, every representation an image can hold, a stopped run, a fit the
/// field cannot support, a person's attestation of an image's state, and the ImageIntegration hand-off.

#include "Test_ProcessesPhotometric/PhotometricTestSupport.h"

#include "app/PlateSolveUtils.h"
#include "app/ProcessRegistry.h"
#include "controls/ImageViewControl.h"
#include "controls/settings/SettingsKeys.h"
#include "processes/PhotometricCalibrationSupport.h"
#include "processes/SpectrophotometricColorCalibration/SpccHandoffSubsetSpectrophotometricCatalog.h"
#include "processes/SpectrophotometricColorCalibration/SpccProcessReadinessPanel.h"
#include "processes/SpectrophotometricColorCalibration/SpccRunInputs.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::photometric;

constexpr double kRedCast = 1.3;
constexpr double kBlueCast = 0.75;

class SpectrophotometricWindowTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        s_field = std::make_unique<StarField>(makeSpectrophotometricField(kRedCast, kBlueCast));
    }

    static void TearDownTestSuite()
    {
        s_field.reset();
    }

    void SetUp() override
    {
        m_context = openProcessContext(4u);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
        m_catalogs.emplace(*m_context);
        ASSERT_TRUE(writeSpectrumCatalog(m_catalogs->spectrumDirectory(), field()));
        ASSERT_TRUE(writeResponseRepository(m_catalogs->responseDirectory()));
        m_catalogs->useSpectrumCatalogAndRepository();
    }

    void TearDown() override
    {
        m_catalogs.reset();
        m_installed.reset();
        m_context.reset();
    }

    [[nodiscard]] ProcessTestContext& context() const
    {
        return *m_context;
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

    [[nodiscard]] static const StarField& field()
    {
        return *s_field;
    }

    [[nodiscard]] InstalledCatalogs& catalogs()
    {
        return *m_catalogs;
    }

private:
    static std::unique_ptr<StarField> s_field;
    std::unique_ptr<ProcessTestContext> m_context;
    std::optional<InstalledContext> m_installed;
    std::optional<InstalledCatalogs> m_catalogs;
};

std::unique_ptr<StarField> SpectrophotometricWindowTest::s_field;

[[nodiscard]] QPushButton* applyButtonOf(QWidget& window)
{
    auto* box = window.findChild<QDialogButtonBox*>(QStringLiteral("buttonBox"));
    return box != nullptr ? box->button(QDialogButtonBox::Apply) : nullptr;
}

[[nodiscard]] const an::ui::processes::SpccProcessReadinessReport& reportOf(QWidget& window)
{
    auto* panel = dynamic_cast<an::ui::processes::SpccProcessReadinessPanel*>(
        window.findChild<QWidget*>(QStringLiteral("spccProcessReadinessPanel")));
    EXPECT_NE(panel, nullptr);
    static const an::ui::processes::SpccProcessReadinessReport empty;
    return panel != nullptr ? panel->report() : empty;
}

template <typename WidgetT> [[nodiscard]] WidgetT* widgetOf(QWidget& window, const char* name)
{
    auto* widget = window.findChild<WidgetT*>(QString::fromLatin1(name));
    EXPECT_NE(widget, nullptr) << "the window has no " << name;
    return widget;
}

/// A window whose run's stop is asked for as its body starts, so the engine sees a stopped execution.
class StoppingSpectrophotometric final : public SpectrophotometricColorCalibrationProcess
{
public:
    using SpectrophotometricColorCalibrationProcess::SpectrophotometricColorCalibrationProcess;

protected:
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run,
                                       const SpectrophotometricColorCalibrationParams& params,
                                       an::ui::ExecutionScope& scope) override
    {
        scope.requestStop();
        return SpectrophotometricColorCalibrationProcess::executeTypedProcessImpl(source, destination, run, params,
                                                                                  scope);
    }
};

/// A window that says what its dialog states and which history step an Apply of it records.
class DescribedSpectrophotometric final : public SpectrophotometricColorCalibrationProcess
{
public:
    using SpectrophotometricColorCalibrationProcess::SpectrophotometricColorCalibrationProcess;

    [[nodiscard]] SpectrophotometricColorCalibrationParams params() const
    {
        return getParams();
    }

    [[nodiscard]] an::ui::ProcessStep describeStep() const
    {
        return describeTypedProcessStep(getParams());
    }
};

/// Opens a window of a class over a target, without the delete-on-close, and waits for its readiness.
template <typename WindowT>
[[nodiscard]] WindowHandle<SpectrophotometricColorCalibrationProcess> openWindow(ImageViewControl& target)
{
    WindowHandle<SpectrophotometricColorCalibrationProcess> window(new WindowT(&target));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    EXPECT_TRUE(settleReadiness(*window));
    return window;
}

/// Issues an edit of a view that copies its image as it is, as any process's edit would replace it: the view's image
/// gets a new revision once the edit lands, and the history a step named "Copy".
void issueCopyEdit(ImageViewControl& view)
{
    view.editImage("Copy", "the image as it was",
                   [](const an::consumer::EngineSession& session, const an::consumer::AssetWriteLease& image,
                      std::stop_token stop) -> an::Result<an::consumer::ImageAsset> {
                       const an::ImageMetadata metadata = std::visit(
                           [](const auto& typed) -> an::ImageMetadata { return typed.metadata(); }, image.image());
                       return an::ui::copyImageWithMetadata(session, image, metadata, stop);
                   });
}

/// The red, green and blue of a planted star over the sky, in planar values.
[[nodiscard]] std::array<double, 3u> starOverSky(const StarField& field, const std::vector<double>& values,
                                                 const PlantedStar& star)
{
    const std::size_t plane = static_cast<std::size_t>(field.width) * field.height;
    const std::size_t at =
        static_cast<std::size_t>(std::floor(star.y)) * field.width + static_cast<std::size_t>(std::floor(star.x));
    std::array<double, 3u> over{};
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        over[channel] = values[channel * plane + at] - skyMedian(field, values, channel);
    }
    return over;
}

/// Whether what a document holds now is a field calibrated with these gains, star by star, within a fraction.
void expectCalibratedBy(const StarField& field, const std::vector<double>& before, const std::vector<double>& after,
                        const std::array<double, 3u>& gains, double tolerance)
{
    ASSERT_EQ(after.size(), before.size());
    for (const std::size_t index : {std::size_t{20}, std::size_t{51}, std::size_t{88}})
    {
        const std::array<double, 3u> was = starOverSky(field, before, field.stars[index]);
        const std::array<double, 3u> now = starOverSky(field, after, field.stars[index]);
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            EXPECT_NEAR(now[channel] / was[channel], gains[channel], tolerance * gains[channel])
                << "star " << index << ", channel " << channel;
        }
    }
}

} // namespace

// apply: the calibration finds the planted casts and the white reference's colour - the gains undo them, the largest
// one - and the sky of every channel lands on one shared level.
TEST_F(SpectrophotometricWindowTest, AnApplyUndoesThePlantedCastsAndNeutralizesTheSky)
{
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()));
    ASSERT_NE(target, nullptr);
    WindowHandle<SpectrophotometricColorCalibrationProcess> window = openSpectrophotometric(*target);
    const an::ui::processes::SpccProcessReadinessReport& report = reportOf(*window);
    ASSERT_TRUE(report.canExecute()) << report.summary.toStdString();
    EXPECT_EQ(report.responseProfileId.toStdString(), kSpccProfileId);
    QPushButton* apply = applyButtonOf(*window);
    ASSERT_NE(apply, nullptr);
    EXPECT_TRUE(apply->isEnabled());

    const std::vector<double> before = documentValues(target->document());
    ASSERT_TRUE(applySpectrophotometric(*window, *target));
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::vector<double> values = documentValues(target->document());
    ASSERT_EQ(values.size(), before.size());

    const std::array<double, 3u> gains = expectedSpectrophotometricGains(kRedCast, kBlueCast);
    const std::size_t plane = static_cast<std::size_t>(field().width) * field().height;
    const PlantedStar& star = field().stars[20];
    const std::size_t at =
        static_cast<std::size_t>(std::floor(star.y)) * field().width + static_cast<std::size_t>(std::floor(star.x));
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        const double skyBefore = skyMedian(field(), before, channel);
        const double skyAfter = skyMedian(field(), values, channel);
        const double peakBefore = before[channel * plane + at] - skyBefore;
        const double peakAfter = values[channel * plane + at] - skyAfter;
        EXPECT_NEAR(peakAfter / peakBefore, gains[channel], 0.02 * gains[channel]) << "channel " << channel;
    }
    EXPECT_NEAR(skyMedian(field(), values, 0u), skyMedian(field(), values, 1u), 0.002);
    EXPECT_NEAR(skyMedian(field(), values, 2u), skyMedian(field(), values, 1u), 0.002);
    auto* diagnostics = window->findChild<QPlainTextEdit*>(QStringLiteral("spccDiagnosticsText"));
    ASSERT_NE(diagnostics, nullptr);
    EXPECT_TRUE(diagnostics->toPlainText().contains(QStringLiteral("Accepted samples")))
        << diagnostics->toPlainText().toStdString();
}

// a run's shared inputs: every frame of one run reads the catalog's verification, its manifest, the response
// repository, the hand-off and a field's chunks once, and the next run - with whatever was installed since - reads
// them again.
TEST_F(SpectrophotometricWindowTest, ARunReadsWhatItsFramesShareOnceAndTheNextRunReadsItAgain)
{
    {
        an::ui::processes::SpccRunInputs inputs;
        an::Result<an::consumer::OperationScope> scope =
            session().admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
        ASSERT_TRUE(scope.hasValue()) << scope.status().message();
        const an::ExecutionContext& execution = scope->execution();
        const QString catalog = catalogs().spectrumDirectory();
        for (int frame = 0; frame < 3; ++frame)
        {
            EXPECT_TRUE(inputs.verifiedCatalog(catalog).checksumsValid);
            ASSERT_TRUE(inputs.manifest(execution, catalog).hasValue());
            ASSERT_TRUE(inputs.repository(execution, catalogs().responseDirectory()).hasValue());
            (void)inputs.handoff(QString());
        }
        const auto manifest = inputs.manifest(execution, catalog);
        ASSERT_TRUE(manifest.hasValue());
        ASSERT_TRUE(field().solution.has_value());
        const an::Result<an::SkyCoordinate> fieldCentre = field().solution->fieldCenter();
        ASSERT_TRUE(fieldCentre.hasValue());
        const an::SkyCoordinate centre = *fieldCentre;
        const auto first = inputs.region(execution, catalog, **manifest, centre, 0.5, {});
        ASSERT_TRUE(first.hasValue()) << first.status().message();
        const auto again = inputs.region(execution, catalog, **manifest, centre, 0.5, {});
        ASSERT_TRUE(again.hasValue());
        EXPECT_EQ(again->get(), first->get()) << "the same field's region is the one already read";
        const auto built = inputs.catalog(execution, *first);
        const auto reused = inputs.catalog(execution, *again);
        ASSERT_TRUE(built.hasValue() && reused.hasValue());
        EXPECT_EQ(reused->get(), built->get());
        const an::ui::processes::SpccRunInputReads reads = inputs.reads();
        EXPECT_EQ(reads.catalogVerifications, 1u);
        EXPECT_EQ(reads.manifests, 1u);
        EXPECT_EQ(reads.repositories, 1u);
        EXPECT_EQ(reads.handoffs, 1u);
        EXPECT_EQ(reads.regions, 1u);
        EXPECT_EQ(reads.catalogs, 1u);

        const an::SkyCoordinate moved{.rightAscensionDegrees = centre.rightAscensionDegrees + 0.01,
                                      .declinationDegrees = centre.declinationDegrees};
        const auto elsewhere = inputs.region(execution, catalog, **manifest, moved, 0.5, {});
        ASSERT_TRUE(elsewhere.hasValue());
        EXPECT_NE(elsewhere->get(), first->get()) << "another field reads its own region";
        EXPECT_EQ(inputs.reads().regions, 2u);
    }

    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()));
    ASSERT_NE(target, nullptr);
    WindowHandle<SpectrophotometricColorCalibrationProcess> window = openSpectrophotometric(*target);
    ASSERT_TRUE(reportOf(*window).canExecute());
    for (int run = 0; run < 2; ++run)
    {
        ASSERT_TRUE(applySpectrophotometric(*window, *target)) << "run " << run;
        const an::ui::processes::SpccRunInputReads reads = window->lastRunInputReads();
        EXPECT_EQ(reads.catalogVerifications, 1u) << "run " << run;
        EXPECT_EQ(reads.manifests, 1u) << "run " << run;
        EXPECT_EQ(reads.repositories, 1u) << "run " << run;
        EXPECT_EQ(reads.handoffs, 1u) << "run " << run;
        EXPECT_EQ(reads.regions, 1u) << "run " << run;
        EXPECT_EQ(reads.catalogs, 1u) << "run " << run;
        // Back to the uncalibrated image, so that the same window runs again.
        an::ui::IViewHistory* history = target->viewHistory();
        ASSERT_NE(history, nullptr);
        ASSERT_TRUE(history->undo());
        ASSERT_TRUE(pumpUntil([history]() { return !history->hasUndo(); }));
        ASSERT_TRUE(settleView(*target));
        ASSERT_TRUE(settleReadiness(*window));
        ASSERT_TRUE(reportOf(*window).canExecute()) << reportOf(*window).summary.toStdString();
    }
    window.reset();
    dropView(context(), target);
}

// launch: the registered action opens the window over the active view, attached under its name and needing the view
// to itself.
TEST_F(SpectrophotometricWindowTest, TheRegisteredActionOpensTheWindowOverTheActiveView)
{
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()));
    ASSERT_NE(target, nullptr);
    context().addViewControl(target.get());
    context().setActiveImageViewControl(target.get());

    const an::ui::process_registry::ProcessDescriptor* descriptor =
        shared::registered("color.actionSpectrophotometric_Color_Calibration");
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->process_name, "Spectrophotometric Color Calibration");
    EXPECT_TRUE(descriptor->requires_exclusive_access);
    ASSERT_TRUE(descriptor->launch_single_image);
    descriptor->launch_single_image(target.get());

    ASSERT_EQ(shared::openedProcessWindows().size(), 1u);
    EXPECT_EQ(shared::openedProcessWindows()[0].className, "SpectrophotometricColorCalibrationProcess");
    ASSERT_EQ(context().openProcesses.size(), 1u) << "the launch opens one window";
    auto* window = dynamic_cast<SpectrophotometricColorCalibrationProcess*>(context().openProcesses.front());
    ASSERT_NE(window, nullptr);
    EXPECT_EQ(window->attachmentName(), "Spectrophotometric Color Calibration");
    EXPECT_TRUE(window->requiresExclusiveAccess());
    EXPECT_TRUE(window->isVisible());
    EXPECT_FALSE(window->isRTPreviewProcess()) << "the calibration offers no preview";
    ASSERT_TRUE(settleReadiness(*window));
    EXPECT_EQ(window->runTelemetry().previewsRequested, 0u);
    window->close();
    ASSERT_TRUE(pumpUntil([this]() { return context().openProcesses.empty(); }));
    dropView(context(), target);
}

// readiness: the window decides on a worker whether it can run - Apply waits for the decision and an Apply asked for
// before it lands does nothing - and says what it will run with once it can.
TEST_F(SpectrophotometricWindowTest, ApplyWaitsForTheReadinessDecidedOnAWorker)
{
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()));
    ASSERT_NE(target, nullptr);
    WindowHandle<SpectrophotometricColorCalibrationProcess> window(
        new SpectrophotometricColorCalibrationProcess(target.get()));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    ASSERT_TRUE(window->readinessPending()) << "the decision is not made on the GUI thread";
    QPushButton* apply = applyButtonOf(*window);
    ASSERT_NE(apply, nullptr);
    EXPECT_FALSE(apply->isEnabled());
    const std::vector<double> before = documentValues(target->document());
    window->executeCalibration();
    EXPECT_EQ(context().errorCount(), 0);

    ASSERT_TRUE(settleReadiness(*window));
    EXPECT_GE(window->readinessReportsShown(), 1u);
    EXPECT_EQ(documentValues(target->document()), before) << "an Apply asked for before the decision runs nothing";
    EXPECT_TRUE(apply->isEnabled());
    const an::ui::processes::SpccProcessReadinessReport& report = reportOf(*window);
    EXPECT_TRUE(report.canExecute()) << report.summary.toStdString();
    EXPECT_EQ(report.whiteReferenceId.toStdString(), kSpccWhiteReferenceId);
    EXPECT_EQ(report.catalogManifestId.toStdString(), kSpccManifestId);
    auto* status = widgetOf<QLabel>(*window, "spccProcessStatusLabel");
    ASSERT_NE(status, nullptr);
    EXPECT_EQ(status->text(),
              QStringLiteral("Ready to run SPCC with response profile %1 and white reference %2.")
                  .arg(QString::fromLatin1(kSpccProfileId), QString::fromLatin1(kSpccWhiteReferenceId)));
}

// readiness: a decision asked for while every thread of the session is taken waits on its worker for one, rather than
// being refused for want of a thread and leaving Apply disabled with nothing to ask again.
TEST_F(SpectrophotometricWindowTest, AReadinessAskedForWhileNoThreadIsFreeWaitsForOne)
{
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()));
    ASSERT_NE(target, nullptr);
    ASSERT_TRUE(settleView(*target));
    const an::consumer::AdmissionSnapshot before = session().admission();
    std::optional<an::consumer::OperationScope> held;
    {
        an::Result<an::consumer::OperationScope> every = session().tryAdmit(an::consumer::OperationRequest{
            .purpose = an::consumer::OperationPurpose::Apply, .maxConcurrency = before.width});
        ASSERT_TRUE(every.hasValue()) << every.status().message();
        held.emplace(std::move(*every));
    }
    ASSERT_EQ(session().admission().otherThreads, before.width) << "a thread is left free";

    WindowHandle<SpectrophotometricColorCalibrationProcess> window(
        new SpectrophotometricColorCalibrationProcess(target.get()));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    // The decision has asked for its thread once it waits in the queue or has been refused.
    ASSERT_TRUE(pumpUntil([this, &before]() {
        const an::consumer::AdmissionSnapshot now = session().admission();
        return now.queued > before.queued || now.refusedBusy > before.refusedBusy;
    }));
    held.reset();

    ASSERT_TRUE(settleReadiness(*window));
    EXPECT_EQ(session().admission().refusedBusy, before.refusedBusy) << "the decision was refused for want of a thread";
    const an::ui::processes::SpccProcessReadinessReport& report = reportOf(*window);
    EXPECT_TRUE(report.canExecute()) << report.summary.toStdString();
    QPushButton* apply = applyButtonOf(*window);
    ASSERT_NE(apply, nullptr);
    EXPECT_TRUE(apply->isEnabled());
}

// readiness: what the run lacks is refused by name - an image marked nonlinear, one already colour calibrated and a
// response repository whose rights are not reviewed - with Apply disabled, and an Apply asked for anyway is refused
// with the reason; a missing catalog is offered as a download of the field's chunks; an unsolved image is offered
// Apply, as it was, and refused by name before anything is read.
TEST_F(SpectrophotometricWindowTest, WhatTheRunLacksIsRefusedByName)
{
    const auto refused = [this](const char* name, const an::ImageMetadata& metadata,
                                const std::function<void(const an::ui::processes::SpccProcessReadinessReport&)>& why) {
        SCOPED_TRACE(name);
        ViewHandle target = fieldView<float>(session(), field(), metadata, name);
        ASSERT_NE(target, nullptr);
        WindowHandle<SpectrophotometricColorCalibrationProcess> window = openSpectrophotometric(*target);
        const an::ui::processes::SpccProcessReadinessReport& report = reportOf(*window);
        EXPECT_FALSE(report.canExecute());
        EXPECT_FALSE(report.summary.isEmpty());
        why(report);
        EXPECT_FALSE(applyButtonOf(*window)->isEnabled());
        const int errors = context().errorCount();
        const std::vector<double> before = documentValues(target->document());
        window->executeCalibration();
        EXPECT_EQ(context().errorCount(), errors + 1);
        EXPECT_EQ(documentValues(target->document()), before);
        window.reset();
        dropView(context(), target);
    };

    FieldMetadataOptions nonlinear;
    nonlinear.state = an::ProcessingStateRecord{.linearity = an::RecordedLinearity::Nonlinear,
                                                .channelBasis = an::RecordedChannelBasis::LinearRgb};
    refused("nonlinear", fieldMetadata(field(), nonlinear), [](const auto& report) {
        EXPECT_TRUE(report.imageStateChecked);
        EXPECT_FALSE(report.imageStateCompatible);
        EXPECT_FALSE(report.imageStateAlreadyColorCalibrated);
    });
    FieldMetadataOptions calibrated;
    calibrated.state = an::ProcessingStateRecord{.linearity = an::RecordedLinearity::Linear,
                                                 .channelBasis = an::RecordedChannelBasis::LinearRgb,
                                                 .colorCalibration = an::RecordedColorCalibration::Photometric};
    refused("calibrated", fieldMetadata(field(), calibrated),
            [](const auto& report) { EXPECT_TRUE(report.imageStateAlreadyColorCalibrated); });

    ASSERT_TRUE(writeResponseRepository(catalogs().responseDirectory(), false));
    refused("unreviewed", fieldMetadata(field()), [](const auto& report) {
        EXPECT_TRUE(report.catalogReady);
        EXPECT_FALSE(report.responseAssetsReady && report.responseProfileResolved);
    });
    ASSERT_TRUE(writeResponseRepository(catalogs().responseDirectory(), true));

    // With no catalog installed the run is offered once the field's chunks are downloaded, which Apply starts first.
    (void)context().upsert(QString::fromLatin1(SettingsKeys::CATALOGS_SPCC_CATALOG_DIRECTORY),
                           catalogs().spectrumDirectory() + QStringLiteral("-missing"));
    {
        ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()), "uncatalogued");
        ASSERT_NE(target, nullptr);
        WindowHandle<SpectrophotometricColorCalibrationProcess> window = openSpectrophotometric(*target);
        const an::ui::processes::SpccProcessReadinessReport& report = reportOf(*window);
        EXPECT_FALSE(report.catalogReady);
        EXPECT_TRUE(report.catalogChunkDownloadRequired);
        EXPECT_TRUE(report.catalogChunkAutoDownloadAvailable);
        EXPECT_EQ(widgetOf<QLabel>(*window, "spccProcessStatusLabel")->text(),
                  QStringLiteral("Ready to run SPCC; AfterNight will download needed catalog chunks first."));
        window.reset();
        dropView(context(), target);
    }
    catalogs().useSpectrumCatalogAndRepository();

    FieldMetadataOptions unsolved;
    unsolved.solved = false;
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field(), unsolved), "unsolved");
    ASSERT_NE(target, nullptr);
    WindowHandle<SpectrophotometricColorCalibrationProcess> window = openSpectrophotometric(*target);
    EXPECT_TRUE(applyButtonOf(*window)->isEnabled()) << "the readiness does not read the solution";
    window->executeCalibration();
    ASSERT_FALSE(context().errorTexts.isEmpty());
    EXPECT_EQ(context().errorTexts.back(), QStringLiteral("SPCC requires a plate-solved image."));
}

// metadata: the result means what it meant, colour calibrated; its lineage names the engine operation; its cards
// record the calibration - spectrophotometric, the gains and offsets, the response profile, the white reference, the
// catalog and the recipe's version - and the window then says the image is calibrated.
TEST_F(SpectrophotometricWindowTest, TheResultIsColourCalibratedAndRecordsTheRecipe)
{
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()));
    ASSERT_NE(target, nullptr);
    WindowHandle<SpectrophotometricColorCalibrationProcess> window = openSpectrophotometric(*target);
    ASSERT_TRUE(applySpectrophotometric(*window, *target));
    ASSERT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    EXPECT_EQ(context().messages.count(QStringLiteral("info:Spectrophotometric Color Calibration")), 1);

    const std::optional<an::ImageMetadata> metadata = documentMetadata(target->document());
    ASSERT_TRUE(metadata.has_value());
    EXPECT_TRUE(metadata->semantics().processingState().colorCalibrated);
    ASSERT_FALSE(metadata->provenance().entries().empty());
    EXPECT_EQ(metadata->provenance().entries().back().operation(), "an::calibrateSpectrophotometricColor");

    const an::ColorCalibrationReading reading = an::readColorCalibrationCards(metadata->codec());
    const std::array<double, 3u> gains = expectedSpectrophotometricGains(kRedCast, kBlueCast);
    EXPECT_EQ(reading.record.calibration, an::RecordedColorCalibration::Spectrophotometric);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_NEAR(reading.record.gains[channel], gains[channel], 0.02 * gains[channel]) << "channel " << channel;
    }
    EXPECT_EQ(reading.record.responseProfileId, kSpccProfileId);
    EXPECT_EQ(reading.record.whiteReferenceId, kSpccWhiteReferenceId);
    EXPECT_EQ(reading.record.catalogManifestId, kSpccManifestId);
    EXPECT_EQ(reading.record.catalogFamilyId, an::kXpCatalogFamilyId);
    EXPECT_EQ(reading.record.catalogAccessProfileId, "local-full");
    EXPECT_FALSE(reading.record.responseProfileVersion.empty());
    EXPECT_FALSE(reading.record.whiteReferenceVersion.empty());
    EXPECT_EQ(reading.record.algorithmVersion, "spcc-sampled-xp-color-calibration-v3");
    EXPECT_EQ(reading.record.inputLinearity, an::RecordedLinearity::Linear) << "the state of the image it was run on";
    EXPECT_EQ(reading.record.inputChannelBasis, an::RecordedChannelBasis::LinearRgb);
    EXPECT_EQ(an::readProcessingStateCards(metadata->codec()).colorCalibration,
              an::RecordedColorCalibration::Spectrophotometric);
    EXPECT_EQ(metadata->acquisition().instrumentName(), kSpccCameraModel);

    // A saved file carries the calibration: an image opened from it reads the same record.
    QTemporaryDir saved;
    ASSERT_TRUE(saved.isValid());
    const std::optional<an::ImageMetadata> reopened =
        writtenAndReadBack(session(), target->document(), saved.filePath(QStringLiteral("calibrated.fits")));
    ASSERT_TRUE(reopened.has_value());
    const an::ColorCalibrationReading fromFile = an::readColorCalibrationCards(reopened->codec());
    EXPECT_EQ(fromFile.record.calibration, an::RecordedColorCalibration::Spectrophotometric);
    EXPECT_EQ(fromFile.record.responseProfileId, kSpccProfileId);
    EXPECT_EQ(fromFile.record.whiteReferenceId, kSpccWhiteReferenceId);
    EXPECT_EQ(fromFile.record.catalogManifestId, kSpccManifestId);
    EXPECT_EQ(fromFile.record.algorithmVersion, reading.record.algorithmVersion);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_NEAR(fromFile.record.gains[channel], reading.record.gains[channel], 1.0e-9) << "channel " << channel;
    }
    EXPECT_EQ(fromFile.record.inputLinearity, an::RecordedLinearity::Linear);
    EXPECT_EQ(an::readProcessingStateCards(reopened->codec()).colorCalibration,
              an::RecordedColorCalibration::Spectrophotometric);

    // The image the window just calibrated is not warned about as already calibrated: the status keeps the outcome.
    const an::ui::processes::SpccProcessReadinessReport& report = reportOf(*window);
    EXPECT_TRUE(report.imageStateAlreadyColorCalibrated);
    EXPECT_TRUE(report.summary.isEmpty()) << report.summary.toStdString();
    EXPECT_FALSE(
        widgetOf<QLabel>(*window, "spccProcessStatusLabel")->text().contains(QStringLiteral("not compatible")));
    EXPECT_FALSE(applyButtonOf(*window)->isEnabled()) << "a calibrated image is not calibrated again";

    // Once the image is edited again it is no longer the result the window made, and says it is already calibrated.
    issueCopyEdit(*target);
    ASSERT_TRUE(settleView(*target));
    ASSERT_TRUE(settleReadiness(*window));
    EXPECT_EQ(target->viewHistory()->previousStepName(), "Copy");
    EXPECT_FALSE(reportOf(*window).summary.isEmpty()) << "the warning returns for an image the window did not make";
}

// readiness: the panel's catalog action opens the Catalogs & Data page and decides the readiness again when it
// returns, so what the person installed or removed there is what the window offers.
TEST_F(SpectrophotometricWindowTest, TheCatalogPageReturnsToAFreshReadiness)
{
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()));
    ASSERT_NE(target, nullptr);
    WindowHandle<SpectrophotometricColorCalibrationProcess> window = openSpectrophotometric(*target);
    ASSERT_TRUE(reportOf(*window).catalogReady);
    const std::uint64_t shown = window->readinessReportsShown();
    (void)context().upsert(QString::fromLatin1(SettingsKeys::CATALOGS_SPCC_CATALOG_DIRECTORY),
                           catalogs().spectrumDirectory() + QStringLiteral("-removed"));
    auto* manage = widgetOf<QPushButton>(*window, "spccProcessManageCatalogsDataButton");
    ASSERT_NE(manage, nullptr);
    manage->click();
    EXPECT_EQ(context().messages.count(QStringLiteral("settings:Catalogs & Data")), 1);
    ASSERT_TRUE(settleReadiness(*window));
    EXPECT_GT(window->readinessReportsShown(), shown);
    EXPECT_FALSE(reportOf(*window).catalogReady) << "the readiness read the settings as the page left them";
}

// detection choices: the dialog states the engine's request - its source count, signal-to-noise floor, match radius,
// magnitude limit and its automatic choice, residual bounds, aperture, annulus clip and fit - and the detector keeps
// as many stars as the dialog asks for.
TEST_F(SpectrophotometricWindowTest, TheDialogStatesTheEngineRequest)
{
    const SpectrophotometricColorCalibrationParams defaults;
    const an::SpectrophotometricColorCalibrationRequest request =
        an::ui::processes::spectrophotometricCalibrationRequest(defaults, QStringLiteral("L"),
                                                                QStringLiteral("wcs-identity"), 2024.5);
    EXPECT_EQ(request.maximumStars, 4096u);
    EXPECT_EQ(request.sourceSelection.maximumSources, 4096u);
    EXPECT_DOUBLE_EQ(request.sourceSelection.minimumSignalToNoise, 5.0);
    EXPECT_DOUBLE_EQ(request.preflight.matchRadiusArcseconds, 5.0);
    EXPECT_DOUBLE_EQ(request.preflight.magnitudeLimit, 17.6);
    EXPECT_TRUE(request.preflight.autoMagnitude.enabled);
    EXPECT_DOUBLE_EQ(request.preflight.residualThresholdPixels, 3.0);
    EXPECT_DOUBLE_EQ(request.preflight.residualThresholdArcseconds, 3.0);
    EXPECT_EQ(request.preflight.observationEpoch, "2024.500000");
    EXPECT_EQ(request.preflight.properMotionModelVersion, "gaia_epoch_correction_v1");
    EXPECT_EQ(request.preflight.wcsSolutionId, "wcs-identity");
    EXPECT_DOUBLE_EQ(request.samples.aperture.fullWidthRadiusScale, 2.0);
    EXPECT_DOUBLE_EQ(request.samples.aperture.annulusClipSigmas, 3.0);
    EXPECT_EQ(request.fit.minimumSamples, an::kDefaultSpectrophotometricMinimumSamples);
    EXPECT_EQ(request.fit.lowSupportSamples, an::kDefaultSpectrophotometricLowSupportSamples);
    EXPECT_EQ(request.fit.outlierPolicy, an::SpectrophotometricOutlierPolicy::IterativeSigmaClipRefit);
    EXPECT_DOUBLE_EQ(request.fit.clipSigmas, an::kDefaultSpectrophotometricClipSigmas);
    EXPECT_EQ(request.fit.maximumIterations, an::kDefaultSpectrophotometricMaximumIterations);
    EXPECT_TRUE(request.background.neutralize);
    EXPECT_EQ(request.filterName, "L");
    EXPECT_FALSE(request.allowNarrowbandInput);
    EXPECT_EQ(an::ui::processes::spectrophotometricStarDetection(defaults).maximumStars, 4096u);

    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()));
    ASSERT_NE(target, nullptr);
    WindowHandle<SpectrophotometricColorCalibrationProcess> window = openWindow<DescribedSpectrophotometric>(*target);
    auto& described = static_cast<DescribedSpectrophotometric&>(*window);
    widgetOf<QCheckBox>(*window, "spccAutoMagnitudeLimitCheck")->setChecked(false);
    widgetOf<QDoubleSpinBox>(*window, "spccMagnitudeLimitSpin")->setValue(15.5);
    widgetOf<QDoubleSpinBox>(*window, "spccMatchRadiusSpin")->setValue(7.5);
    widgetOf<QSpinBox>(*window, "spccMaxSourcesSpin")->setValue(1024);
    widgetOf<QDoubleSpinBox>(*window, "spccSourceSnrSpin")->setValue(8.0);
    widgetOf<QDoubleSpinBox>(*window, "spccResidualPxSpin")->setValue(2.0);
    widgetOf<QDoubleSpinBox>(*window, "spccResidualArcsecSpin")->setValue(4.0);
    widgetOf<QDoubleSpinBox>(*window, "spccAnnulusSigmaClipSpin")->setValue(2.5);
    widgetOf<QDoubleSpinBox>(*window, "spccOutlierSigmaSpin")->setValue(2.0);
    widgetOf<QSpinBox>(*window, "spccFitIterationsSpin")->setValue(4);
    widgetOf<QSpinBox>(*window, "spccMinimumSamplesSpin")->setValue(70);
    widgetOf<QDoubleSpinBox>(*window, "spccApertureScaleSpin")->setValue(2.5);
    widgetOf<QCheckBox>(*window, "spccBackgroundNeutralizationCheck")->setChecked(false);
    const SpectrophotometricColorCalibrationParams params = described.params();
    const an::SpectrophotometricColorCalibrationRequest asked =
        an::ui::processes::spectrophotometricCalibrationRequest(params, QString(), QString(), std::nullopt);
    EXPECT_FALSE(asked.preflight.autoMagnitude.enabled);
    EXPECT_DOUBLE_EQ(asked.preflight.magnitudeLimit, 15.5);
    EXPECT_DOUBLE_EQ(asked.preflight.matchRadiusArcseconds, 7.5);
    EXPECT_EQ(asked.maximumStars, 1024u);
    EXPECT_DOUBLE_EQ(asked.sourceSelection.minimumSignalToNoise, 8.0);
    EXPECT_EQ(asked.fit.minimumSamples, 70u);
    EXPECT_EQ(asked.fit.lowSupportSamples, 70u) << "the low-support count is never below the minimum";
    EXPECT_DOUBLE_EQ(asked.samples.aperture.fullWidthRadiusScale, 2.5);
    EXPECT_DOUBLE_EQ(asked.preflight.residualThresholdPixels, 2.0);
    EXPECT_DOUBLE_EQ(asked.preflight.residualThresholdArcseconds, 4.0);
    EXPECT_DOUBLE_EQ(asked.samples.aperture.annulusClipSigmas, 2.5);
    EXPECT_DOUBLE_EQ(asked.fit.clipSigmas, 2.0);
    EXPECT_EQ(asked.fit.maximumIterations, 4u);
    EXPECT_FALSE(asked.background.neutralize);
    EXPECT_TRUE(asked.preflight.observationEpoch.empty()) << "an image with no exposure start states no epoch";
    EXPECT_EQ(an::ui::processes::spectrophotometricStarDetection(params).maximumStars, 1024u);
    ASSERT_TRUE(settleReadiness(*window));
}

// persistence: the dialog opens at its defaults, Reset restores them after a person changed them, and an Apply leaves
// what the person set.
TEST_F(SpectrophotometricWindowTest, TheDialogOpensAtItsDefaultsResetRestoresThemAndAnApplyKeepsThem)
{
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()));
    ASSERT_NE(target, nullptr);
    WindowHandle<SpectrophotometricColorCalibrationProcess> window = openWindow<DescribedSpectrophotometric>(*target);
    auto& described = static_cast<DescribedSpectrophotometric&>(*window);
    const SpectrophotometricColorCalibrationParams defaults;
    const SpectrophotometricColorCalibrationParams opened = described.params();
    EXPECT_EQ(opened.autoMagnitudeLimit, defaults.autoMagnitudeLimit);
    EXPECT_DOUBLE_EQ(opened.magnitudeLimit, defaults.magnitudeLimit);
    EXPECT_DOUBLE_EQ(opened.matchRadiusArcsec, defaults.matchRadiusArcsec);
    EXPECT_EQ(opened.maxDetectedSources, defaults.maxDetectedSources);
    EXPECT_DOUBLE_EQ(opened.sourceSnr, defaults.sourceSnr);
    EXPECT_EQ(opened.minimumSampleCount, defaults.minimumSampleCount);
    EXPECT_DOUBLE_EQ(opened.apertureFwhmScale, defaults.apertureFwhmScale);
    EXPECT_DOUBLE_EQ(opened.annulusSigmaClip, defaults.annulusSigmaClip);
    EXPECT_EQ(opened.fitOutlierPolicy, defaults.fitOutlierPolicy);
    EXPECT_DOUBLE_EQ(opened.outlierSigma, defaults.outlierSigma);
    EXPECT_EQ(opened.fitIterations, defaults.fitIterations);
    EXPECT_EQ(opened.applyBackgroundNeutralization, defaults.applyBackgroundNeutralization);
    EXPECT_EQ(opened.catalogDirectory, catalogs().spectrumDirectory());
    EXPECT_EQ(opened.responseRepositoryDirectory, catalogs().responseDirectory());

    widgetOf<QDoubleSpinBox>(*window, "spccMatchRadiusSpin")->setValue(6.0);
    widgetOf<QSpinBox>(*window, "spccFitIterationsSpin")->setValue(3);
    widgetOf<QCheckBox>(*window, "spccBackgroundNeutralizationCheck")->setChecked(false);
    ASSERT_TRUE(applySpectrophotometric(*window, *target));
    ASSERT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const SpectrophotometricColorCalibrationParams applied = described.params();
    EXPECT_DOUBLE_EQ(applied.matchRadiusArcsec, 6.0);
    EXPECT_EQ(applied.fitIterations, 3);
    EXPECT_FALSE(applied.applyBackgroundNeutralization);

    auto* box = window->findChild<QDialogButtonBox*>(QStringLiteral("buttonBox"));
    ASSERT_NE(box, nullptr);
    box->button(QDialogButtonBox::Reset)->click();
    ASSERT_TRUE(settleReadiness(*window));
    const SpectrophotometricColorCalibrationParams reset = described.params();
    EXPECT_DOUBLE_EQ(reset.matchRadiusArcsec, defaults.matchRadiusArcsec);
    EXPECT_EQ(reset.fitIterations, defaults.fitIterations);
    EXPECT_TRUE(reset.applyBackgroundNeutralization);
}

// apply-mask: a mask on the target weighs the result pixel by pixel - the source where it is black, the result where
// it is white.
TEST_F(SpectrophotometricWindowTest, TheTargetsMaskWeighsTheResultPixelByPixel)
{
    ViewHandle plain = fieldView<float>(session(), field(), fieldMetadata(field()), "plain");
    ASSERT_NE(plain, nullptr);
    const std::vector<double> source = documentValues(plain->document());
    {
        WindowHandle<SpectrophotometricColorCalibrationProcess> window = openSpectrophotometric(*plain);
        ASSERT_TRUE(applySpectrophotometric(*window, *plain));
    }
    const std::vector<double> unmasked = documentValues(plain->document());

    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()), "masked");
    ASSERT_NE(target, nullptr);
    const std::uint32_t half = field().height / 2u;
    std::optional<an::ui::ImageDocument> maskDocument =
        greyDocument(session(), field().width, field().height,
                     [half](std::uint32_t, std::uint32_t y) { return y < half ? 0.0f : 1.0f; });
    ASSERT_TRUE(maskDocument.has_value());
    ViewHandle mask = makeTargetView("mask", std::move(*maskDocument));
    target->setCurrentMask(mask.get());
    target->setEnableMask(true);
    ASSERT_TRUE(target->isMasked());
    {
        WindowHandle<SpectrophotometricColorCalibrationProcess> window = openSpectrophotometric(*target);
        ASSERT_TRUE(applySpectrophotometric(*window, *target));
    }
    ASSERT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::vector<double> masked = documentValues(target->document());
    ASSERT_EQ(masked.size(), source.size());
    ASSERT_EQ(unmasked.size(), source.size());
    const std::size_t plane = static_cast<std::size_t>(field().width) * field().height;
    double largest = 0.0;
    for (std::size_t index = 0u; index < masked.size(); ++index)
    {
        const std::size_t y = (index % plane) / field().width;
        const double expected = y < half ? source[index] : unmasked[index];
        largest = (std::max)(largest, std::abs(masked[index] - expected));
    }
    EXPECT_LE(largest, 1.0e-6);
    target->removeCurrentMask();
}

// history: an Apply is one step named after the process and described by the recipe it ran, and undoing it brings the
// image back.
TEST_F(SpectrophotometricWindowTest, AnApplyIsOneHistoryStepThatUndoes)
{
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()));
    ASSERT_NE(target, nullptr);
    const std::vector<double> source = documentValues(target->document());
    WindowHandle<SpectrophotometricColorCalibrationProcess> window = openWindow<DescribedSpectrophotometric>(*target);
    const an::ui::ProcessStep step = static_cast<DescribedSpectrophotometric&>(*window).describeStep();
    EXPECT_EQ(step.name, "Spectrophotometric Color Calibration");
    EXPECT_EQ(step.description, std::string("Response profile: ") + kSpccProfileId +
                                    ", White reference: " + kSpccWhiteReferenceId + ", Background neutralization: on")
        << "the step names the recipe the readiness resolved";
    ASSERT_TRUE(applySpectrophotometric(*window, *target));
    an::ui::IViewHistory* history = target->viewHistory();
    ASSERT_NE(history, nullptr);
    ASSERT_TRUE(history->hasUndo());
    EXPECT_EQ(history->previousStepName(), "Spectrophotometric Color Calibration");
    EXPECT_NE(documentValues(target->document()), source);
    window.reset();
    ASSERT_TRUE(history->undo());
    ASSERT_TRUE(pumpUntil([history]() { return !history->hasUndo(); }));
    ASSERT_TRUE(settleView(*target));
    EXPECT_EQ(documentValues(target->document()), source);
}

// representation: every representation is calibrated as fractions of full scale - an integer as a fraction of its
// range, a signed one counted from its smallest value - and the result is kept in it.
TEST_F(SpectrophotometricWindowTest, EveryRepresentationIsCalibratedAsFractionsOfFullScaleAndKept)
{
    const std::array<double, 3u> gains = expectedSpectrophotometricGains(kRedCast, kBlueCast);
    const auto check = [this, &gains]<typename SampleT>(const char* name, double tolerance) {
        SCOPED_TRACE(name);
        ViewHandle target = fieldView<SampleT>(session(), field(), fieldMetadata(field()), name);
        ASSERT_NE(target, nullptr);
        const std::vector<double> before = documentValues(target->document());
        WindowHandle<SpectrophotometricColorCalibrationProcess> window = openSpectrophotometric(*target);
        ASSERT_TRUE(applySpectrophotometric(*window, *target));
        EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        const std::optional<an::ImageDescriptor> descriptor = target->document().descriptor();
        ASSERT_TRUE(descriptor.has_value());
        EXPECT_EQ(descriptor->sampleType(), an::kSampleTypeOf<SampleT>);
        expectCalibratedBy(field(), before, documentValues(target->document()), gains, tolerance);
        window.reset();
        dropView(context(), target);
    };
    check.template operator()<std::uint8_t>("UInt8", 0.08);
    check.template operator()<std::int16_t>("Int16", 0.02);
    check.template operator()<std::uint16_t>("UInt16", 0.02);
    check.template operator()<double>("Float64", 0.02);
}

// representation: a floating image keeps what the gains make beyond full scale, where an integer one saturates.
TEST_F(SpectrophotometricWindowTest, AFloatingImageKeepsValuesBeyondFullScale)
{
    StarField bright = field();
    const std::size_t plane = static_cast<std::size_t>(bright.width) * bright.height;
    const std::size_t at = static_cast<std::size_t>(2u) * bright.width + 2u;
    bright.samples[2u * plane + at] = 1.6f;
    ViewHandle target = fieldView<float>(session(), bright, fieldMetadata(bright));
    ASSERT_NE(target, nullptr);
    WindowHandle<SpectrophotometricColorCalibrationProcess> window = openSpectrophotometric(*target);
    ASSERT_TRUE(applySpectrophotometric(*window, *target));
    ASSERT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::vector<double> values = documentValues(target->document());
    ASSERT_EQ(values.size(), 3u * plane);
    EXPECT_GT(values[2u * plane + at], 1.2) << "the blue gain is one, and the sample is kept beyond full scale";
}

// cancel: a run whose stop is asked for as its body starts publishes nothing, leaves the image and its history as they
// were and shows no error.
TEST_F(SpectrophotometricWindowTest, AStoppedRunPublishesNothing)
{
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()));
    ASSERT_NE(target, nullptr);
    const std::vector<double> source = documentValues(target->document());
    WindowHandle<SpectrophotometricColorCalibrationProcess> window = openWindow<StoppingSpectrophotometric>(*target);
    ASSERT_TRUE(applySpectrophotometric(*window, *target));
    const ProcessRunTelemetry telemetry = window->runTelemetry();
    EXPECT_EQ(telemetry.runsCancelled, 1u);
    EXPECT_EQ(telemetry.runsCommitted, 0u);
    EXPECT_EQ(documentValues(target->document()), source);
    ASSERT_NE(target->viewHistory(), nullptr);
    EXPECT_FALSE(target->viewHistory()->hasUndo());
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    EXPECT_EQ(context().messages.count(QStringLiteral("info:Spectrophotometric Color Calibration")), 0);
}

// error: a fit the field cannot support - more samples asked for than it has stars - is refused by the engine; the run
// says why, publishes nothing and leaves the history as it was.
TEST_F(SpectrophotometricWindowTest, AFitTheFieldCannotSupportSaysWhyAndChangesNothing)
{
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()));
    ASSERT_NE(target, nullptr);
    const std::vector<double> source = documentValues(target->document());
    WindowHandle<SpectrophotometricColorCalibrationProcess> window = openSpectrophotometric(*target);
    widgetOf<QSpinBox>(*window, "spccMinimumSamplesSpin")->setValue(500);
    ASSERT_TRUE(applySpectrophotometric(*window, *target));
    EXPECT_EQ(documentValues(target->document()), source);
    EXPECT_FALSE(target->viewHistory()->hasUndo());
    EXPECT_EQ(context().errorCount(), 1);
    auto* diagnostics = widgetOf<QPlainTextEdit>(*window, "spccDiagnosticsText");
    ASSERT_NE(diagnostics, nullptr);
    EXPECT_TRUE(diagnostics->toPlainText().contains(QStringLiteral("the spectrophotometric fit was refused")))
        << diagnostics->toPlainText().toStdString();
    EXPECT_FALSE(diagnostics->toPlainText().contains(QStringLiteral("Quality score")))
        << "a refused run has no figures to show";
    EXPECT_EQ(context().messages.count(QStringLiteral("info:Spectrophotometric Color Calibration")), 0);
}

// attestation: an image that states no processing state is not calibrated until a person attests it linear - a "no"
// changes nothing, a "yes" records the attestation as an edit the history keeps, and the window can then run.
TEST_F(SpectrophotometricWindowTest, APersonCanAttestAnImagesLinearState)
{
    FieldMetadataOptions stateless;
    stateless.state.reset();
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field(), stateless));
    ASSERT_NE(target, nullptr);
    WindowHandle<SpectrophotometricColorCalibrationProcess> window = openSpectrophotometric(*target);
    {
        const an::ui::processes::SpccProcessReadinessReport& report = reportOf(*window);
        EXPECT_TRUE(report.imageStateChecked);
        EXPECT_FALSE(report.imageStateCompatible);
        EXPECT_FALSE(report.canExecute());
    }
    auto* attest = widgetOf<QPushButton>(*window, "spccProcessAssignImageStateButton");
    ASSERT_NE(attest, nullptr);
    EXPECT_FALSE(attest->isHidden());

    context().answerYes = false;
    attest->click();
    ASSERT_TRUE(settleView(*target));
    EXPECT_EQ(context().messages.count(QStringLiteral("question:Assign Linear Image State")), 1);
    EXPECT_FALSE(target->viewHistory()->hasUndo());

    context().answerYes = true;
    attest->click();
    ASSERT_TRUE(settleView(*target));
    ASSERT_TRUE(settleReadiness(*window));
    ASSERT_TRUE(target->viewHistory()->hasUndo());
    EXPECT_EQ(target->viewHistory()->previousStepName(), "Assign Linear Image State");
    const std::optional<an::ImageMetadata> metadata = documentMetadata(target->document());
    ASSERT_TRUE(metadata.has_value());
    const an::ProcessingStateRecord state = an::readProcessingStateCards(metadata->codec());
    EXPECT_EQ(state.linearity, an::RecordedLinearity::Linear);
    EXPECT_EQ(state.channelBasis, an::RecordedChannelBasis::LinearRgb);
    EXPECT_TRUE(reportOf(*window).canExecute()) << reportOf(*window).summary.toStdString();
    EXPECT_TRUE(attest->isHidden());
    EXPECT_TRUE(applyButtonOf(*window)->isEnabled());
}

// attestation: the evidence of an attestation is the image the person looked at - an image another edit changes before
// the attestation lands is not attested, and stays refused.
TEST_F(SpectrophotometricWindowTest, AnAttestationOfAnImageThatChangedSinceIsRefused)
{
    FieldMetadataOptions stateless;
    stateless.state.reset();
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field(), stateless));
    ASSERT_NE(target, nullptr);
    WindowHandle<SpectrophotometricColorCalibrationProcess> window = openSpectrophotometric(*target);
    auto* attest = widgetOf<QPushButton>(*window, "spccProcessAssignImageStateButton");
    ASSERT_NE(attest, nullptr);
    ASSERT_FALSE(attest->isHidden());

    // An edit already on its way when the person answers lands first, so the image attested is not the one they saw.
    issueCopyEdit(*target);
    context().answerYes = true;
    attest->click();
    ASSERT_TRUE(settleView(*target));
    ASSERT_TRUE(settleReadiness(*window));
    EXPECT_EQ(target->viewHistory()->previousStepName(), "Copy");
    const std::optional<an::ImageMetadata> metadata = documentMetadata(target->document());
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(an::readProcessingStateCards(metadata->codec()).linearity, an::RecordedLinearity::Unknown);
    EXPECT_FALSE(reportOf(*window).canExecute());
    EXPECT_EQ(context().messages.count(QStringLiteral("warning:Assign Linear Image State")), 1) << "the person is told";
}

// hand-off: an image an integration wrote is calibrated against the sources its registration matched, when they still
// hold for this image - the run says how many it used - and the gains are the field's.
TEST_F(SpectrophotometricWindowTest, TheIntegrationHandoffNarrowsTheSourcesToThoseThatStillHold)
{
    QTemporaryDir output;
    ASSERT_TRUE(output.isValid());
    writeWorkflowArchive(output.path(), QStringLiteral("stack.fits"), QStringLiteral("registration-L"));
    const QString targetPath = QDir(output.path()).filePath(QStringLiteral("stack.fits"));
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()), "stack", targetPath);
    ASSERT_NE(target, nullptr);
    const std::optional<an::ImageDescriptor> descriptor = target->document().descriptor();
    const std::optional<an::ImageMetadata> metadata = documentMetadata(target->document());
    ASSERT_TRUE(descriptor.has_value() && metadata.has_value());
    const std::optional<an::WcsSolution> solution = an::ui::processes::calibrationSolutionOf(*metadata, *descriptor);
    ASSERT_TRUE(solution.has_value());

    RecordedHandoff recorded;
    recorded.metricsDatabasePath = QDir(output.path()).filePath(QStringLiteral("registration_metrics.db"));
    recorded.registrationGroupKey = QStringLiteral("registration-L");
    for (std::size_t index = 0u; index < 80u; ++index)
    {
        an::MatchedSourceReference reference;
        reference.sourceIdentifier = field().stars[index].sourceIdentifier;
        reference.spectrumCatalogFamilyId = an::kXpCatalogFamilyId;
        reference.spectrumCatalogManifestId = kSpccManifestId;
        reference.wcsSolutionId = an::ui::processes::spccSolutionIdentity(*solution).toStdString();
        reference.properMotionModelVersion = an::ui::processes::kSpccProperMotionModelVersion;
        reference.separationArcseconds = 0.4;
        reference.residualPixels = 0.2;
        recorded.references.push_back(std::move(reference));
    }
    writeHandoffDatabase(recorded);
    const std::vector<double> before = documentValues(target->document());
    WindowHandle<SpectrophotometricColorCalibrationProcess> window = openSpectrophotometric(*target);
    ASSERT_TRUE(applySpectrophotometric(*window, *target));
    ASSERT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    auto* diagnostics = widgetOf<QPlainTextEdit>(*window, "spccDiagnosticsText");
    ASSERT_NE(diagnostics, nullptr);
    EXPECT_TRUE(diagnostics->toPlainText().contains(QStringLiteral("using 80 validated Gaia source_id references")))
        << diagnostics->toPlainText().toStdString();
    expectCalibratedBy(field(), before, documentValues(target->document()),
                       expectedSpectrophotometricGains(kRedCast, kBlueCast), 0.02);
}

// hand-off: sources matched against another solution no longer hold for this image; the run says so and calibrates
// against the whole field.
TEST_F(SpectrophotometricWindowTest, AStaleHandoffFallsBackToTheWholeField)
{
    QTemporaryDir output;
    ASSERT_TRUE(output.isValid());
    writeWorkflowArchive(output.path(), QStringLiteral("stack.fits"), QStringLiteral("registration-L"));
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()), "stack",
                                         QDir(output.path()).filePath(QStringLiteral("stack.fits")));
    ASSERT_NE(target, nullptr);
    RecordedHandoff recorded;
    recorded.metricsDatabasePath = QDir(output.path()).filePath(QStringLiteral("registration_metrics.db"));
    recorded.registrationGroupKey = QStringLiteral("registration-L");
    for (std::size_t index = 0u; index < 80u; ++index)
    {
        an::MatchedSourceReference reference;
        reference.sourceIdentifier = field().stars[index].sourceIdentifier;
        reference.spectrumCatalogFamilyId = an::kXpCatalogFamilyId;
        reference.spectrumCatalogManifestId = kSpccManifestId;
        reference.wcsSolutionId = "another-solution";
        reference.separationArcseconds = 0.4;
        reference.residualPixels = 0.2;
        reference.properMotionModelVersion = an::ui::processes::kSpccProperMotionModelVersion;
        recorded.references.push_back(std::move(reference));
    }
    writeHandoffDatabase(recorded);
    const std::vector<double> before = documentValues(target->document());
    WindowHandle<SpectrophotometricColorCalibrationProcess> window = openSpectrophotometric(*target);
    ASSERT_TRUE(applySpectrophotometric(*window, *target));
    ASSERT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const QString text = widgetOf<QPlainTextEdit>(*window, "spccDiagnosticsText")->toPlainText();
    EXPECT_TRUE(text.contains(QStringLiteral("accepted 0 of 80"))) << text.toStdString();
    EXPECT_TRUE(text.contains(QStringLiteral("running the normal WCS region-query fallback"))) << text.toStdString();
    expectCalibratedBy(field(), before, documentValues(target->document()),
                       expectedSpectrophotometricGains(kRedCast, kBlueCast), 0.02);
}

// response profile: a camera no alias names leaves the choice to the person; the profile they choose is used, and is
// remembered for that camera, so a window opened again resolves it.
TEST_F(SpectrophotometricWindowTest, AProfileChosenByHandIsUsedAndRemembered)
{
    FieldMetadataOptions unnamed;
    unnamed.camera = "Other Camera";
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field(), unnamed));
    ASSERT_NE(target, nullptr);
    {
        WindowHandle<SpectrophotometricColorCalibrationProcess> window = openSpectrophotometric(*target);
        const an::ui::processes::SpccProcessReadinessReport& report = reportOf(*window);
        EXPECT_FALSE(report.responseProfileResolved);
        EXPECT_FALSE(report.canExecute());
        EXPECT_TRUE(report.availableResponseProfileIds.contains(QString::fromLatin1(kSpccProfileId)));
        EXPECT_FALSE(applyButtonOf(*window)->isEnabled());
        // The repository's profile names its sensor, so the person picks the sensor and the profile follows.
        auto* sensors = widgetOf<QComboBox>(*window, "spccProcessResponseSensorCombo");
        ASSERT_NE(sensors, nullptr);
        ASSERT_EQ(sensors->count(), 2) << "a prompt and the one sensor";
        sensors->setCurrentIndex(1);
        ASSERT_TRUE(settleReadiness(*window));
        EXPECT_TRUE(reportOf(*window).canExecute()) << reportOf(*window).summary.toStdString();
        EXPECT_EQ(reportOf(*window).responseProfileId.toStdString(), kSpccProfileId);
        ASSERT_TRUE(applySpectrophotometric(*window, *target));
        ASSERT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        const std::optional<an::ImageMetadata> metadata = documentMetadata(target->document());
        ASSERT_TRUE(metadata.has_value());
        EXPECT_EQ(an::readColorCalibrationCards(metadata->codec()).record.responseProfileId, kSpccProfileId);
    }
    ViewHandle again = fieldView<float>(session(), field(), fieldMetadata(field(), unnamed), "again");
    ASSERT_NE(again, nullptr);
    WindowHandle<SpectrophotometricColorCalibrationProcess> window = openSpectrophotometric(*again);
    const an::ui::processes::SpccProcessReadinessReport& report = reportOf(*window);
    EXPECT_TRUE(report.responseProfileResolved);
    EXPECT_TRUE(report.responseProfileSavedMapping);
    EXPECT_EQ(report.responseProfileId.toStdString(), kSpccProfileId);
    EXPECT_TRUE(report.canExecute()) << report.summary.toStdString();
}

// response profile: an alias names a camera and a filter, so an image of the camera taken through a filter the alias
// does not name is left to the person.
TEST_F(SpectrophotometricWindowTest, AnAliasNamesItsFilterToo)
{
    FieldMetadataOptions filtered;
    filtered.filter = "L";
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field(), filtered));
    ASSERT_NE(target, nullptr);
    WindowHandle<SpectrophotometricColorCalibrationProcess> window = openSpectrophotometric(*target);
    const an::ui::processes::SpccProcessReadinessReport& report = reportOf(*window);
    EXPECT_EQ(report.responseProfileCameraModel.toStdString(), kSpccCameraModel);
    EXPECT_FALSE(report.responseProfileResolved);
    EXPECT_FALSE(report.canExecute());
}

// white reference: the person may choose another of the repository's white references; the readiness, the run and the
// result's cards all name the one chosen, and its colour moves the gains. The image names no camera, so nothing is
// remembered and the window's own choice is what reaches the run.
TEST_F(SpectrophotometricWindowTest, AWhiteReferenceChosenByHandIsTheOneCalibratedAgainst)
{
    FieldMetadataOptions unnamed;
    unnamed.camera.clear();
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field(), unnamed));
    ASSERT_NE(target, nullptr);
    WindowHandle<SpectrophotometricColorCalibrationProcess> window = openSpectrophotometric(*target);
    auto* sensors = widgetOf<QComboBox>(*window, "spccProcessResponseSensorCombo");
    ASSERT_NE(sensors, nullptr);
    ASSERT_EQ(sensors->count(), 2) << "a prompt and the one sensor";
    sensors->setCurrentIndex(1);
    ASSERT_TRUE(settleReadiness(*window));
    EXPECT_EQ(reportOf(*window).whiteReferenceId.toStdString(), kSpccWhiteReferenceId);
    auto* whites = widgetOf<QComboBox>(*window, "spccProcessWhiteReferenceCombo");
    ASSERT_NE(whites, nullptr);
    const int warm = whites->findData(QString::fromLatin1(kSpccWarmWhiteReferenceId));
    ASSERT_GE(warm, 0);
    whites->setCurrentIndex(warm);
    ASSERT_TRUE(settleReadiness(*window));
    EXPECT_EQ(reportOf(*window).whiteReferenceId.toStdString(), kSpccWarmWhiteReferenceId);
    EXPECT_FALSE(reportOf(*window).responseProfileSavedMapping);
    ASSERT_TRUE(applySpectrophotometric(*window, *target));
    ASSERT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::optional<an::ImageMetadata> metadata = documentMetadata(target->document());
    ASSERT_TRUE(metadata.has_value());
    const an::ColorCalibrationReading reading = an::readColorCalibrationCards(metadata->codec());
    EXPECT_EQ(reading.record.whiteReferenceId, kSpccWarmWhiteReferenceId);
    const std::array<double, 3u> flat = expectedSpectrophotometricGains(kRedCast, kBlueCast);
    const double flatRatio = flat[0] / flat[2];
    const double warmRatio = reading.record.gains[0] / reading.record.gains[2];
    EXPECT_GT(std::abs(std::log(warmRatio / flatRatio)), std::log(1.2)) << "the white's colour moves the gains";
}

// response profile: an image that names no camera leaves the choice to the person each time - the profile chosen is the
// window's for its run, and nothing is remembered for a camera nobody named.
TEST_F(SpectrophotometricWindowTest, AProfileChosenForAnUnnamedCameraIsTheWindowsOwn)
{
    FieldMetadataOptions unnamed;
    unnamed.camera.clear();
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field(), unnamed));
    ASSERT_NE(target, nullptr);
    {
        WindowHandle<SpectrophotometricColorCalibrationProcess> window = openSpectrophotometric(*target);
        EXPECT_FALSE(reportOf(*window).responseProfileResolved);
        auto* sensors = widgetOf<QComboBox>(*window, "spccProcessResponseSensorCombo");
        ASSERT_NE(sensors, nullptr);
        ASSERT_EQ(sensors->count(), 2) << "a prompt and the one sensor";
        sensors->setCurrentIndex(1);
        ASSERT_TRUE(settleReadiness(*window));
        EXPECT_TRUE(reportOf(*window).canExecute()) << reportOf(*window).summary.toStdString();
        EXPECT_FALSE(reportOf(*window).responseProfileSavedMapping);
        ASSERT_TRUE(applySpectrophotometric(*window, *target));
        ASSERT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        const std::optional<an::ImageMetadata> metadata = documentMetadata(target->document());
        ASSERT_TRUE(metadata.has_value());
        EXPECT_EQ(an::readColorCalibrationCards(metadata->codec()).record.responseProfileId, kSpccProfileId);
    }
    ViewHandle again = fieldView<float>(session(), field(), fieldMetadata(field(), unnamed), "again");
    ASSERT_NE(again, nullptr);
    WindowHandle<SpectrophotometricColorCalibrationProcess> window = openSpectrophotometric(*again);
    EXPECT_FALSE(reportOf(*window).responseProfileResolved) << "nothing was remembered";
    EXPECT_FALSE(reportOf(*window).canExecute());
}

// sequence: an Apply over a sequence calibrates every frame as a still image of it - each frame needs its own plate
// solution - so a recording whose frames carry none is refused by name, writes no sequence and leaves the view on the
// recording it showed.
TEST_F(SpectrophotometricWindowTest, ASequenceWhoseFramesCarryNoSolutionIsRefused)
{
    QTemporaryDir recordings;
    ASSERT_TRUE(recordings.isValid());
    const QString path = recordings.path() + QStringLiteral("/frames.ser");
    writeFieldRecording(session(), path, field(), 2u);
    ViewHandle target = fieldSequenceView(session(), path, field(), fieldMetadata(field()), 2u);
    ASSERT_NE(target, nullptr);
    WindowHandle<SpectrophotometricColorCalibrationProcess> window = openSpectrophotometric(*target);
    ASSERT_TRUE(reportOf(*window).canExecute()) << "the frame shown carries the solution";
    ASSERT_TRUE(applySpectrophotometric(*window, *target));
    EXPECT_EQ(window->runTelemetry().runsCommitted, 0u);
    ASSERT_TRUE(pumpUntil([this]() { return context().errorCount() == 1; })) << "the window says why";
    EXPECT_TRUE(context().errorTexts.back().contains(QStringLiteral("SPCC requires a plate-solved image.")))
        << context().errorTexts.back().toStdString();
    const std::optional<an::ui::DocumentSequence> sequence = target->document().sequence();
    ASSERT_TRUE(sequence.has_value());
    EXPECT_EQ(sequence->sourcePath, path);
    EXPECT_TRUE(context().madeViews.empty());
    window.reset();
    dropView(context(), target);
}
