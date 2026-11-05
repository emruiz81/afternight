// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QPointer>
#include <QScreen>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QWidget>

#include <algorithm>
#include <optional>

#include "components/AnCollapsibleSection.h"
#include "components/AnInfoBanner.h"
#include "controls/IAppContext.h"
#include "processes/ImageIntegration/core/ParallelExecutionUtils.h"
#include "processes/ImageIntegration/dialogs/WorkflowProgressDialogV2.h"
#include "processes/ImageIntegration/lite/LiteIntegrationProfile.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegrationLiteProcess.h"
#include "processes/ImageIntegrationLiteProcessUiBuilder.h"

#include "Test_ImageIntegration/ui/ProcessWindowContext.h"

namespace
{
FrameInfo makeLiteUiLightFrame(const QString& filename, const QString& filter = QStringLiteral("L"),
                               const QString& object = QStringLiteral("M42"))
{
    const QString path = QDir::tempPath() + QDir::separator() + filename;
    const FrameMetadata metadata(FrameType::Light, QSize(640, 480), 120.0, std::nullopt, 100, 10, filter, object,
                                 QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QStringLiteral("Default"));
    return FrameInfo(FrameId(path), path, metadata, QStringLiteral("2026-01-01"), QString());
}

FrameInfo makeLiteUiTypedFrame(const FrameType type, const QString& filename,
                               const QString& filter = QStringLiteral("L"),
                               const QString& object = QStringLiteral("M42"), const double exposure = 120.0,
                               const QString& customTag = {})
{
    const QString path = QDir::tempPath() + QDir::separator() + filename;
    const FrameMetadata metadata(type, QSize(640, 480), exposure, std::nullopt, 100, 10, filter, object,
                                 QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC), QStringLiteral("Default"));
    return FrameInfo(FrameId(path), path, metadata, QStringLiteral("2026-01-01"), customTag);
}

void processEvents()
{
    QApplication::processEvents(QEventLoop::AllEvents, 50);
}

void resetLiteProcessSettings()
{
    const QString processName = QString::fromStdString(ImageIntegrationLiteProcess::getProcessNameStatic());
    if (auto* settings = an::ui::appContext()->getProcessSettings())
    {
        settings->upsert(processName, Qt::WindowNoState, -1, -1, QString());
    }
    if (auto* pathMemory = an::ui::appContext()->getPathMemoryService())
    {
        pathMemory->upsertPath(QStringLiteral("__IMAGE_INTEGRATION_LITE_ADD_FILES_DIRECTORY__"), QString());
        pathMemory->upsertPath(QStringLiteral("__IMAGE_INTEGRATION_LITE_ADD_FOLDER_DIRECTORY__"), QString());
        pathMemory->upsertPath(QStringLiteral("__IMAGE_INTEGRATION_LITE_OUTPUT_DIRECTORY__"), QString());
    }
}

QFileDialog* findOpenFileDialog(QWidget& parent, const QString& title)
{
    const auto dialogs = parent.findChildren<QFileDialog*>();
    for (QFileDialog* dialog : dialogs)
    {
        if (dialog && dialog->windowTitle() == title)
        {
            return dialog;
        }
    }

    return nullptr;
}

void expectHelpfulTooltip(const QWidget* widget, const char* controlName)
{
    ASSERT_NE(widget, nullptr) << controlName;
    const QString tooltip = widget->toolTip().trimmed();
    EXPECT_FALSE(tooltip.isEmpty()) << controlName;
    EXPECT_GE(tooltip.size(), 24) << controlName;
}

void expectComboItemTooltips(const QComboBox* combo, const char* controlName)
{
    ASSERT_NE(combo, nullptr) << controlName;
    for (int index = 0; index < combo->count(); ++index)
    {
        const QString tooltip = combo->itemData(index, Qt::ToolTipRole).toString().trimmed();
        EXPECT_FALSE(tooltip.isEmpty()) << controlName << " item " << index;
        EXPECT_GE(tooltip.size(), 24) << controlName << " item " << index;
    }
}

int longestComboItemTextWidth(const QComboBox* combo)
{
    int longest_text_width = 0;
    const QFontMetrics metrics(combo->font());
    for (int index = 0; index < combo->count(); ++index)
    {
        longest_text_width = (std::max)(longest_text_width, metrics.horizontalAdvance(combo->itemText(index)));
    }
    return longest_text_width;
}

int comboEditFieldWidth(const QComboBox* combo, const int comboWidth)
{
    QStyleOptionComboBox option;
    option.initFrom(combo);
    option.editable = combo->isEditable();
    option.frame = combo->hasFrame();
    option.rect = QRect(0, 0, comboWidth, combo->sizeHint().height());

    return combo->style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, combo).width();
}
} // namespace

// Every case runs under an application context of its own: the window registers itself, keeps its settings and
// directories in it and reads its frames in its session.
class ImageIntegrationLiteProcessUi : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_scope.context().valid());
    }

    an::tests::integration::ScopedProcessWindowContext m_scope;
};

TEST_F(ImageIntegrationLiteProcessUi, BuilderDefaultsMatchLiteContract)
{
    QWidget host;
    const auto ui = an::ui::processes::buildImageIntegrationLiteProcessUi(&host);

    ASSERT_NE(ui.statusBanner, nullptr);
    ASSERT_NE(ui.summaryCard, nullptr);
    ASSERT_NE(ui.setupCard, nullptr);
    ASSERT_NE(ui.settingsCard, nullptr);
    ASSERT_NE(ui.framesCard, nullptr);
    ASSERT_NE(ui.calibrationSection, nullptr);
    ASSERT_NE(ui.registrationSection, nullptr);
    ASSERT_NE(ui.normalizationSection, nullptr);
    ASSERT_NE(ui.stackingSection, nullptr);
    ASSERT_NE(ui.stackMethodCombo, nullptr);
    ASSERT_NE(ui.rejectionMethodCombo, nullptr);
    ASSERT_NE(ui.rejectionLowLabel, nullptr);
    ASSERT_NE(ui.rejectionLowSpin, nullptr);
    ASSERT_NE(ui.rejectionHighLabel, nullptr);
    ASSERT_NE(ui.rejectionHighSpin, nullptr);
    ASSERT_NE(ui.weightingCombo, nullptr);
    ASSERT_NE(ui.transformationCombo, nullptr);
    ASSERT_NE(ui.autoReferenceCheck, nullptr);
    ASSERT_NE(ui.autoCropCheck, nullptr);
    ASSERT_NE(ui.normalizationCombo, nullptr);
    ASSERT_NE(ui.calibrationCombo, nullptr);
    ASSERT_NE(ui.darkOptimizationCheck, nullptr);
    ASSERT_NE(ui.flatAutoLevelCheck, nullptr);
    ASSERT_NE(ui.solveFinalMasterCheck, nullptr);
    ASSERT_NE(ui.drizzleEnabledCheck, nullptr);
    ASSERT_NE(ui.drizzleScaleCombo, nullptr);
    ASSERT_NE(ui.drizzlePixelFractionCombo, nullptr);
    ASSERT_NE(ui.openResultsCheck, nullptr);
    ASSERT_NE(ui.limitMemoryCheck, nullptr);
    ASSERT_NE(ui.maxMemorySpin, nullptr);
    ASSERT_NE(ui.maxParallelThreadsLabel, nullptr);
    ASSERT_NE(ui.maxParallelThreadsSpin, nullptr);
    ASSERT_NE(ui.importTypeCombo, nullptr);
    ASSERT_NE(ui.changeFrameTypeButton, nullptr);
    ASSERT_NE(ui.outputDirectoryEdit, nullptr);
    ASSERT_NE(ui.frameTable, nullptr);
    ASSERT_NE(ui.buttonBox, nullptr);
    ASSERT_NE(ui.runButton, nullptr);
    ASSERT_NE(ui.openFullProcessButton, nullptr);

    EXPECT_EQ(ui.statusBanner->objectName(), QStringLiteral("liteStatusBanner"));
    EXPECT_EQ(host.findChild<QComboBox*>(QStringLiteral("liteProfileCombo")), nullptr);
    EXPECT_EQ(ui.frameTable->objectName(), QStringLiteral("liteFrameTable"));
    EXPECT_EQ(ui.calibrationSection->objectName(), QStringLiteral("liteCalibrationSection"));
    EXPECT_FALSE(ui.calibrationSection->isExpanded());
    EXPECT_FALSE(ui.registrationSection->isExpanded());
    EXPECT_FALSE(ui.normalizationSection->isExpanded());
    EXPECT_FALSE(ui.stackingSection->isExpanded());
    EXPECT_FALSE(ui.autoCropCheck->isChecked());
    EXPECT_EQ(ui.normalizationCombo->currentText(), QStringLiteral("Global Additive Scaling"));
    EXPECT_EQ(ui.stackMethodCombo->currentText(), QStringLiteral("Average"));
    EXPECT_EQ(ui.rejectionMethodCombo->currentText(), QStringLiteral("Auto"));
    EXPECT_TRUE(ui.rejectionLowLabel->isHidden());
    EXPECT_TRUE(ui.rejectionLowSpin->isHidden());
    EXPECT_TRUE(ui.rejectionHighLabel->isHidden());
    EXPECT_TRUE(ui.rejectionHighSpin->isHidden());
    EXPECT_GE(ui.stackMethodCombo->findText(QStringLiteral("Sum")), 0);
    EXPECT_GE(ui.stackMethodCombo->findText(QStringLiteral("Maximum")), 0);
    EXPECT_GE(ui.stackMethodCombo->findText(QStringLiteral("Minimum")), 0);
    EXPECT_GE(ui.rejectionMethodCombo->findText(QStringLiteral("Median Sigma")), 0);
    EXPECT_GE(ui.rejectionMethodCombo->findText(QStringLiteral("Percentile")), 0);
    EXPECT_GE(ui.rejectionMethodCombo->findText(QStringLiteral("GESDT")), 0);
    EXPECT_GE(ui.rejectionMethodCombo->findText(QStringLiteral("Linear Fit")), 0);
    EXPECT_EQ(host.findChild<QComboBox*>(QStringLiteral("liteRegistrationMethodCombo")), nullptr);
    EXPECT_GE(ui.transformationCombo->findText(QStringLiteral("Polynomial 2")), 0);
    EXPECT_GE(ui.transformationCombo->findText(QStringLiteral("Polynomial 3")), 0);
    EXPECT_GE(ui.transformationCombo->findText(QStringLiteral("Polynomial 4")), 0);
    EXPECT_GE(ui.transformationCombo->findText(QStringLiteral("Auto")), 0);
    // A control the builder makes starts at the choice a Lite run makes when nobody changes it, so the window shows
    // the same choice whether or not the process has copied its options onto the controls yet.
    const ImageIntegrationLite::ProcessingOptions defaults = ImageIntegrationLite::defaultProcessingOptions();
    EXPECT_EQ(ui.solveFinalMasterCheck->isChecked(), defaults.solveFinalMasterIfNeeded);
    EXPECT_EQ(ui.darkOptimizationCheck->isChecked(), defaults.darkOptimization);
    EXPECT_EQ(ui.flatAutoLevelCheck->isChecked(), defaults.flatAutoLevel);
    EXPECT_EQ(ui.autoReferenceCheck->isChecked(), defaults.autoReference);
    EXPECT_EQ(ui.autoCropCheck->isChecked(), defaults.autoCrop);
    EXPECT_EQ(ui.drizzleEnabledCheck->isChecked(), defaults.drizzleEnabled);
    EXPECT_EQ(ui.limitMemoryCheck->isChecked(), defaults.limitMemory);
    EXPECT_FALSE(ui.drizzleEnabledCheck->isChecked());
    EXPECT_EQ(ui.drizzleScaleCombo->currentData().toDouble(), 2.0);
    EXPECT_EQ(ui.drizzlePixelFractionCombo->currentData().toDouble(), 0.9);
    EXPECT_TRUE(ui.openResultsCheck->isChecked());
    EXPECT_FALSE(ui.limitMemoryCheck->isChecked());
    EXPECT_FALSE(ui.maxMemorySpin->isEnabled());
    EXPECT_EQ(ui.maxMemorySpin->minimum(), 512);
    EXPECT_EQ(ui.maxMemorySpin->maximum(), 1048576);
    EXPECT_EQ(ui.maxMemorySpin->value(), 2048);
    EXPECT_EQ(ui.maxMemorySpin->suffix(), QStringLiteral(" MB"));
    EXPECT_EQ(ui.maxParallelThreadsLabel->text(), QStringLiteral("CPU Threads"));
    EXPECT_EQ(ui.maxParallelThreadsSpin->minimum(), 1);
    EXPECT_EQ(ui.maxParallelThreadsSpin->maximum(), ImageIntegration::core::detectedParallelCoreCount());
    EXPECT_EQ(ui.maxParallelThreadsSpin->value(), ImageIntegration::core::detectedParallelCoreCount());
    EXPECT_TRUE(ui.maxParallelThreadsSpin->suffix().isEmpty());
    EXPECT_GE(ui.maxParallelThreadsSpin->minimumWidth(), ui.maxParallelThreadsSpin->sizeHint().width());
    EXPECT_EQ(ui.importTypeCombo->currentText(), QStringLiteral("Auto-detect"));
    EXPECT_GE(ui.importTypeCombo->findText(QStringLiteral("Dark Flat")), 0);
    EXPECT_EQ(ui.frameTable->columnCount(), 6);
    EXPECT_EQ(ui.frameTable->horizontalHeaderItem(0)->text(), QStringLiteral("Type"));
    EXPECT_EQ(ui.frameTable->horizontalHeaderItem(4)->text(), QStringLiteral("Exposure"));
    EXPECT_EQ(ui.frameTable->horizontalHeaderItem(5)->text(), QStringLiteral("Calibration"));
    EXPECT_EQ(ui.runButton->text(), QStringLiteral("Run Integration"));
    EXPECT_EQ(ui.openFullProcessButton->text(), QStringLiteral("Open Full Process"));
    EXPECT_GE(host.size().width(), 1120);
}

TEST_F(ImageIntegrationLiteProcessUi, SectionCardsUseFlatParentBackedChrome)
{
    QWidget host;
    const auto ui = an::ui::processes::buildImageIntegrationLiteProcessUi(&host);
    const QColor expectedBackdrop = an::ui::tokens().colors.bgSurface;

    const QVector<an::ui::AnCardWidget*> cards = {ui.summaryCard, ui.setupCard, ui.settingsCard, ui.framesCard};
    for (const an::ui::AnCardWidget* card : cards)
    {
        ASSERT_NE(card, nullptr);
        EXPECT_TRUE(card->property("anCardFlat").toBool());
        EXPECT_EQ(card->property("anCardBackdropColor").value<QColor>(), expectedBackdrop);
    }
}

TEST_F(ImageIntegrationLiteProcessUi, BuilderProvidesHumanFriendlyTooltips)
{
    QWidget host;
    const auto ui = an::ui::processes::buildImageIntegrationLiteProcessUi(&host);

    expectHelpfulTooltip(ui.outputDirectoryEdit, "outputDirectoryEdit");
    expectHelpfulTooltip(ui.browseOutputButton, "browseOutputButton");
    expectHelpfulTooltip(ui.reprocessExistingCheck, "reprocessExistingCheck");
    expectHelpfulTooltip(ui.openResultsCheck, "openResultsCheck");
    expectHelpfulTooltip(ui.limitMemoryCheck, "limitMemoryCheck");
    expectHelpfulTooltip(ui.maxMemorySpin, "maxMemorySpin");
    expectHelpfulTooltip(ui.maxParallelThreadsSpin, "maxParallelThreadsSpin");
    expectHelpfulTooltip(ui.calibrationSection, "calibrationSection");
    expectHelpfulTooltip(ui.calibrationCombo, "calibrationCombo");
    expectHelpfulTooltip(ui.darkOptimizationCheck, "darkOptimizationCheck");
    expectHelpfulTooltip(ui.flatAutoLevelCheck, "flatAutoLevelCheck");
    expectHelpfulTooltip(ui.registrationSection, "registrationSection");
    expectHelpfulTooltip(ui.transformationCombo, "transformationCombo");
    EXPECT_TRUE(ui.transformationCombo->toolTip().contains(QStringLiteral("Modes:")));
    EXPECT_TRUE(ui.transformationCombo->toolTip().contains(QStringLiteral("Similarity")));
    EXPECT_TRUE(ui.transformationCombo->toolTip().contains(QStringLiteral("Polynomial 2/3/4")));
    EXPECT_TRUE(ui.transformationCombo->toolTip().contains(QStringLiteral("Auto starts with Homography")));
    expectHelpfulTooltip(ui.autoReferenceCheck, "autoReferenceCheck");
    expectHelpfulTooltip(ui.autoCropCheck, "autoCropCheck");
    expectHelpfulTooltip(ui.normalizationSection, "normalizationSection");
    expectHelpfulTooltip(ui.normalizationCombo, "normalizationCombo");
    expectHelpfulTooltip(ui.calibrationNormalizationLabel, "calibrationNormalizationLabel");
    expectHelpfulTooltip(ui.stackingSection, "stackingSection");
    expectHelpfulTooltip(ui.stackMethodCombo, "stackMethodCombo");
    expectHelpfulTooltip(ui.rejectionMethodCombo, "rejectionMethodCombo");
    expectHelpfulTooltip(ui.rejectionLowSpin, "rejectionLowSpin");
    expectHelpfulTooltip(ui.rejectionHighSpin, "rejectionHighSpin");
    expectHelpfulTooltip(ui.weightingCombo, "weightingCombo");
    expectHelpfulTooltip(ui.solveFinalMasterCheck, "solveFinalMasterCheck");
    expectHelpfulTooltip(ui.drizzleEnabledCheck, "drizzleEnabledCheck");
    expectHelpfulTooltip(ui.drizzleScaleCombo, "drizzleScaleCombo");
    expectHelpfulTooltip(ui.drizzlePixelFractionCombo, "drizzlePixelFractionCombo");
    expectHelpfulTooltip(ui.importTypeCombo, "importTypeCombo");
    expectHelpfulTooltip(ui.addFilesButton, "addFilesButton");
    expectHelpfulTooltip(ui.addFolderButton, "addFolderButton");
    expectHelpfulTooltip(ui.changeFrameTypeButton, "changeFrameTypeButton");
    expectHelpfulTooltip(ui.clearAllButton, "clearAllButton");
    expectHelpfulTooltip(ui.frameTable, "frameTable");
    expectHelpfulTooltip(ui.runButton, "runButton");
    expectHelpfulTooltip(ui.openFullProcessButton, "openFullProcessButton");
    expectHelpfulTooltip(ui.buttonBox->button(QDialogButtonBox::Close), "closeButton");

    expectComboItemTooltips(ui.calibrationCombo, "calibrationCombo");
    expectComboItemTooltips(ui.transformationCombo, "transformationCombo");
    expectComboItemTooltips(ui.normalizationCombo, "normalizationCombo");
    expectComboItemTooltips(ui.stackMethodCombo, "stackMethodCombo");
    expectComboItemTooltips(ui.rejectionMethodCombo, "rejectionMethodCombo");
    expectComboItemTooltips(ui.weightingCombo, "weightingCombo");
    expectComboItemTooltips(ui.drizzleScaleCombo, "drizzleScaleCombo");
    expectComboItemTooltips(ui.drizzlePixelFractionCombo, "drizzlePixelFractionCombo");
    expectComboItemTooltips(ui.importTypeCombo, "importTypeCombo");

    EXPECT_TRUE(ui.importTypeCombo->toolTip().contains(QStringLiteral("metadata")));
    EXPECT_TRUE(ui.drizzleEnabledCheck->toolTip().contains(QStringLiteral("dithered")));
    EXPECT_TRUE(ui.solveFinalMasterCheck->toolTip().contains(QStringLiteral("final integrated")));
    EXPECT_TRUE(ui.flatAutoLevelCheck->toolTip().contains(QStringLiteral("master flat")));
}

TEST_F(ImageIntegrationLiteProcessUi, ImportTypeComboKeepsEnoughWidthForAllChoices)
{
    QWidget host;
    const auto ui = an::ui::processes::buildImageIntegrationLiteProcessUi(&host);

    ASSERT_NE(ui.importTypeCombo, nullptr);
    EXPECT_GE(ui.importTypeCombo->minimumWidth(), ui.importTypeCombo->sizeHint().width());
    EXPECT_GE(comboEditFieldWidth(ui.importTypeCombo, ui.importTypeCombo->minimumWidth()),
              longestComboItemTextWidth(ui.importTypeCombo) + an::ui::tokens().spacing.sm);
}

// The Auto-detect choice says what the frame detection reads - the header, then a folder on the path, then a light
// frame - and promises nothing it does not read: it said it fell back to filename hints, and no file name is read.
TEST_F(ImageIntegrationLiteProcessUi, AutoDetectChoiceSaysWhatTheFrameDetectionReads)
{
    QWidget host;
    const auto ui = an::ui::processes::buildImageIntegrationLiteProcessUi(&host);
    ASSERT_NE(ui.importTypeCombo, nullptr);
    const int autoDetect = ui.importTypeCombo->findData(-1);
    ASSERT_GE(autoDetect, 0);
    const QString tooltip = ui.importTypeCombo->itemData(autoDetect, Qt::ToolTipRole).toString();

    EXPECT_FALSE(tooltip.contains(QStringLiteral("filename"), Qt::CaseInsensitive)) << tooltip.toStdString();
    EXPECT_FALSE(tooltip.contains(QStringLiteral("file name"), Qt::CaseInsensitive)) << tooltip.toStdString();
    EXPECT_TRUE(tooltip.contains(QStringLiteral("header"), Qt::CaseInsensitive)) << tooltip.toStdString();
    EXPECT_TRUE(tooltip.contains(QStringLiteral("folder"), Qt::CaseInsensitive)) << tooltip.toStdString();
    EXPECT_TRUE(tooltip.contains(QStringLiteral("light frame"), Qt::CaseInsensitive)) << tooltip.toStdString();
    EXPECT_TRUE(ui.importTypeCombo->toolTip().contains(QStringLiteral("folder"), Qt::CaseInsensitive))
        << ui.importTypeCombo->toolTip().toStdString();
}

TEST_F(ImageIntegrationLiteProcessUi, RejectionControlsFollowStackingMethodAndSelection)
{
    ImageIntegrationLiteProcess process;
    auto* stackCombo = process.findChild<QComboBox*>(QStringLiteral("liteStackMethodCombo"));
    auto* rejectionCombo = process.findChild<QComboBox*>(QStringLiteral("liteRejectionMethodCombo"));
    auto* lowLabel = process.findChild<QLabel*>(QStringLiteral("liteRejectionLowLabel"));
    auto* lowSpin = process.findChild<QDoubleSpinBox*>(QStringLiteral("liteRejectionLowSpin"));
    auto* highLabel = process.findChild<QLabel*>(QStringLiteral("liteRejectionHighLabel"));
    auto* highSpin = process.findChild<QDoubleSpinBox*>(QStringLiteral("liteRejectionHighSpin"));
    ASSERT_NE(stackCombo, nullptr);
    ASSERT_NE(rejectionCombo, nullptr);
    ASSERT_NE(lowLabel, nullptr);
    ASSERT_NE(lowSpin, nullptr);
    ASSERT_NE(highLabel, nullptr);
    ASSERT_NE(highSpin, nullptr);

    EXPECT_TRUE(rejectionCombo->isEnabled());
    EXPECT_TRUE(lowSpin->isHidden());
    EXPECT_TRUE(highSpin->isHidden());

    rejectionCombo->setCurrentText(QStringLiteral("Linear Fit"));
    processEvents();
    EXPECT_FALSE(lowSpin->isHidden());
    EXPECT_FALSE(highSpin->isHidden());
    EXPECT_EQ(lowLabel->text(), QStringLiteral("Linear Low"));
    EXPECT_EQ(highLabel->text(), QStringLiteral("Linear High"));
    EXPECT_DOUBLE_EQ(lowSpin->value(), 5.0);
    EXPECT_DOUBLE_EQ(highSpin->value(), 4.0);

    stackCombo->setCurrentText(QStringLiteral("Median"));
    processEvents();
    EXPECT_FALSE(rejectionCombo->isEnabled());
    EXPECT_TRUE(lowSpin->isHidden());
    EXPECT_TRUE(highSpin->isHidden());
}

TEST_F(ImageIntegrationLiteProcessUi, RestoresPersistedOutputDirectoryAndWindowSize)
{
    QTemporaryDir outputDir;
    ASSERT_TRUE(outputDir.isValid());
    resetLiteProcessSettings();

    constexpr int persistedWidth = 980;
    constexpr int persistedHeight = 660;
    const QString jsonSettings =
        QStringLiteral(R"({"version":1,"params":{"outputDirectory":"%1"}})").arg(outputDir.path());
    auto* settings = an::ui::appContext()->getProcessSettings();
    ASSERT_NE(settings, nullptr);
    ASSERT_TRUE(settings->upsert(QString::fromStdString(ImageIntegrationLiteProcess::getProcessNameStatic()),
                                 Qt::WindowNoState, persistedWidth, persistedHeight, jsonSettings));

    ImageIntegrationLiteProcess process;

    QSize expectedSize(persistedWidth, persistedHeight);
    if (const QScreen* screen = QGuiApplication::primaryScreen())
    {
        expectedSize = expectedSize.boundedTo(screen->availableGeometry().size());
    }
    expectedSize = expectedSize.expandedTo(process.minimumSize());

    EXPECT_EQ(QDir::cleanPath(process.model()->outputDirectory()), QDir::cleanPath(outputDir.path()));
    EXPECT_EQ(process.size(), expectedSize);

    resetLiteProcessSettings();
}

TEST_F(ImageIntegrationLiteProcessUi, PersistsOutputDirectoryAndWindowSizeOnClose)
{
    QTemporaryDir outputDir;
    ASSERT_TRUE(outputDir.isValid());
    resetLiteProcessSettings();

    constexpr int expectedWidth = 1040;
    constexpr int expectedHeight = 680;
    QPointer<ImageIntegrationLiteProcess> process = new ImageIntegrationLiteProcess();
    ASSERT_NE(process, nullptr);
    process->model()->setOutputDirectory(outputDir.path());
    process->resize(expectedWidth, expectedHeight);

    process->close();
    processEvents();

    int winState = Qt::WindowNoState;
    int width = -1;
    int height = -1;
    QString jsonSettings;
    auto* settings = an::ui::appContext()->getProcessSettings();
    ASSERT_NE(settings, nullptr);
    ASSERT_TRUE(settings->select(QString::fromStdString(ImageIntegrationLiteProcess::getProcessNameStatic()), winState,
                                 width, height, jsonSettings));

    EXPECT_EQ(width, expectedWidth);
    EXPECT_EQ(height, expectedHeight);
    EXPECT_TRUE(jsonSettings.contains(outputDir.path()));

    resetLiteProcessSettings();
}

TEST_F(ImageIntegrationLiteProcessUi, FileDialogsUseRememberedDirectories)
{
    QTemporaryDir addFilesDir;
    QTemporaryDir addFolderDir;
    QTemporaryDir outputDir;
    ASSERT_TRUE(addFilesDir.isValid());
    ASSERT_TRUE(addFolderDir.isValid());
    ASSERT_TRUE(outputDir.isValid());
    resetLiteProcessSettings();

    auto* pathMemory = an::ui::appContext()->getPathMemoryService();
    ASSERT_NE(pathMemory, nullptr);
    ASSERT_TRUE(
        pathMemory->upsertPath(QStringLiteral("__IMAGE_INTEGRATION_LITE_ADD_FILES_DIRECTORY__"), addFilesDir.path()));
    ASSERT_TRUE(
        pathMemory->upsertPath(QStringLiteral("__IMAGE_INTEGRATION_LITE_ADD_FOLDER_DIRECTORY__"), addFolderDir.path()));
    ASSERT_TRUE(
        pathMemory->upsertPath(QStringLiteral("__IMAGE_INTEGRATION_LITE_OUTPUT_DIRECTORY__"), outputDir.path()));

    ImageIntegrationLiteProcess process;
    auto* addFilesButton = process.findChild<QPushButton*>(QStringLiteral("liteAddFilesButton"));
    auto* addFolderButton = process.findChild<QPushButton*>(QStringLiteral("liteAddFolderButton"));
    auto* browseOutputButton = process.findChild<QPushButton*>(QStringLiteral("liteBrowseOutputButton"));
    ASSERT_NE(addFilesButton, nullptr);
    ASSERT_NE(addFolderButton, nullptr);
    ASSERT_NE(browseOutputButton, nullptr);

    addFilesButton->click();
    auto* addFilesDialog = findOpenFileDialog(process, QStringLiteral("Add Frames"));
    ASSERT_NE(addFilesDialog, nullptr);
    EXPECT_EQ(QDir::cleanPath(addFilesDialog->directory().absolutePath()), QDir::cleanPath(addFilesDir.path()));
    const QString addFilesFilters = addFilesDialog->nameFilters().join(QStringLiteral(";;"));
    EXPECT_TRUE(addFilesFilters.contains(QStringLiteral("*.fits")));
    EXPECT_TRUE(addFilesFilters.contains(QStringLiteral("*.fts")));
    EXPECT_TRUE(addFilesFilters.contains(QStringLiteral("*.jpe")));
    EXPECT_TRUE(addFilesFilters.contains(QStringLiteral("*.cr2")));
    EXPECT_TRUE(addFilesFilters.contains(QStringLiteral("*.raw")));
    addFilesDialog->reject();
    processEvents();

    addFolderButton->click();
    auto* addFolderDialog = findOpenFileDialog(process, QStringLiteral("Add Folder"));
    ASSERT_NE(addFolderDialog, nullptr);
    EXPECT_EQ(QDir::cleanPath(addFolderDialog->directory().absolutePath()), QDir::cleanPath(addFolderDir.path()));
    addFolderDialog->reject();
    processEvents();

    browseOutputButton->click();
    auto* outputDialog = findOpenFileDialog(process, QStringLiteral("Output Directory"));
    ASSERT_NE(outputDialog, nullptr);
    EXPECT_EQ(QDir::cleanPath(outputDialog->directory().absolutePath()), QDir::cleanPath(outputDir.path()));
    outputDialog->reject();

    resetLiteProcessSettings();
}

TEST_F(ImageIntegrationLiteProcessUi, ProcessStartsBlockedAndFollowsModelReadiness)
{
    QTemporaryDir outputDir;
    ASSERT_TRUE(outputDir.isValid());

    ImageIntegrationLiteProcess process;
    auto* runButton = process.findChild<QPushButton*>(QStringLiteral("liteRunIntegrationButton"));
    auto* bannerWidget = process.findChild<QWidget*>(QStringLiteral("liteStatusBanner"));
    auto* banner = dynamic_cast<an::ui::AnInfoBanner*>(bannerWidget);
    auto* frameSummary = process.findChild<QLabel*>(QStringLiteral("liteFrameSummaryLabel"));
    auto* filterSummary = process.findChild<QLabel*>(QStringLiteral("liteFilterSummaryLabel"));
    auto* objectSummary = process.findChild<QLabel*>(QStringLiteral("liteObjectSummaryLabel"));
    auto* frameTable = process.findChild<QTableWidget*>(QStringLiteral("liteFrameTable"));

    ASSERT_NE(runButton, nullptr);
    ASSERT_NE(banner, nullptr);
    ASSERT_NE(frameSummary, nullptr);
    ASSERT_NE(filterSummary, nullptr);
    ASSERT_NE(objectSummary, nullptr);
    ASSERT_NE(frameTable, nullptr);

    EXPECT_FALSE(runButton->isEnabled());
    EXPECT_EQ(banner->tone(), an::ui::AnInfoBanner::Tone::Error);

    process.model()->setOutputDirectory(outputDir.path());
    process.model()->addFrames(FrameType::Light,
                               {makeLiteUiLightFrame(QStringLiteral("lite_l_1.fit"), QStringLiteral("L")),
                                makeLiteUiLightFrame(QStringLiteral("lite_ha_1.fit"), QStringLiteral("Ha"))});
    processEvents();

    EXPECT_TRUE(runButton->isEnabled());
    EXPECT_EQ(frameSummary->text(), QStringLiteral("2 light, 0 flat, 0 dark, 0 bias"));
    EXPECT_EQ(filterSummary->text(), QStringLiteral("Ha, L"));
    EXPECT_EQ(objectSummary->text(), QStringLiteral("M42"));
    EXPECT_EQ(frameTable->rowCount(), 2);
    EXPECT_EQ(banner->tone(), an::ui::AnInfoBanner::Tone::Warning);
    EXPECT_TRUE(banner->message().contains(QStringLiteral("No calibration frames")));
}

TEST_F(ImageIntegrationLiteProcessUi, AddingCalibrationFramesClearsNoCalibrationWarning)
{
    QTemporaryDir outputDir;
    ASSERT_TRUE(outputDir.isValid());

    ImageIntegrationLiteProcess process;
    auto* bannerWidget = process.findChild<QWidget*>(QStringLiteral("liteStatusBanner"));
    auto* banner = dynamic_cast<an::ui::AnInfoBanner*>(bannerWidget);
    ASSERT_NE(banner, nullptr);

    process.model()->setOutputDirectory(outputDir.path());
    process.model()->addFrames(FrameType::Light,
                               {makeLiteUiLightFrame(QStringLiteral("lite_cal_l_1.fit"), QStringLiteral("L")),
                                makeLiteUiLightFrame(QStringLiteral("lite_cal_l_2.fit"), QStringLiteral("L"))});
    processEvents();

    EXPECT_EQ(banner->tone(), an::ui::AnInfoBanner::Tone::Warning);

    process.model()->addFrames(FrameType::Flat,
                               {makeLiteUiTypedFrame(FrameType::Flat, QStringLiteral("lite_cal_flat.fit"),
                                                     QStringLiteral("L"), QStringLiteral("FlatPanel"), 3.0)});
    processEvents();

    EXPECT_FALSE(banner->message().contains(QStringLiteral("No calibration frames")));
    EXPECT_TRUE(banner->toolTip().contains(QStringLiteral("Flat frames have no bias or Dark Flat calibration")));
}

TEST_F(ImageIntegrationLiteProcessUi, WarningBannerTooltipListsAllWarnings)
{
    QTemporaryDir outputDir;
    ASSERT_TRUE(outputDir.isValid());

    ImageIntegrationLiteProcess process;
    auto* bannerWidget = process.findChild<QWidget*>(QStringLiteral("liteStatusBanner"));
    auto* banner = dynamic_cast<an::ui::AnInfoBanner*>(bannerWidget);
    ASSERT_NE(banner, nullptr);

    process.model()->setOutputDirectory(outputDir.path());
    process.model()->addFrames(FrameType::Light,
                               {makeLiteUiTypedFrame(FrameType::Light, QStringLiteral("lite_warn_l_1.fit"),
                                                     QStringLiteral("L"), QStringLiteral("M42")),
                                makeLiteUiTypedFrame(FrameType::Light, QStringLiteral("lite_warn_l_2.fit"),
                                                     QStringLiteral("L"), QStringLiteral("M42"))});
    process.model()->addFrames(FrameType::Flat,
                               {makeLiteUiTypedFrame(FrameType::Flat, QStringLiteral("lite_warn_flat.fit"),
                                                     QStringLiteral("Ha"), QStringLiteral("FlatPanel"), 3.0)});
    processEvents();

    EXPECT_EQ(banner->tone(), an::ui::AnInfoBanner::Tone::Warning);
    EXPECT_FALSE(banner->toolTip().isEmpty());
    EXPECT_TRUE(banner->toolTip().contains(QStringLiteral("Missing flat calibration match")));
}

TEST_F(ImageIntegrationLiteProcessUi, FrameTableShowsCurrentCalibrationSummary)
{
    QTemporaryDir outputDir;
    ASSERT_TRUE(outputDir.isValid());

    ImageIntegrationLiteProcess process;
    auto* frameTable = process.findChild<QTableWidget*>(QStringLiteral("liteFrameTable"));
    ASSERT_NE(frameTable, nullptr);

    process.model()->setOutputDirectory(outputDir.path());
    process.model()->addFrames(FrameType::Light,
                               {makeLiteUiTypedFrame(FrameType::Light, QStringLiteral("lite_calcol_l_1.fit"),
                                                     QStringLiteral("L"), QStringLiteral("M42")),
                                makeLiteUiTypedFrame(FrameType::Light, QStringLiteral("lite_calcol_l_2.fit"),
                                                     QStringLiteral("L"), QStringLiteral("M42"))});
    process.model()->addFrames(FrameType::Flat,
                               {makeLiteUiTypedFrame(FrameType::Flat, QStringLiteral("lite_calcol_flat.fit"),
                                                     QStringLiteral("L"), QStringLiteral("FlatPanel"), 3.0)});
    processEvents();

    ASSERT_EQ(frameTable->columnCount(), 6);
    ASSERT_EQ(frameTable->rowCount(), 3);

    bool sawFlat = false;
    bool sawLightWithFlat = false;
    for (int row = 0; row < frameTable->rowCount(); ++row)
    {
        const QString type = frameTable->item(row, 0)->text();
        const QString calibration = frameTable->item(row, 5)->text();
        if (type == QStringLiteral("Flat"))
        {
            sawFlat = true;
            EXPECT_EQ(calibration, QStringLiteral("None"));
        }
        if (type == QStringLiteral("Light"))
        {
            sawLightWithFlat = sawLightWithFlat || calibration.contains(QStringLiteral("Flat"));
            EXPECT_TRUE(frameTable->item(row, 5)->toolTip().contains(QStringLiteral("Flat")));
        }
    }

    EXPECT_TRUE(sawFlat);
    EXPECT_TRUE(sawLightWithFlat);
}

TEST_F(ImageIntegrationLiteProcessUi, CommonSettingsAndDrizzleReapplyToExistingGroups)
{
    QTemporaryDir outputDir;
    ASSERT_TRUE(outputDir.isValid());

    ImageIntegrationLiteProcess process;
    auto* normalizationCombo = process.findChild<QComboBox*>(QStringLiteral("liteNormalizationCombo"));
    auto* stackCombo = process.findChild<QComboBox*>(QStringLiteral("liteStackMethodCombo"));
    auto* transformationCombo = process.findChild<QComboBox*>(QStringLiteral("liteTransformationCombo"));
    auto* autoCropCheck = process.findChild<QCheckBox*>(QStringLiteral("liteAutoCropCheck"));
    auto* solveFinalMasterCheck = process.findChild<QCheckBox*>(QStringLiteral("liteSolveFinalMasterCheck"));
    auto* drizzleCheck = process.findChild<QCheckBox*>(QStringLiteral("liteDrizzleEnabledCheck"));
    auto* drizzleScaleCombo = process.findChild<QComboBox*>(QStringLiteral("liteDrizzleScaleCombo"));
    auto* drizzlePixelFractionCombo = process.findChild<QComboBox*>(QStringLiteral("liteDrizzlePixelFractionCombo"));
    auto* limitMemoryCheck = process.findChild<QCheckBox*>(QStringLiteral("liteLimitMemoryCheck"));
    auto* maxMemorySpin = process.findChild<QSpinBox*>(QStringLiteral("liteMaxMemorySpin"));
    ASSERT_NE(normalizationCombo, nullptr);
    ASSERT_NE(stackCombo, nullptr);
    ASSERT_NE(transformationCombo, nullptr);
    ASSERT_NE(autoCropCheck, nullptr);
    ASSERT_NE(solveFinalMasterCheck, nullptr);
    ASSERT_NE(drizzleCheck, nullptr);
    ASSERT_NE(drizzleScaleCombo, nullptr);
    ASSERT_NE(drizzlePixelFractionCombo, nullptr);
    ASSERT_NE(limitMemoryCheck, nullptr);
    ASSERT_NE(maxMemorySpin, nullptr);

    process.model()->setOutputDirectory(outputDir.path());
    process.model()->addFrames(FrameType::Light,
                               {makeLiteUiLightFrame(QStringLiteral("lite_common_l_1.fit"), QStringLiteral("L")),
                                makeLiteUiLightFrame(QStringLiteral("lite_common_l_2.fit"), QStringLiteral("L"))});
    processEvents();

    normalizationCombo->setCurrentText(QStringLiteral("Local Normalization"));
    stackCombo->setCurrentText(QStringLiteral("Median"));
    transformationCombo->setCurrentIndex(transformationCombo->findData(QStringLiteral("polynomial3")));
    autoCropCheck->setChecked(true);
    EXPECT_FALSE(solveFinalMasterCheck->isChecked());
    solveFinalMasterCheck->setChecked(true);
    drizzleCheck->setChecked(true);
    drizzleScaleCombo->setCurrentIndex(drizzleScaleCombo->findData(3.0));
    drizzlePixelFractionCombo->setCurrentIndex(drizzlePixelFractionCombo->findData(0.8));
    limitMemoryCheck->setChecked(true);
    maxMemorySpin->setValue(4096);
    processEvents();

    const auto& stackingByGroup = process.model()->stackingSettings(FrameType::Light);
    ASSERT_FALSE(stackingByGroup.empty());
    EXPECT_EQ(stackingByGroup.begin()->second.stackMethod, QStringLiteral("Median"));
    EXPECT_EQ(stackingByGroup.begin()->second.normalizationMode, NormalizationMode::Local);
    EXPECT_TRUE(stackingByGroup.begin()->second.localNormalization.enabled);
    EXPECT_TRUE(stackingByGroup.begin()->second.drizzle.enabled);
    EXPECT_DOUBLE_EQ(stackingByGroup.begin()->second.drizzle.scale, 3.0);
    EXPECT_DOUBLE_EQ(stackingByGroup.begin()->second.drizzle.pixelFraction, 0.8);
    EXPECT_TRUE(stackingByGroup.begin()->second.solveFinalMasterIfNeeded);
    EXPECT_TRUE(stackingByGroup.begin()->second.limitMemory);
    EXPECT_EQ(stackingByGroup.begin()->second.maxMemoryMb, 4096);

    const auto& registrationByGroup = process.model()->registrationSettings();
    ASSERT_FALSE(registrationByGroup.empty());
    EXPECT_EQ(registrationByGroup.begin()->second.registrationMethod, QStringLiteral("star_matching"));
    EXPECT_EQ(registrationByGroup.begin()->second.transformation, QStringLiteral("Homography"));
    EXPECT_EQ(registrationByGroup.begin()->second.transformMode, QStringLiteral("polynomial"));
    EXPECT_EQ(registrationByGroup.begin()->second.transformOrder, 3);
    EXPECT_TRUE(registrationByGroup.begin()->second.twoPassRefinementEnabled);
    EXPECT_EQ(registrationByGroup.begin()->second.alignmentMode, QStringLiteral("intersection"));
}

TEST_F(ImageIntegrationLiteProcessUi, LinearFitRejectionAppliesToExistingLightGroups)
{
    QTemporaryDir outputDir;
    ASSERT_TRUE(outputDir.isValid());

    ImageIntegrationLiteProcess process;
    auto* rejectionCombo = process.findChild<QComboBox*>(QStringLiteral("liteRejectionMethodCombo"));
    auto* lowSpin = process.findChild<QDoubleSpinBox*>(QStringLiteral("liteRejectionLowSpin"));
    auto* highSpin = process.findChild<QDoubleSpinBox*>(QStringLiteral("liteRejectionHighSpin"));
    ASSERT_NE(rejectionCombo, nullptr);
    ASSERT_NE(lowSpin, nullptr);
    ASSERT_NE(highSpin, nullptr);

    process.model()->setOutputDirectory(outputDir.path());
    process.model()->addFrames(FrameType::Light,
                               {makeLiteUiLightFrame(QStringLiteral("lite_linear_fit_l_1.fit"), QStringLiteral("L")),
                                makeLiteUiLightFrame(QStringLiteral("lite_linear_fit_l_2.fit"), QStringLiteral("L"))});
    processEvents();

    rejectionCombo->setCurrentText(QStringLiteral("Linear Fit"));
    lowSpin->setValue(4.5);
    highSpin->setValue(3.5);
    processEvents();

    const auto& stackingByGroup = process.model()->stackingSettings(FrameType::Light);
    ASSERT_FALSE(stackingByGroup.empty());
    EXPECT_EQ(stackingByGroup.begin()->second.stackMethod, QStringLiteral("Average"));
    EXPECT_EQ(stackingByGroup.begin()->second.rejectionMethod, QStringLiteral("Linear Fit"));
    EXPECT_DOUBLE_EQ(stackingByGroup.begin()->second.sigmaLow, 4.5);
    EXPECT_DOUBLE_EQ(stackingByGroup.begin()->second.sigmaHigh, 3.5);
}

TEST_F(ImageIntegrationLiteProcessUi, ChangingNonCalibrationSettingsDoesNotRebuildFrameTable)
{
    QTemporaryDir outputDir;
    ASSERT_TRUE(outputDir.isValid());

    ImageIntegrationLiteProcess process;
    auto* frameTable = process.findChild<QTableWidget*>(QStringLiteral("liteFrameTable"));
    auto* normalizationCombo = process.findChild<QComboBox*>(QStringLiteral("liteNormalizationCombo"));
    auto* autoReferenceCheck = process.findChild<QCheckBox*>(QStringLiteral("liteAutoReferenceCheck"));
    ASSERT_NE(frameTable, nullptr);
    ASSERT_NE(normalizationCombo, nullptr);
    ASSERT_NE(autoReferenceCheck, nullptr);

    process.model()->setOutputDirectory(outputDir.path());
    process.model()->addFrames(FrameType::Light,
                               {makeLiteUiLightFrame(QStringLiteral("lite_no_rebuild_l_1.fit"), QStringLiteral("L")),
                                makeLiteUiLightFrame(QStringLiteral("lite_no_rebuild_l_2.fit"), QStringLiteral("L"))});
    processEvents();

    ASSERT_EQ(frameTable->rowCount(), 2);
    auto* firstTypeItem = frameTable->item(0, 0);
    ASSERT_NE(firstTypeItem, nullptr);

    normalizationCombo->setCurrentText(QStringLiteral("Local Normalization"));
    processEvents();
    EXPECT_EQ(frameTable->item(0, 0), firstTypeItem);

    autoReferenceCheck->setChecked(false);
    processEvents();
    EXPECT_EQ(frameTable->item(0, 0), firstTypeItem);
}

TEST_F(ImageIntegrationLiteProcessUi, ExecuteOpensPreparingProgressDialogImmediately)
{
    QTemporaryDir outputDir;
    ASSERT_TRUE(outputDir.isValid());

    ImageIntegrationLiteProcess process;
    auto* runButton = process.findChild<QPushButton*>(QStringLiteral("liteRunIntegrationButton"));
    auto* bannerWidget = process.findChild<QWidget*>(QStringLiteral("liteStatusBanner"));
    auto* banner = dynamic_cast<an::ui::AnInfoBanner*>(bannerWidget);
    ASSERT_NE(runButton, nullptr);
    ASSERT_NE(banner, nullptr);

    process.model()->setOutputDirectory(outputDir.path());
    process.model()->addFrames(FrameType::Light,
                               {makeLiteUiLightFrame(QStringLiteral("lite_prepare_l_1.fit"), QStringLiteral("L")),
                                makeLiteUiLightFrame(QStringLiteral("lite_prepare_l_2.fit"), QStringLiteral("L"))});
    processEvents();
    ASSERT_TRUE(runButton->isEnabled());

    runButton->click();

    EXPECT_FALSE(runButton->isEnabled());
    EXPECT_EQ(banner->tone(), an::ui::AnInfoBanner::Tone::Info);
    EXPECT_TRUE(banner->message().contains(QStringLiteral("Preparing")));

    auto* progressDialog = process.findChild<WorkflowProgressDialogV2*>(QStringLiteral("workflowProgressDialogV2"));
    ASSERT_NE(progressDialog, nullptr);
    EXPECT_TRUE(progressDialog->isVisible());
}

TEST_F(ImageIntegrationLiteProcessUi, SelectedFramesCanBeReassignedToAnotherType)
{
    QTemporaryDir outputDir;
    ASSERT_TRUE(outputDir.isValid());

    ImageIntegrationLiteProcess process;
    auto* frameTable = process.findChild<QTableWidget*>(QStringLiteral("liteFrameTable"));
    auto* changeTypeButton = process.findChild<QPushButton*>(QStringLiteral("liteChangeFrameTypeButton"));
    ASSERT_NE(frameTable, nullptr);
    ASSERT_NE(changeTypeButton, nullptr);
    ASSERT_NE(changeTypeButton->menu(), nullptr);
    const QList<QAction*> frameTypeActions = changeTypeButton->menu()->actions();
    const bool hasDarkFlatAction =
        std::any_of(frameTypeActions.begin(), frameTypeActions.end(), [](const QAction* action) {
            return action->text() == QStringLiteral("Dark Flat") && action->data().toInt() == 4;
        });
    EXPECT_TRUE(hasDarkFlatAction);

    process.model()->setOutputDirectory(outputDir.path());
    process.model()->addFrames(FrameType::Light,
                               {makeLiteUiLightFrame(QStringLiteral("lite_retype_l_1.fit"), QStringLiteral("L")),
                                makeLiteUiLightFrame(QStringLiteral("lite_retype_l_2.fit"), QStringLiteral("L"))});
    processEvents();

    ASSERT_EQ(frameTable->rowCount(), 2);
    frameTable->selectRow(0);
    processEvents();

    QAction* darkAction = nullptr;
    for (QAction* action : changeTypeButton->menu()->actions())
    {
        if (action->data().toInt() == 1)
        {
            darkAction = action;
            break;
        }
    }
    ASSERT_NE(darkAction, nullptr);
    darkAction->trigger();
    processEvents();

    EXPECT_EQ(process.model()->lightFrames().size(), 1u);
    EXPECT_EQ(process.model()->darkFrames().size(), 1u);
    EXPECT_EQ(frameTable->rowCount(), 2);
}
