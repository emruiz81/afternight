// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A view's own reads of a camera file take the RAW policy published when they are asked: a preview read under one
/// balance and then under another shows each, a preview read before is shown from the cache only under the policy it
/// was read with, and a view with no policy service reads the file as decoded.

#include "Test_ControlsDialogs/DialogsTestSupport.h"

#include "fixtures/common/FixtureImage.h"
#include "fixtures/common/RawPolicyCases.h"

#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>

namespace
{

namespace fixtures = an::tests::fixtures;
using an::tests::dialogs::DialogsTestContext;
using an::tests::dialogs::InstalledContext;
using an::tests::dialogs::makeView;
using an::tests::dialogs::settle;
using an::ui::RawOpenSettings;
using an::ui::RawWhiteBalanceSetting;

/// A two-by-two cell of a sixteen-bit image a view shows - red, green, green and blue on the fixture's mosaic - away
/// from the site the fixture holds at its black level.
using Cell = std::array<std::uint16_t, 4u>;

class ViewRawPolicyTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        context = an::tests::dialogs::openDialogsContext();
        ASSERT_NE(context, nullptr);
        installed = std::make_unique<InstalledContext>(*context);
        ASSERT_TRUE(directory.isValid());

        const fixtures::RawPolicyInput* input = fixtures::rawPolicyInput("raw-mosaic-rggb-u16");
        ASSERT_NE(input, nullptr);
        std::string error;
        const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(input->image, error);
        ASSERT_TRUE(samples.has_value()) << error;
        path = directory.filePath(QStringLiteral("mosaic.dng"));
        ASSERT_TRUE(fixtures::writeRawPolicyDng(*input, *samples, std::filesystem::path(path.toStdWString()), error))
            << error;
        view = makeView("preview", ViewControlMode::st_preview);
    }

    void TearDown() override
    {
        view.reset();
        installed.reset();
        context.reset();
    }

    /// Publishes a white balance, auto-rotate off.
    void publish(const RawWhiteBalanceSetting balance)
    {
        RawOpenSettings settings;
        settings.whiteBalance = balance;
        settings.autoRotate = false;
        ASSERT_TRUE(context->rawPolicies.setRawOpenSettings(settings).ok());
    }

    /// Reads the file in the view and answers the first cell it shows.
    [[nodiscard]] std::optional<Cell> readCell()
    {
        view->loadImageFromFile(path.toStdString());
        EXPECT_TRUE(settle(*view));
        const std::shared_ptr<const an::consumer::ImageAsset> asset = view->document().asset();
        if (asset == nullptr)
        {
            ADD_FAILURE() << "the view shows no image";
            return std::nullopt;
        }
        Cell cell{};
        const std::array<std::pair<std::uint32_t, std::uint32_t>, 4u> at{{{2u, 0u}, {3u, 0u}, {2u, 1u}, {3u, 1u}}};
        for (std::size_t index = 0u; index < at.size(); ++index)
        {
            const std::optional<std::uint16_t> sample =
                an::tests::viewer::sampleOf<std::uint16_t>(*asset, at[index].first, at[index].second);
            if (!sample.has_value())
            {
                ADD_FAILURE() << "the view's image is not the sixteen-bit mosaic";
                return std::nullopt;
            }
            cell[index] = *sample;
        }
        return cell;
    }

    std::unique_ptr<DialogsTestContext> context;
    std::unique_ptr<InstalledContext> installed;
    QTemporaryDir directory;
    QString path;
    an::tests::dialogs::ViewHandle view;
};

TEST_F(ViewRawPolicyTest, APreviewOfACameraFileIsReadUnderThePolicyPublishedWhenItIsAsked)
{
    publish(RawWhiteBalanceSetting::None);
    const std::optional<Cell> none = readCell();
    ASSERT_TRUE(none.has_value());

    publish(RawWhiteBalanceSetting::CameraAsShot);
    const std::optional<Cell> asShot = readCell();
    ASSERT_TRUE(asShot.has_value());

    // The camera's as-shot balance scales the red and blue sites of the mosaic by the gains it recorded, two and one
    // and a half against green; no balance leaves every site as decoded.
    EXPECT_GT((*asShot)[0], (*none)[0]);
    EXPECT_EQ((*asShot)[1], (*none)[1]);
    EXPECT_GT((*asShot)[3], (*none)[3]);

    // The cached preview is shown again only under the policy it was read with.
    publish(RawWhiteBalanceSetting::None);
    const std::optional<Cell> again = readCell();
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(*again, *none);
}

TEST_F(ViewRawPolicyTest, APreviewIsShownFromTheCacheOnlyUnderTheSettingsItWasReadWith)
{
    publish(RawWhiteBalanceSetting::None);
    const std::optional<Cell> none = readCell();
    ASSERT_TRUE(none.has_value());

    // With the file gone, only the cache can show the preview: under the settings it was read with it does.
    ASSERT_TRUE(QFile::remove(path));
    const std::uint64_t failedBefore = view->workTelemetry().readsFailed;
    const std::optional<Cell> cached = readCell();
    ASSERT_TRUE(cached.has_value());
    EXPECT_EQ(*cached, *none);
    EXPECT_EQ(view->workTelemetry().readsFailed, failedBefore);

    // Under other settings the cache does not answer, and the read of the missing file fails.
    publish(RawWhiteBalanceSetting::CameraAsShot);
    view->loadImageFromFile(path.toStdString());
    ASSERT_TRUE(settle(*view));
    EXPECT_EQ(view->workTelemetry().readsFailed, failedBefore + 1u);
    EXPECT_EQ(view->document().asset(), nullptr);
}

TEST_F(ViewRawPolicyTest, AViewWithNoPolicyServiceReadsTheFileThroughTheSessionAsDecoded)
{
    publish(RawWhiteBalanceSetting::None);
    const std::optional<Cell> balanced = readCell();
    ASSERT_TRUE(balanced.has_value());

    context->rawOpenPolicyService = nullptr;
    view = makeView("untreated", ViewControlMode::st_preview);
    const std::optional<Cell> decoded = readCell();
    ASSERT_TRUE(decoded.has_value());

    // Every policy stretches a code at the white level to full scale, none included; an untreated read does not.
    EXPECT_NE(*balanced, *decoded);
}

} // namespace
