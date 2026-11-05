// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QFile>
#include <QFileInfo>
#include <QItemSelectionModel>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <filesystem>
#include <string>

#include "Test_ImageIntegration/ui/ProcessWindowContext.h"
#include "fixtures/common/IntegrationFrameFiles.h"

#include "controls/ImageViewControl.h"
#include "processes/ImageIntegration/ImageIntegrationProcess.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"
#include "processes/ImageIntegration/widgets/FrameTreeView.h"
#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"

namespace
{
FrameInfo createTestLight(const QString& path)
{
    FrameMetadata meta(FrameType::Light, QSize(1000, 1000), 60.0, std::nullopt, std::nullopt, std::nullopt,
                       QStringLiteral("L"), QStringLiteral("TestObject"), QDateTime::currentDateTimeUtc(), QString());
    // Provide an explicit session so LightGroupKey matching is stable and
    // doesn't depend on capturedAtUtc-derived date strings.
    return FrameInfo(FrameId(path), path, meta, QStringLiteral("TestSession"));
}

FrameInfo createTestFrame(FrameType type, const QString& path, const QString& filter, const QString& session)
{
    const double exposure = (type == FrameType::Bias) ? 0.0 : 60.0;
    FrameMetadata meta(type, QSize(1000, 1000), exposure, std::nullopt, std::nullopt, std::nullopt, filter,
                       QStringLiteral("TestObject"), QDateTime::currentDateTimeUtc(), QString());
    return FrameInfo(FrameId(path), path, meta, session);
}

/// Writes a small readable frame: the preview reads the light it shows, and a light it cannot read offers no calibrated
/// preview.
void writePlaceholderFile(const QString& path)
{
    std::string name;
    for (const QChar character : QFileInfo(path).completeBaseName())
    {
        name.push_back(
            character.isLetterOrNumber() && character.unicode() < 128u ? static_cast<char>(character.unicode()) : '_');
    }
    an::tests::fixtures::AcquisitionFacts facts;
    std::string error;
    ASSERT_TRUE(an::tests::fixtures::writeAcquisitionFrame(an::tests::fixtures::acquisitionFrame(name, facts),
                                                           std::filesystem::path(path.toStdWString()), error))
        << error;
}

FrameTreeView* findFrameTreeUnder(QWidget* root)
{
    if (!root)
        return nullptr;
    const auto trees = root->findChildren<FrameTreeView*>();
    return trees.isEmpty() ? nullptr : trees.first();
}

ImageViewControl* findPreviewUnder(QWidget* root)
{
    if (!root)
        return nullptr;
    const auto widgets = root->findChildren<QWidget*>();
    for (QWidget* widget : widgets)
    {
        if (auto* preview = dynamic_cast<ImageViewControl*>(widget))
        {
            return preview;
        }
    }
    return nullptr;
}

void selectFrame(FrameTreeView* tree, const QString& path)
{
    ASSERT_NE(tree, nullptr);
    auto* model = qobject_cast<GroupTreeModel*>(tree->model());
    ASSERT_NE(model, nullptr);

    const QString canonical = FrameId(path).canonicalPath();
    QModelIndex frameIdx;
    for (int attempt = 0; attempt < 100 && !frameIdx.isValid(); ++attempt)
    {
        frameIdx = model->findIndexForPath(canonical);
        if (frameIdx.isValid())
        {
            break;
        }
        QCoreApplication::processEvents();
        QTest::qWait(5);
    }
    ASSERT_TRUE(frameIdx.isValid());

    tree->selectionModel()->select(frameIdx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}
} // namespace

class CalibrationPreviewTooltipsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!QApplication::instance())
        {
            static int argc = 0;
            static char* argv[] = {nullptr};
            m_app = new QApplication(argc, argv);
        }

        // The window registers itself, keeps its settings and reads its frames through the application context.
        ASSERT_TRUE(m_scope.context().valid());

        process = new ImageIntegrationProcess(nullptr);
        ASSERT_NE(process, nullptr);

        model = process->model();
        ASSERT_NE(model, nullptr);

        // Ensure a clean starting state; ImageIntegrationProcess may restore
        // previous session state depending on local settings.
        model->reset();
        QApplication::processEvents();

        lightTab = process->findChild<QWidget*>(QStringLiteral("lightTab"));
        ASSERT_NE(lightTab, nullptr);

        lightTree = findFrameTreeUnder(lightTab);
        ASSERT_NE(lightTree, nullptr);

        lightPreview = findPreviewUnder(lightTab);
        ASSERT_NE(lightPreview, nullptr);
    }

    void TearDown() override
    {
        delete process;
        process = nullptr;
    }

    an::tests::integration::ScopedProcessWindowContext m_scope;
    QApplication* m_app = nullptr;
    ImageIntegrationProcess* process = nullptr;
    IntegrationDataModel* model = nullptr;

    QWidget* lightTab = nullptr;
    FrameTreeView* lightTree = nullptr;
    ImageViewControl* lightPreview = nullptr;
};

TEST_F(CalibrationPreviewTooltipsTest, CalibrationPreviewCheckboxHiddenOnNonLightTabs)
{
    QWidget* biasTab = process->findChild<QWidget*>(QStringLiteral("biasTab"));
    ASSERT_NE(biasTab, nullptr);

    ImageViewControl* biasPreview = findPreviewUnder(biasTab);
    ASSERT_NE(biasPreview, nullptr);

    auto* biasCal = biasPreview->findChild<QCheckBox*>(QStringLiteral("chkCalibrationPreview"));
    ASSERT_NE(biasCal, nullptr);
    EXPECT_TRUE(biasCal->isHidden());

    auto* lightCal = lightPreview->findChild<QCheckBox*>(QStringLiteral("chkCalibrationPreview"));
    ASSERT_NE(lightCal, nullptr);
    EXPECT_FALSE(lightCal->isHidden());
}

TEST_F(CalibrationPreviewTooltipsTest, PreCalibratedFlagDisablesCalibratedPreviewWithExplanation)
{
    const FrameInfo light = createTestLight(QStringLiteral("C:/images/light_001.fit"));
    model->addFrames(FrameType::Light, {light});
    QApplication::processEvents();

    ASSERT_FALSE(model->lightGroups().empty());
    const GroupKey groupKey = model->lightGroups().front().key;

    LightCalibrationSettings settings;
    settings.preCalibrated = true;
    model->setLightCalibrationSettings(groupKey, settings);
    QApplication::processEvents();

    selectFrame(lightTree, light.displayPath);
    QApplication::processEvents();

    auto* cal = lightPreview->findChild<QCheckBox*>(QStringLiteral("chkCalibrationPreview"));
    ASSERT_NE(cal, nullptr);

    EXPECT_FALSE(cal->isEnabled());
    EXPECT_TRUE(cal->toolTip().contains(QStringLiteral("Pre-Calibrated"), Qt::CaseInsensitive));
    EXPECT_TRUE(cal->toolTip().contains(QStringLiteral("Calibration table"), Qt::CaseInsensitive));
    EXPECT_TRUE(cal->toolTip().contains(groupKey.toCompactString(), Qt::CaseInsensitive));
}

TEST_F(CalibrationPreviewTooltipsTest, MissingMasterFilesExplainedInTooltip)
{
    const FrameInfo light = createTestLight(QStringLiteral("C:/images/light_002.fit"));
    model->addFrames(FrameType::Light, {light});
    QApplication::processEvents();

    const auto frameOpt = model->frames(FrameType::Light).findById(light.id);
    ASSERT_TRUE(frameOpt.has_value());
    const LightGroupKey lightKey = LightGroupKey::fromFrame(*frameOpt);
    model->setCalibrationOverride(lightKey, CalibrationOverrideType::Flat,
                                  QStringLiteral("C:/does/not/exist/MasterFlat.fit"));
    QApplication::processEvents();

    selectFrame(lightTree, light.displayPath);
    QApplication::processEvents();

    auto* cal = lightPreview->findChild<QCheckBox*>(QStringLiteral("chkCalibrationPreview"));
    ASSERT_NE(cal, nullptr);

    EXPECT_FALSE(cal->isEnabled());
    const QString tip = cal->toolTip();
    EXPECT_TRUE(tip.contains(QStringLiteral("Missing calibration masters"), Qt::CaseInsensitive)) << tip.toStdString();
    EXPECT_TRUE(tip.contains(QStringLiteral("Flat"), Qt::CaseInsensitive)) << tip.toStdString();
}

TEST_F(CalibrationPreviewTooltipsTest, AssignedCalibrationFramesDoNotEnablePreviewBeforeMastersAreGenerated)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const FrameInfo light = createTestFrame(FrameType::Light, tempDir.filePath(QStringLiteral("light_L.fit")),
                                            QStringLiteral("L"), QStringLiteral("2025-01-10"));
    const FrameInfo bias =
        createTestFrame(FrameType::Bias, tempDir.filePath(QStringLiteral("bias.fit")), QString(), QString());
    const FrameInfo dark =
        createTestFrame(FrameType::Dark, tempDir.filePath(QStringLiteral("dark.fit")), QString(), QString());
    const FrameInfo flat =
        createTestFrame(FrameType::Flat, tempDir.filePath(QStringLiteral("flat_L_previous_session.fit")),
                        QStringLiteral("L"), QStringLiteral("2025-01-09"));

    writePlaceholderFile(light.displayPath);
    writePlaceholderFile(bias.displayPath);
    writePlaceholderFile(dark.displayPath);
    writePlaceholderFile(flat.displayPath);

    model->addFrames(FrameType::Bias, {bias});
    model->addFrames(FrameType::Dark, {dark});
    model->addFrames(FrameType::Flat, {flat});
    model->addFrames(FrameType::Light, {light});
    QApplication::processEvents();

    selectFrame(lightTree, light.displayPath);
    QApplication::processEvents();

    auto* cal = lightPreview->findChild<QCheckBox*>(QStringLiteral("chkCalibrationPreview"));
    ASSERT_NE(cal, nullptr);

    EXPECT_FALSE(cal->isEnabled());
    const QString tip = cal->toolTip();
    EXPECT_TRUE(tip.contains(QStringLiteral("assigned"), Qt::CaseInsensitive)) << tip.toStdString();
    EXPECT_TRUE(tip.contains(QStringLiteral("master"), Qt::CaseInsensitive)) << tip.toStdString();
    EXPECT_TRUE(tip.contains(QStringLiteral("Flat"), Qt::CaseInsensitive)) << tip.toStdString();
}

TEST_F(CalibrationPreviewTooltipsTest, ExistingMasterCalibrationFramesEnablePreview)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const FrameInfo light = createTestFrame(FrameType::Light, tempDir.filePath(QStringLiteral("light_L.fit")),
                                            QStringLiteral("L"), QStringLiteral("2025-01-10"));
    const FrameInfo masterDark =
        createTestFrame(FrameType::Dark, tempDir.filePath(QStringLiteral("MasterDark.fit")), QString(), QString());
    const FrameInfo masterFlat = createTestFrame(FrameType::Flat, tempDir.filePath(QStringLiteral("MasterFlat_L.fit")),
                                                 QStringLiteral("L"), QStringLiteral("2025-01-10"));

    writePlaceholderFile(light.displayPath);
    writePlaceholderFile(masterDark.displayPath);
    writePlaceholderFile(masterFlat.displayPath);

    model->addFrames(FrameType::Dark, {masterDark});
    model->addFrames(FrameType::Flat, {masterFlat});
    model->setMasterStatus(masterDark.displayPath, true, MasterSource::UserDefined);
    model->setMasterStatus(masterFlat.displayPath, true, MasterSource::UserDefined);
    model->addFrames(FrameType::Light, {light});
    QApplication::processEvents();

    selectFrame(lightTree, light.displayPath);
    QApplication::processEvents();

    auto* cal = lightPreview->findChild<QCheckBox*>(QStringLiteral("chkCalibrationPreview"));
    ASSERT_NE(cal, nullptr);

    // What the preview runs: with the masters matched and present, checking the toggle turns the calibrated preview
    // on. The toggle's widget state follows its toolbar action only while the bar shows it, which a window that is
    // never shown does not.
    cal->setChecked(true);
    EXPECT_TRUE(lightPreview->isCalibrationPreviewEnabled()) << cal->toolTip().toStdString();
    const QString tip = cal->toolTip();
    EXPECT_TRUE(tip.contains(QStringLiteral("Dark"), Qt::CaseInsensitive)) << tip.toStdString();
    EXPECT_TRUE(tip.contains(QStringLiteral("Flat"), Qt::CaseInsensitive)) << tip.toStdString();
}

TEST_F(CalibrationPreviewTooltipsTest, ManualUnassignExplainedInTooltip)
{
    const FrameInfo light = createTestLight(QStringLiteral("C:/images/light_003.fit"));
    model->addFrames(FrameType::Light, {light});
    QApplication::processEvents();

    const auto frameOpt = model->frames(FrameType::Light).findById(light.id);
    ASSERT_TRUE(frameOpt.has_value());
    const LightGroupKey lightKey = LightGroupKey::fromFrame(*frameOpt);
    const QString unassigned = QString::fromLatin1(kUnassignedCalibrationMarker);

    model->setCalibrationOverride(lightKey, CalibrationOverrideType::Bias, unassigned);
    model->setCalibrationOverride(lightKey, CalibrationOverrideType::Dark, unassigned);
    model->setCalibrationOverride(lightKey, CalibrationOverrideType::Flat, unassigned);
    QApplication::processEvents();

    selectFrame(lightTree, light.displayPath);
    QApplication::processEvents();

    auto* cal = lightPreview->findChild<QCheckBox*>(QStringLiteral("chkCalibrationPreview"));
    ASSERT_NE(cal, nullptr);

    EXPECT_FALSE(cal->isEnabled());
    const QString tip = cal->toolTip();
    EXPECT_TRUE(tip.contains(QStringLiteral("manually"), Qt::CaseInsensitive)) << tip.toStdString();
    EXPECT_TRUE(tip.contains(QStringLiteral("unassigned"), Qt::CaseInsensitive)) << tip.toStdString();
}
