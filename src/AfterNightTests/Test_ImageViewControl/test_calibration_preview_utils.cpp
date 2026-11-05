// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "controls/CalibrationPreviewUtils.h"

TEST(ImageViewControlCalibrationPreviewUtils, EnabledStateRequiresCheckboxAvailabilityAndInputs)
{
    an::ui::CalibrationPreviewConfig config;

    EXPECT_FALSE(an::ui::detail::calibrationPreviewIsEnabled(config, true));

    config.available = true;
    EXPECT_FALSE(an::ui::detail::calibrationPreviewIsEnabled(config, true));

    config.biasPath = QStringLiteral("/tmp/MasterBias.fit");
    EXPECT_FALSE(an::ui::detail::calibrationPreviewIsEnabled(config, false));
    EXPECT_TRUE(an::ui::detail::calibrationPreviewIsEnabled(config, true));
}

TEST(ImageViewControlCalibrationPreviewUtils, CalibrationKeyReturnsEmptyWhenPreviewIsDisabled)
{
    an::ui::CalibrationPreviewConfig config;
    config.available = true;
    config.biasPath = QStringLiteral("/tmp/MasterBias.fit");

    EXPECT_TRUE(an::ui::detail::buildCalibrationPreviewKey(config, false).isEmpty());

    config.available = false;
    EXPECT_TRUE(an::ui::detail::buildCalibrationPreviewKey(config, true).isEmpty());
}

TEST(ImageViewControlCalibrationPreviewUtils, CalibrationKeySerializesAllRelevantFields)
{
    an::ui::CalibrationPreviewConfig config;
    config.available = true;
    config.biasPath = QStringLiteral("/tmp/MasterBias.fit");
    config.darkPath = QStringLiteral("/tmp/MasterDark.fit");
    config.flatPath = QStringLiteral("/tmp/MasterFlat.fit");
    config.subtractBiasFromDark = true;
    config.settings.cosmeticCorrection = false;
    config.settings.sigmaCold = 1.5;
    config.settings.sigmaHot = 2.5;
    config.settings.darkOptimization = true;
    config.settings.darkMultiplicationFactor = 1.25;
    config.settings.flatAutoLevel = false;
    config.settings.pedestal = 42.0;

    const QString key = an::ui::detail::buildCalibrationPreviewKey(config, true);

    EXPECT_TRUE(key.contains(QStringLiteral("/tmp/MasterBias.fit")));
    EXPECT_TRUE(key.contains(QStringLiteral("/tmp/MasterDark.fit")));
    EXPECT_TRUE(key.contains(QStringLiteral("/tmp/MasterFlat.fit")));
    EXPECT_TRUE(key.contains(QStringLiteral("1.500000")));
    EXPECT_TRUE(key.contains(QStringLiteral("2.500000")));
    EXPECT_TRUE(key.contains(QStringLiteral("1.250000")));
    EXPECT_TRUE(key.contains(QStringLiteral("42.000000")));
}

TEST(ImageViewControlCalibrationPreviewUtils, CalibrationKeyChangesWhenSettingsChange)
{
    an::ui::CalibrationPreviewConfig config;
    config.available = true;
    config.biasPath = QStringLiteral("/tmp/MasterBias.fit");
    config.settings.sigmaCold = 3.0;

    const QString first = an::ui::detail::buildCalibrationPreviewKey(config, true);

    config.settings.sigmaCold = 4.0;
    const QString second = an::ui::detail::buildCalibrationPreviewKey(config, true);

    EXPECT_NE(first, second);
}
