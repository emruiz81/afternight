// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The recent files' thumbnails: which images are linear data, a linear image stretched by the display presentation,
/// each colour channel on its own, an image reduced to the thumbnail's size, a mosaic shown as the grey picture its
/// codes make, an image that is not linear left unstretched, and thumbnails rendered on a worker and recorded only
/// while they are the last one asked for their file.

#include "Test_ControlsFiles/FilesTestContext.h"

#include "controls/AutostretchApply.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageMetadata.h>

#include <QDir>
#include <QFileInfo>

#include <gtest/gtest.h>

#include <cmath>

namespace
{

using an::tests::files::Channels;
using an::tests::files::FileManagerFixture;
using an::tests::files::FilesTestContext;
using an::tests::files::InstalledContext;
using an::tests::files::makeAsset;
using an::tests::files::openFilesContext;
using an::tests::files::pumpQuietly;

/// Metadata whose processing-state cards record the image's linearity as `value` - `'linear'` or `'nonlinear'` - as a
/// file the product wrote does.
an::ImageMetadata recordedAs(const char* value)
{
    an::Result<an::CodecCard> card = an::CodecCard::create(an::CodecNamespace::Fits, "ANLIN", value, {});
    EXPECT_TRUE(card.hasValue()) << card.status().message();
    an::Result<an::CodecMetadata> codec = an::CodecMetadata::create({}, {*card});
    EXPECT_TRUE(codec.hasValue()) << codec.status().message();
    return an::ImageMetadata::create(an::AcquisitionMetadata{}, an::ImageSemantics{}, an::AstrometricSolution{},
                                     an::Provenance{}, *codec, std::nullopt);
}

/// Metadata whose processing-state cards record the image linear.
an::ImageMetadata recordedLinear()
{
    return recordedAs("'linear'");
}

/// The largest difference between two assets' samples, over every sample, or a negative number when their shapes or
/// representations differ.
template <typename SampleT>
double largestDifference(const an::consumer::ImageAsset& left, const an::consumer::ImageAsset& right)
{
    an::Result<an::consumer::AssetReadLease> a = left.read();
    an::Result<an::consumer::AssetReadLease> b = right.read();
    if (!a.hasValue() || !b.hasValue())
    {
        return -1.0;
    }
    const auto* first = std::get_if<an::Image<SampleT>>(&a->image());
    const auto* second = std::get_if<an::Image<SampleT>>(&b->image());
    if (first == nullptr || second == nullptr || first->view()->width() != second->view()->width() ||
        first->view()->height() != second->view()->height() ||
        first->view()->channelCount() != second->view()->channelCount())
    {
        return -1.0;
    }
    double largest = 0.0;
    const auto one = first->view();
    const auto two = second->view();
    for (std::uint32_t channel = 0u; channel < one->channelCount(); ++channel)
    {
        for (std::uint32_t y = 0u; y < one->height(); ++y)
        {
            const auto rowOne = one->row(channel, y);
            const auto rowTwo = two->row(channel, y);
            for (std::uint32_t x = 0u; x < one->width(); ++x)
            {
                largest = (std::max)(largest, std::abs(static_cast<double>((*rowOne)[x]) - (*rowTwo)[x]));
            }
        }
    }
    return largest;
}

class ThumbnailTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openFilesContext(2u);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
    }

    void TearDown() override
    {
        m_installed.reset();
        m_context.reset();
    }

    /// A colour image whose channels sit at very different levels, as an unbalanced linear frame does; its cards
    /// record it linear when asked.
    std::shared_ptr<an::consumer::ImageAsset> unbalancedColour(bool linear, std::uint32_t width = 64u,
                                                               std::uint32_t height = 48u)
    {
        an::Result<an::consumer::SessionHold> hold = m_context->engineSession().hold();
        EXPECT_TRUE(hold.hasValue());
        const std::optional<an::ImageDescriptor> descriptor =
            an::tests::viewer::describeImage(hold->engine(), an::SampleType::UInt16, width, height, Channels::Rgb);
        EXPECT_TRUE(descriptor.has_value());
        an::Result<an::Image<std::uint16_t>> image = an::Image<std::uint16_t>::create(
            hold->engine(), *descriptor, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed,
            linear ? recordedLinear() : an::ImageMetadata{});
        EXPECT_TRUE(image.hasValue()) << image.status().message();
        {
            std::optional<an::ImageWriteAccess<std::uint16_t>> access = image->write();
            for (std::uint32_t channel = 0u; channel < 3u; ++channel)
            {
                for (std::uint32_t y = 0u; y < height; ++y)
                {
                    const auto row = access->view().row(channel, y);
                    for (std::uint32_t x = 0u; x < width; ++x)
                    {
                        const std::uint32_t noise = (x * 7u + y * 13u) % 29u;
                        const std::uint32_t star = (x % 16u == 5u && y % 12u == 4u) ? 30000u : 0u;
                        (*row)[x] = static_cast<std::uint16_t>((channel + 1u) * 900u + 4u * noise + star);
                    }
                }
            }
        }
        an::Result<an::consumer::ImageAsset> asset =
            an::consumer::ImageAsset::adopt(m_context->engineSession(), an::consumer::AnyImage{std::move(*image)});
        EXPECT_TRUE(asset.hasValue()) << asset.status().message();
        return std::make_shared<an::consumer::ImageAsset>(std::move(*asset));
    }

    std::unique_ptr<FilesTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

} // namespace

// A linear colour image's thumbnail is stretched with its channels unlinked - each channel its own stretch, as the
// view's autostretch shows an image unless asked to link, so the thumbnail's colours are the ones the view shows - and
// is exactly the display presentation's unlinked stretch.
TEST_F(ThumbnailTest, ALinearColourThumbnailIsStretchedWithItsChannelsUnlinked)
{
    std::shared_ptr<an::consumer::ImageAsset> image = unbalancedColour(true);
    an::Result<std::shared_ptr<const an::consumer::ImageAsset>> thumbnail =
        an::ui::renderRecentFileThumbnail(m_context->engineSession(), image, {});
    ASSERT_TRUE(thumbnail.hasValue()) << thumbnail.status().message();

    an::Result<an::consumer::AssetReadLease> lease = image->read();
    ASSERT_TRUE(lease.hasValue());
    an::ui::AutostretchApplySettings unlinked;
    unlinked.linkedChannels = false;
    an::Result<an::consumer::ImageAsset> expected =
        an::ui::applyAutostretchToCopy(m_context->engineSession(), lease->image(), nullptr, unlinked, {});
    an::ui::AutostretchApplySettings linked;
    linked.linkedChannels = true;
    an::Result<an::consumer::ImageAsset> together =
        an::ui::applyAutostretchToCopy(m_context->engineSession(), lease->image(), nullptr, linked, {});
    lease->release();
    ASSERT_TRUE(expected.hasValue());
    ASSERT_TRUE(together.hasValue());
    EXPECT_EQ(largestDifference<std::uint16_t>(**thumbnail, *expected), 0.0);
    EXPECT_GT(largestDifference<std::uint16_t>(**thumbnail, *together), 1000.0);
}

// An image is linear data, which a picture of it - a thumbnail, a history step's preview - shows stretched, when its
// cards record it linear or it is a camera RAW frame; cards that record it nonlinear, or none, leave it as it is.
TEST_F(ThumbnailTest, LinearDataIsAnImageRecordedLinearOrACameraRawFrame)
{
    EXPECT_TRUE(an::ui::isLinearData(recordedLinear()));
    EXPECT_FALSE(an::ui::isLinearData(recordedAs("'nonlinear'")));
    EXPECT_FALSE(an::ui::isLinearData(an::ImageMetadata{}));

    an::Result<an::AcquisitionMetadata> camera =
        an::AcquisitionMetadata::create(an::AcquisitionMetadataRequest{.rawCamera = an::RawCameraMetadata{}});
    ASSERT_TRUE(camera.hasValue()) << camera.status().message();
    EXPECT_TRUE(an::ui::isLinearData(an::ImageMetadata::create(*camera, an::ImageSemantics{}, an::AstrometricSolution{},
                                                               an::Provenance{}, an::CodecMetadata{}, std::nullopt)));
}

// An image that does not record itself linear is not stretched: the thumbnail is the image itself when it needs no
// reduction.
TEST_F(ThumbnailTest, AnImageNotRecordedLinearIsNotStretched)
{
    std::shared_ptr<an::consumer::ImageAsset> image = unbalancedColour(false);
    an::Result<std::shared_ptr<const an::consumer::ImageAsset>> thumbnail =
        an::ui::renderRecentFileThumbnail(m_context->engineSession(), image, {});
    ASSERT_TRUE(thumbnail.hasValue()) << thumbnail.status().message();
    EXPECT_EQ(thumbnail->get(), image.get());
}

// An image larger than the thumbnail is reduced so its longer side is the thumbnail's, keeping its aspect.
TEST_F(ThumbnailTest, ALargeImageIsReducedToTheThumbnailsSize)
{
    std::shared_ptr<an::consumer::ImageAsset> image = unbalancedColour(false, 3000u, 1200u);
    an::Result<std::shared_ptr<const an::consumer::ImageAsset>> thumbnail =
        an::ui::renderRecentFileThumbnail(m_context->engineSession(), image, {});
    ASSERT_TRUE(thumbnail.hasValue()) << thumbnail.status().message();
    const std::optional<an::ImageDescriptor> reduced = (*thumbnail)->descriptor();
    ASSERT_TRUE(reduced.has_value());
    EXPECT_EQ(reduced->extent().width(), static_cast<std::uint32_t>(an::ui::RECENT_FILE_THUMBNAIL_MAX_DIMENSION));
    EXPECT_EQ(reduced->extent().height(), 960u);
    EXPECT_EQ(reduced->extent().channelCount(), 3u);
}

// A mosaic's thumbnail is the grey picture its codes make, which a JPEG can hold.
TEST_F(ThumbnailTest, AMosaicsThumbnailIsTheGreyPictureOfItsCodes)
{
    std::optional<an::consumer::ImageAsset> mosaic = makeAsset<std::uint16_t>(
        m_context->engineSession(), 40u, 30u, Channels::Grey,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return static_cast<std::uint16_t>(100u * x + y); },
        an::NumericDomain::NormalizedLinear, *an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u));
    ASSERT_TRUE(mosaic.has_value());
    auto shared = std::make_shared<an::consumer::ImageAsset>(std::move(*mosaic));
    an::Result<std::shared_ptr<const an::consumer::ImageAsset>> thumbnail =
        an::ui::renderRecentFileThumbnail(m_context->engineSession(), shared, {});
    ASSERT_TRUE(thumbnail.hasValue()) << thumbnail.status().message();
    const std::optional<an::ImageDescriptor> picture = (*thumbnail)->descriptor();
    ASSERT_TRUE(picture.has_value());
    EXPECT_FALSE(picture->isMosaiced());
    EXPECT_EQ(picture->extent().width(), 40u);

    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("mosaic.jpg"));
    const an::Status written = an::ui::writeRecentFileThumbnail(m_context->engineSession(), shared, path, {});
    EXPECT_TRUE(written.ok()) << written.message();
    EXPECT_TRUE(QFileInfo::exists(path));
}

// A thumbnail is rendered and written on a worker and recorded on the GUI thread; asked for again before the first
// lands, only the last is recorded and the superseded file is removed.
TEST_F(ThumbnailTest, OnlyTheLastThumbnailAskedForAFileIsRecorded)
{
    FileManagerFixture files;
    const std::string name = files.path(QStringLiteral("frame.fits")).toStdString();
    // Three states of the file, each of its own size: only the last one's thumbnail may be recorded.
    files.manager().addRecentFile(name, unbalancedColour(true, 64u, 48u));
    files.manager().addRecentFile(name, unbalancedColour(true, 48u, 36u));
    files.manager().addRecentFile(name, unbalancedColour(true, 32u, 24u));
    ASSERT_TRUE(an::tests::files::pumpUntil([&files]() { return !files.recordedThumbnails.empty(); }));
    QThreadPool::globalInstance()->waitForDone();
    pumpQuietly();
    ASSERT_EQ(files.recordedThumbnails.size(), 1u);
    const QString recorded = files.recordedThumbnail(name);
    EXPECT_EQ(recorded, files.recordedThumbnails.front().second);
    EXPECT_TRUE(QFileInfo::exists(recorded));
    {
        an::Result<an::consumer::OperationScope> scope = m_context->engineSession().admit(
            an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply});
        ASSERT_TRUE(scope.hasValue());
        const std::string file = recorded.toStdString();
        an::Result<an::CodecDecodeResult> read = an::decodeImage(
            scope->engine(), scope->execution(), m_context->registry(), an::CodecReadRequest{.path = file});
        ASSERT_TRUE(read.hasValue()) << read.status().message();
        EXPECT_EQ(read->frame.descriptor()->extent().width(), 32u);
        EXPECT_EQ(read->frame.descriptor()->residentSampleType(), an::SampleType::UInt8);
    }
    // Only the recorded thumbnail is left in the folder.
    EXPECT_EQ(QDir(files.thumbnailsDirectory()).entryList(QDir::Files).size(), 1);
    EXPECT_GE(files.thumbnailReloads, 1);

    // Removing the file from the recent list removes its thumbnail.
    files.manager().removeRecentFile(name);
    EXPECT_TRUE(files.recordedThumbnail(name).isEmpty());
    EXPECT_FALSE(QFileInfo::exists(recorded));
}
