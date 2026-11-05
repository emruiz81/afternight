// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "fixtures/common/FixtureCatalogue.h"
#include "fixtures/common/FixtureFits.h"
#include "fixtures/harness/TestHarness.h"
#include "fixtures/engine/EngineFixtureAdapter.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/Version.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace
{

namespace fixtures = an::tests::fixtures;
namespace adapter = an::tests::fixtures::engine;

class EngineFixtureAdapterTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const std::optional<std::string> root = an::tests::harness::environmentValue("AFTERNIGHT_TEST_STORAGE_ROOT");
        ASSERT_TRUE(root.has_value());
        m_directory = std::filesystem::path(*root) / "engine-adapter";
        std::filesystem::create_directories(m_directory);
        an::Result<an::consumer::EngineSession> session = adapter::createFixtureSession();
        ASSERT_TRUE(session.hasValue()) << session.status().message();
        m_session.emplace(std::move(*session));
        an::Result<an::consumer::SessionHold> hold = m_session->hold();
        ASSERT_TRUE(hold.hasValue()) << hold.status().message();
        m_hold.emplace(std::move(*hold));
    }

    void TearDown() override
    {
        m_hold.reset();
        m_session.reset();
        std::error_code ignored;
        std::filesystem::remove_all(m_directory, ignored);
    }

    static fixtures::FixtureSamples materialized(const fixtures::FixtureImage& image)
    {
        std::string error;
        const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(image, error);
        EXPECT_TRUE(samples.has_value()) << error;
        return samples.value_or(fixtures::FixtureSamples{});
    }

    std::filesystem::path m_directory;
    std::optional<an::consumer::EngineSession> m_session;
    std::optional<an::consumer::SessionHold> m_hold;
};

TEST_F(EngineFixtureAdapterTest, TheVersionIsTheLoadedLibrarysOwn)
{
    EXPECT_EQ(adapter::loadedEngineVersion(), std::string(an::engineVersionString()));
    const fixtures::ExpectationProvenance provenance = adapter::engineProvenance();
    EXPECT_EQ(provenance.producer, "AfterNightEngine");
    EXPECT_EQ(provenance.producerVersion, adapter::loadedEngineVersion());
    EXPECT_EQ(provenance.producerAbi, std::to_string(an::engineAbiGeneration()));
    EXPECT_FALSE(provenance.configuration.empty());
    EXPECT_FALSE(provenance.compiler.empty());
    EXPECT_FALSE(provenance.platform.empty());
    EXPECT_EQ(provenance.captured.size(), 10u);
}

TEST_F(EngineFixtureAdapterTest, TheDescriptorStatesWhatTheFixtureStates)
{
    ASSERT_FALSE(fixtures::standardFixtures().empty());
    for (const fixtures::FixtureImage& image : fixtures::standardFixtures())
    {
        const an::Result<an::ImageDescriptor> descriptor = adapter::describeEngineImage(m_hold->engine(), image);
        ASSERT_TRUE(descriptor.hasValue()) << image.name << ": " << descriptor.status().message();
        EXPECT_EQ(descriptor->extent().width(), image.width) << image.name;
        EXPECT_EQ(descriptor->extent().height(), image.height) << image.name;
        EXPECT_EQ(descriptor->extent().channelCount(), fixtures::channelCountOf(image.channels)) << image.name;
        EXPECT_EQ(descriptor->sampleType(), adapter::engineSampleType(image.sampleType)) << image.name;
        EXPECT_EQ(descriptor->channelSchema(), adapter::engineChannelSchema(image.channels)) << image.name;
        EXPECT_EQ(descriptor->numericDomain(), adapter::engineNumericDomain(image.domain)) << image.name;
        EXPECT_EQ(descriptor->cfa().pattern(), adapter::engineCfaPattern(image.cfa)) << image.name;
        EXPECT_EQ(descriptor->coordinateOrigin(), an::CoordinateOrigin::TopLeft) << image.name;
    }
}

TEST_F(EngineFixtureAdapterTest, EveryStandardFixtureBecomesAnEngineImageWithItsExactSamples)
{
    ASSERT_FALSE(fixtures::standardFixtures().empty());
    for (const fixtures::FixtureImage& image : fixtures::standardFixtures())
    {
        const fixtures::FixtureSamples samples = materialized(image);
        const an::Result<adapter::EngineImage> made = adapter::makeEngineImage(m_hold->engine(), image, samples);
        ASSERT_TRUE(made.hasValue()) << image.name << ": " << made.status().message();
        std::string error;
        const std::optional<fixtures::FixtureSamples> read = adapter::readImageSamples(*made, error);
        ASSERT_TRUE(read.has_value()) << image.name << ": " << error;
        EXPECT_TRUE(fixtures::sameSamples(*read, samples)) << image.name;
    }
}

// What a product consumer keeps is an asset of its session: the same samples, read back under a read lease, and
// counted by the session until the case lets it go.
TEST_F(EngineFixtureAdapterTest, EveryStandardFixtureBecomesAnAssetOfTheSessionWithItsExactSamples)
{
    ASSERT_FALSE(fixtures::standardFixtures().empty());
    for (const fixtures::FixtureImage& image : fixtures::standardFixtures())
    {
        const fixtures::FixtureSamples samples = materialized(image);
        an::Result<an::consumer::ImageAsset> asset = adapter::makeImageAsset(*m_session, image, samples);
        ASSERT_TRUE(asset.hasValue()) << image.name << ": " << asset.status().message();
        EXPECT_EQ(m_session->outstanding().assets, 1u) << image.name;
        std::string error;
        const std::optional<fixtures::FixtureSamples> read = adapter::readAssetSamples(*asset, error);
        ASSERT_TRUE(read.has_value()) << image.name << ": " << error;
        EXPECT_TRUE(fixtures::sameSamples(*read, samples)) << image.name;
    }
    EXPECT_EQ(m_session->outstanding().assets, 0u);
}

TEST_F(EngineFixtureAdapterTest, EveryNeutralFitsFileDecodesToItsDescriptionsSamples)
{
    ASSERT_FALSE(fixtures::standardFixtures().empty());
    for (const fixtures::FixtureImage& image : fixtures::standardFixtures())
    {
        const fixtures::FixtureSamples samples = materialized(image);
        const std::filesystem::path path = m_directory / (image.name + ".fits");
        std::string error;
        ASSERT_TRUE(fixtures::writeFixtureFits(image, samples, path, error)) << error;
        const an::Result<adapter::DecodedFile> decoded = adapter::decodeEngineFile(*m_session, path);
        ASSERT_TRUE(decoded.hasValue()) << image.name << ": " << decoded.status().message();
        EXPECT_TRUE(fixtures::sameSamples(decoded->samples, samples)) << image.name;
        EXPECT_EQ(decoded->cfa, image.cfa) << image.name;
        // The FITS adapter states a float image as physically scaled and an integer one as sensor codes, whatever the
        // file's author meant.
        EXPECT_EQ(decoded->domain, fixtures::isFloating(image.sampleType) ? fixtures::FixtureDomain::PhysicalScaled
                                                                          : fixtures::FixtureDomain::SensorCode)
            << image.name;
    }
}

TEST_F(EngineFixtureAdapterTest, ABottomUpNeutralFileDecodesTopDown)
{
    for (const char* name : {"gradient-rgb-uint16", "cfa-rggb-uint16"})
    {
        const fixtures::FixtureImage* image = fixtures::standardFixture(name);
        ASSERT_NE(image, nullptr) << name;
        const fixtures::FixtureSamples samples = materialized(*image);
        const std::filesystem::path path = m_directory / (image->name + "-bottom-up.fits");
        std::string error;
        ASSERT_TRUE(fixtures::writeFixtureFits(*image, samples, path, error, fixtures::FixtureRowOrder::BottomUp))
            << error;
        const an::Result<adapter::DecodedFile> decoded = adapter::decodeEngineFile(*m_session, path);
        ASSERT_TRUE(decoded.hasValue()) << name << ": " << decoded.status().message();
        EXPECT_TRUE(fixtures::sameSamples(decoded->samples, samples)) << name;
        EXPECT_EQ(decoded->cfa, image->cfa) << name;
    }
}

TEST_F(EngineFixtureAdapterTest, WhatTheEngineWriterStoresTheNeutralReaderReadsBack)
{
    ASSERT_FALSE(fixtures::standardFixtures().empty());
    for (const fixtures::FixtureImage& image : fixtures::standardFixtures())
    {
        const fixtures::FixtureSamples samples = materialized(image);
        const std::filesystem::path path = m_directory / (image.name + "-engine.fits");
        const an::Status written = adapter::writeEngineFits(*m_session, image, samples, path);
        ASSERT_TRUE(written.ok()) << image.name << ": " << written.message();
        std::string error;
        const std::optional<fixtures::FixtureFitsFile> file = fixtures::readFixtureFits(path, error);
        ASSERT_TRUE(file.has_value()) << image.name << ": " << error;
        EXPECT_TRUE(fixtures::sameSamples(file->samples, samples)) << image.name;
        EXPECT_EQ(file->cfa, image.cfa) << image.name;
        for (const fixtures::FixtureCard& card : image.cards)
        {
            EXPECT_EQ(file->card(card.keyword).value_or("<missing>"), card.value) << image.name << " " << card.keyword;
        }
    }
}

TEST_F(EngineFixtureAdapterTest, WhatTheNeutralLayerDoesNotDescribeIsRefusedByName)
{
    std::string error;
    EXPECT_FALSE(adapter::readImageSamples(adapter::EngineImage{}, error).has_value());
    EXPECT_FALSE(error.empty());

    const fixtures::FixtureImage* image = fixtures::standardFixture("gradient-rgb-uint16");
    ASSERT_NE(image, nullptr);
    const an::Result<adapter::EngineImage> mismatched =
        adapter::makeEngineImage(m_hold->engine(), *image, fixtures::FixtureSamples(1u, 1u, 1u, image->sampleType));
    EXPECT_FALSE(mismatched.hasValue());
    EXPECT_FALSE(adapter::makeImageAsset(*m_session, *image, fixtures::FixtureSamples(1u, 1u, 1u, image->sampleType))
                     .hasValue());

    const an::Result<adapter::DecodedFile> absent = adapter::decodeEngineFile(*m_session, m_directory / "absent.fits");
    EXPECT_FALSE(absent.hasValue());
}

} // namespace
