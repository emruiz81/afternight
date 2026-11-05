// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "processes/ImageIntegration/workflow/DirtyStateTracker.h"

TEST(DirtyStateTracker, BiasChangePropagatesToDarkFlatAndRegistration)
{
    DirtyStateTracker tracker;

    GroupKey biasKey{QStringLiteral("Bias|6144x4128|800ISO")};
    GroupKey darkKey{QStringLiteral("Dark|6144x4128|800ISO|120s")};
    GroupKey flatKey{QStringLiteral("Flat|6144x4128|800ISO|L")};
    GroupKey regKey{QStringLiteral("Reg|6144x4128|M31")};

    tracker.markBiasGroupDirty(biasKey, DirtyReason::StackingSettingsChanged, "Method changed");
    tracker.propagateBiasDirtyToConsumers(biasKey, {darkKey}, {flatKey}, {regKey});

    EXPECT_TRUE(tracker.isBiasGroupDirty(biasKey));
    EXPECT_TRUE(tracker.isDarkGroupDirty(darkKey));
    EXPECT_TRUE(tracker.isFlatGroupDirty(flatKey));
    EXPECT_TRUE(tracker.isRegistrationGroupDirty(regKey));

    auto darkDetails = tracker.getDirtyDetails("Dark|" + darkKey.toString());
    ASSERT_TRUE(darkDetails.has_value());
    EXPECT_EQ(darkDetails->reason, DirtyReason::UpstreamDirty);
    EXPECT_TRUE(darkDetails->upstreamCause.has_value());
}

TEST(DirtyStateTracker, RegistrationChangePropagatesToIntegration)
{
    DirtyStateTracker tracker;

    GroupKey regKey{QStringLiteral("Reg|6144x4128|M31")};
    IntegrationGroupKey intKey1{GroupKey{QStringLiteral("Int|M31|L")}};
    IntegrationGroupKey intKey2{GroupKey{QStringLiteral("Int|M31|Ha")}};

    tracker.markRegistrationGroupDirty(regKey, DirtyReason::RegistrationSettingsChanged,
                                       "Transformation changed: Affine -> Homography");
    tracker.propagateRegistrationDirtyToIntegration(regKey, {intKey1, intKey2});

    EXPECT_TRUE(tracker.isRegistrationGroupDirty(regKey));
    EXPECT_TRUE(tracker.isIntegrationGroupDirty(intKey1));
    EXPECT_TRUE(tracker.isIntegrationGroupDirty(intKey2));
}
