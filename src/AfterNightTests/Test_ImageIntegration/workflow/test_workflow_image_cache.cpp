// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "processes/ImageIntegration/workflow/WorkflowImageCache.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutor.h"

#include "Test_ImageIntegration/ImportSession.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <gtest/gtest.h>

#include <QDir>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <variant>

namespace
{
ExecutionPreparationSnapshot::FileStamp makeCacheStamp(const QString& path, const QString& generation)
{
    return {.canonicalPath = path,
            .exists = true,
            .isFile = true,
            .sizeBytes = 16384,
            .modifiedAtMs = 1234,
            .identityToken = generation};
}

WorkflowImageCacheKey makeDecodedKey(const QString& generation, const QString& variant = QStringLiteral("raw-v1"))
{
    const FrameId frame(QStringLiteral("C:/cache/light.fit"));
    return WorkflowImageCacheKey::decoded(frame, makeCacheStamp(frame.canonicalPath(), generation), variant);
}

WorkflowImageCacheKey makeCalibratedKey(const QString& source_generation, const QString& calibration_identity,
                                        const QString& pixel_variant = QStringLiteral("calibrated-float-v1"))
{
    const FrameId frame(QStringLiteral("C:/cache/light.fit"));
    return WorkflowImageCacheKey::calibrated(frame, makeCacheStamp(frame.canonicalPath(), source_generation),
                                             calibration_identity, pixel_variant);
}

WorkflowManifest makeExecutorManifest(const QString& temporaryRoot)
{
    WorkflowManifest manifest;
    manifest.outputDirectory = QDir(temporaryRoot).filePath(QStringLiteral("output"));
    WorkflowManifest::RegistrationGroup registration;
    registration.groupKey = GroupKey{QStringLiteral("cache|executor")};
    manifest.registrationGroups.push_back(std::move(registration));
    return manifest;
}

/// The width an image describes, or zero for one that holds nothing.
std::uint32_t widthOf(const an::consumer::AnyImage& image)
{
    return std::visit(
        [](const auto& held) -> std::uint32_t {
            const auto descriptor = held.descriptor();
            return descriptor.has_value() ? descriptor->extent().width() : 0u;
        },
        image);
}

/// The bytes an image's storage took from the engine.
std::uint64_t storageCapacityOf(const an::consumer::AnyImage& image)
{
    return std::visit([](const auto& held) { return held.storage().capacityBytes(); }, image);
}

/// The first sample of a Float32 image's first plane, or NaN for anything else.
float firstSampleOf(const an::consumer::AnyImage& image)
{
    const auto* held = std::get_if<an::Image<float>>(&image);
    if (held == nullptr)
    {
        return std::numeric_limits<float>::quiet_NaN();
    }
    const auto view = held->view();
    if (!view.has_value())
    {
        return std::numeric_limits<float>::quiet_NaN();
    }
    const auto row = view->row(0u, 0u);
    return row.has_value() && !row->empty() ? (*row)[0] : std::numeric_limits<float>::quiet_NaN();
}

using ImageMaker = std::function<WorkflowCachedImage()>;

class ExecutorCacheProbe final : public IWorkflowStage
{
public:
    ExecutorCacheProbe(std::atomic<int>& physicalLoads, const StageResult::Status status, ImageMaker makeImage)
        : m_physicalLoads(physicalLoads), m_status(status), m_makeImage(std::move(makeImage))
    {
    }

    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("ExecutorCacheProbe");
    }

    [[nodiscard]] WorkflowStage stageId() const override
    {
        return WorkflowStage::None;
    }

    StageResult execute(const WorkflowManifest&, CancellationToken& token, ProgressReporter&) override
    {
        const auto& cache = workflowImageCache();
        if (!cache)
        {
            return StageResult::failure(IntegrationError::internalError(QStringLiteral("Image cache not bound")));
        }
        const auto key = makeDecodedKey(QStringLiteral("executor-source"));
        auto first = cache->load(key, [&]() {
            ++m_physicalLoads;
            return m_makeImage();
        });
        auto second = cache->load(key, [&]() {
            ++m_physicalLoads;
            return m_makeImage();
        });
        if (!first || !second || first.get() != second.get())
        {
            return StageResult::failure(IntegrationError::internalError(QStringLiteral("Image cache reuse failed")));
        }
        if (m_status == StageResult::Status::Cancelled)
        {
            token.requestCancel();
            return StageResult::cancelled();
        }
        if (m_status == StageResult::Status::Failure)
        {
            return StageResult::failure(IntegrationError::internalError(QStringLiteral("Injected stage failure")));
        }
        return StageResult::success();
    }

private:
    std::atomic<int>& m_physicalLoads;
    StageResult::Status m_status = StageResult::Status::Success;
    ImageMaker m_makeImage;
};

/// The cache's cases, over engine images made in a session of their own.
class WorkflowImageCacheP213 : public ::testing::Test
{
protected:
    void SetUp() override
    {
        an::Result<an::consumer::EngineSession> opened = an::tests::integration::openImportSession(2u);
        ASSERT_TRUE(opened.hasValue()) << opened.status().message();
        m_session = std::make_unique<an::consumer::EngineSession>(std::move(*opened));
    }

    /// A grey Float32 image `width` by 64 pixels with every sample `value`, as the cache holds a frame.
    [[nodiscard]] WorkflowCachedImage makeImage(const std::uint32_t width = 64u, const float value = 0.25f) const
    {
        an::Result<an::consumer::SessionHold> hold = m_session->hold();
        if (!hold.hasValue())
        {
            ADD_FAILURE() << hold.status().message();
            return {};
        }
        const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, 64u, 1u, hold->engine().limits());
        if (!extent.has_value())
        {
            ADD_FAILURE() << "the engine refused a " << width << " by 64 extent";
            return {};
        }
        const std::optional<an::ImageDescriptor> descriptor =
            an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                                   .sampleType = an::SampleType::Float32,
                                                                   .channelSchema = an::ChannelSchema::Mono,
                                                                   .colorSpace = an::ColorSpace::Unspecified,
                                                                   .numericDomain = an::NumericDomain::NormalizedLinear,
                                                                   .cfa = an::CfaDescriptor{},
                                                                   .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                                   .pixelAspectRatio = 1.0});
        if (!descriptor.has_value())
        {
            ADD_FAILURE() << "the engine refused a grey Float32 description";
            return {};
        }
        an::Result<an::Image<float>> image =
            an::Image<float>::createFilled(hold->engine(), *descriptor, an::StorageArrangement::Planar, value);
        if (!image.hasValue())
        {
            ADD_FAILURE() << image.status().message();
            return {};
        }
        return std::make_shared<const an::consumer::AnyImage>(std::move(*image));
    }

    /// A maker the executor's probe stage builds its images with.
    [[nodiscard]] ImageMaker imageMaker() const
    {
        return [this]() { return makeImage(); };
    }

    std::unique_ptr<an::consumer::EngineSession> m_session;
};
} // namespace

TEST_F(WorkflowImageCacheP213, ReusesDecodedAndCalibratedProductsWithinOneRun)
{
    WorkflowImageCache cache(256 * 1024);
    std::atomic<int> decoded_loads{0};
    std::atomic<int> calibrations{0};

    const auto decoded_key = makeDecodedKey(QStringLiteral("source-a"));
    const auto calibrated_key = makeCalibratedKey(QStringLiteral("source-a"), QStringLiteral("masters-a"));

    auto decoded_first = cache.load(decoded_key, [&]() {
        ++decoded_loads;
        return makeImage();
    });
    auto decoded_second = cache.load(decoded_key, [&]() {
        ++decoded_loads;
        return makeImage();
    });
    auto calibrated_first = cache.load(calibrated_key, [&]() {
        ++calibrations;
        return makeImage(64, 0.5f);
    });
    auto calibrated_second = cache.load(calibrated_key, [&]() {
        ++calibrations;
        return makeImage(64, 0.5f);
    });

    ASSERT_TRUE(decoded_first && decoded_second && calibrated_first && calibrated_second);
    EXPECT_EQ(decoded_first.get(), decoded_second.get());
    EXPECT_EQ(calibrated_first.get(), calibrated_second.get());
    EXPECT_NE(decoded_first.get(), calibrated_first.get());
    EXPECT_EQ(decoded_loads.load(), 1);
    EXPECT_EQ(calibrations.load(), 1);
    const auto diagnostics = cache.diagnostics();
    EXPECT_EQ(diagnostics.hits, 2u);
    EXPECT_EQ(diagnostics.misses, 2u);
    EXPECT_EQ(diagnostics.physicalDecodedLoads, 1u);
    EXPECT_EQ(diagnostics.physicalCalibrations, 1u);
}

TEST_F(WorkflowImageCacheP213, StableIdentityInvalidatesSourceCalibrationAndPixelVariants)
{
    WorkflowImageCache cache(512 * 1024);
    std::atomic<int> builds{0};
    const auto load = [&](const WorkflowImageCacheKey& key) {
        return cache.load(key, [&]() {
            ++builds;
            return makeImage();
        });
    };

    ASSERT_TRUE(load(makeDecodedKey(QStringLiteral("source-a"))));
    ASSERT_TRUE(load(makeDecodedKey(QStringLiteral("source-b"))));
    ASSERT_TRUE(load(makeCalibratedKey(QStringLiteral("source-b"), QStringLiteral("masters-a"))));
    ASSERT_TRUE(load(makeCalibratedKey(QStringLiteral("source-b"), QStringLiteral("masters-b"))));
    ASSERT_TRUE(load(makeCalibratedKey(QStringLiteral("source-b"), QStringLiteral("masters-b"),
                                       QStringLiteral("calibrated-double-v1"))));

    EXPECT_EQ(builds.load(), 5);
    EXPECT_GE(cache.diagnostics().generationInvalidations, 3u);
}

TEST_F(WorkflowImageCacheP213, SourceReplacementDuringPhysicalLoadIsNotPublishedUnderStaleGeneration)
{
    WorkflowImageCache cache(256 * 1024);
    std::atomic<bool> generation_current{true};
    const auto key = makeDecodedKey(QStringLiteral("source-a"));

    const auto loaded = cache.load(
        key,
        [&]() {
            generation_current.store(false);
            return makeImage();
        },
        {}, {}, [&]() { return generation_current.load(); });

    EXPECT_FALSE(loaded);
    EXPECT_EQ(cache.diagnostics().publications, 0u);
    EXPECT_EQ(cache.diagnostics().generationInvalidations, 1u);
}

TEST_F(WorkflowImageCacheP213, EnforcesStrictCapWithDeterministicLruEviction)
{
    const auto key_a = makeDecodedKey(QStringLiteral("a"));
    const auto key_b = makeDecodedKey(QStringLiteral("b"));
    const auto key_c = makeDecodedKey(QStringLiteral("c"));
    const auto sizing_image = makeImage();
    ASSERT_TRUE(sizing_image);
    const std::uint64_t one_entry = WorkflowImageCache::retainedBytes(key_a, *sizing_image);
    WorkflowImageCache cache(one_entry * 2);
    std::atomic<int> builds_a{0};
    std::atomic<int> builds_b{0};
    std::atomic<int> builds_c{0};

    cache
        .load(key_a,
              [&]() {
                  ++builds_a;
                  return makeImage();
              })
        .reset();
    cache
        .load(key_b,
              [&]() {
                  ++builds_b;
                  return makeImage();
              })
        .reset();
    cache
        .load(key_a,
              [&]() {
                  ++builds_a;
                  return makeImage();
              })
        .reset();
    cache
        .load(key_c,
              [&]() {
                  ++builds_c;
                  return makeImage();
              })
        .reset();
    cache
        .load(key_b,
              [&]() {
                  ++builds_b;
                  return makeImage();
              })
        .reset();

    EXPECT_EQ(builds_a.load(), 1);
    EXPECT_EQ(builds_b.load(), 2);
    EXPECT_EQ(builds_c.load(), 1);
    const auto diagnostics = cache.diagnostics();
    EXPECT_EQ(diagnostics.evictions, 2u);
    EXPECT_LE(diagnostics.peakBytes, cache.capacityBytes());
    EXPECT_LE(diagnostics.currentBytes, cache.capacityBytes());
}

// An entry is accounted as the bytes its image's storage took from the engine plus the cache's own bookkeeping, so a
// larger image is charged exactly the difference in storage and one larger than the cap is handed back uncached.
TEST_F(WorkflowImageCacheP213, RetainedBytesIncludeTheImageStorage)
{
    const auto key = makeDecodedKey(QStringLiteral("storage-accounting"));
    const auto small_image = makeImage(64);
    const auto large_image = makeImage(128);
    ASSERT_TRUE(small_image && large_image);
    const std::uint64_t small_storage = storageCapacityOf(*small_image);
    const std::uint64_t large_storage = storageCapacityOf(*large_image);
    ASSERT_GE(large_storage, small_storage + 64u * 64u * sizeof(float));

    const std::uint64_t small_retained = WorkflowImageCache::retainedBytes(key, *small_image);
    const std::uint64_t large_retained = WorkflowImageCache::retainedBytes(key, *large_image);
    EXPECT_GE(small_retained, small_storage);
    EXPECT_EQ(large_retained - small_retained, large_storage - small_storage);

    WorkflowImageCache cache(small_retained + 1024u);
    EXPECT_EQ(cache.load(key, [&]() { return large_image; }, []() { return false; }), large_image);
    const auto diagnostics = cache.diagnostics();
    EXPECT_EQ(diagnostics.oversizedBypasses, 1u);
    EXPECT_EQ(diagnostics.publications, 0u);
    EXPECT_EQ(diagnostics.currentBytes, 0u);
}

TEST_F(WorkflowImageCacheP213, TinyEntriesChargeConservativeContainerAndOwnershipStructure)
{
    const auto key = makeDecodedKey(QStringLiteral("tiny-generation"));
    const auto image = makeImage(1, 1);
    ASSERT_TRUE(image);
    const std::uint64_t retained = WorkflowImageCache::retainedBytes(key, *image);
    const std::uint64_t dynamic_keys =
        static_cast<std::uint64_t>(key.identity().capacity() + key.baseIdentity().capacity() + 2) * sizeof(QChar);
    const std::uint64_t known_payload = storageCapacityOf(*image) + dynamic_keys;

    EXPECT_GE(retained, known_payload + 512u);

    constexpr size_t kEntryCount = 64;
    WorkflowImageCache cache(retained * kEntryCount);
    for (size_t index = 0; index < kEntryCount; ++index)
    {
        const auto indexed_key = makeDecodedKey(QStringLiteral("tiny-generation-%1").arg(index));
        ASSERT_TRUE(cache.load(indexed_key, [this]() { return makeImage(1, 1); }));
    }
    const auto diagnostics = cache.diagnostics();
    EXPECT_LE(diagnostics.currentBytes, cache.capacityBytes());
    EXPECT_LE(diagnostics.peakBytes, cache.capacityBytes());
}

TEST_F(WorkflowImageCacheP213, OversizedEntryBypassesWithoutPublication)
{
    WorkflowImageCache cache(1);
    const auto key = makeDecodedKey(QStringLiteral("source-a"));
    std::atomic<int> loads{0};

    ASSERT_TRUE(cache.load(key, [&]() {
        ++loads;
        return makeImage();
    }));
    ASSERT_TRUE(cache.load(key, [&]() {
        ++loads;
        return makeImage();
    }));

    EXPECT_EQ(loads.load(), 2);
    const auto diagnostics = cache.diagnostics();
    EXPECT_EQ(diagnostics.oversizedBypasses, 2u);
    EXPECT_EQ(diagnostics.publications, 0u);
    EXPECT_EQ(diagnostics.currentBytes, 0u);
}

TEST_F(WorkflowImageCacheP213, EvictedBorrowerKeepsMemoryTokenUntilImageRelease)
{
    const auto key_a = makeDecodedKey(QStringLiteral("a"));
    const auto key_b = makeDecodedKey(QStringLiteral("b"));
    const auto sizing_image = makeImage();
    ASSERT_TRUE(sizing_image);
    const std::uint64_t capacity = WorkflowImageCache::retainedBytes(key_a, *sizing_image);
    WorkflowImageCache cache(capacity);

    auto borrower = cache.load(key_a, [this] { return makeImage(); });
    ASSERT_TRUE(borrower);
    auto bypassed = cache.load(key_b, [this] { return makeImage(); });
    ASSERT_TRUE(bypassed);
    EXPECT_EQ(cache.diagnostics().currentBytes, capacity);
    EXPECT_EQ(cache.diagnostics().capacityBypasses, 1u);

    cache.clear();
    EXPECT_EQ(cache.diagnostics().currentBytes, capacity);
    borrower.reset();
    EXPECT_EQ(cache.diagnostics().currentBytes, 0u);
}

TEST_F(WorkflowImageCacheP213, FailureCancellationAndIncompatibilityDoNotPublishPartialEntries)
{
    WorkflowImageCache cache(256 * 1024);
    const auto failed_key = makeDecodedKey(QStringLiteral("failed"));
    const auto cancelled_key = makeCalibratedKey(QStringLiteral("cancelled"), QStringLiteral("masters"));
    const auto incompatible_key = makeDecodedKey(QStringLiteral("incompatible"));
    const auto width_is_32 = [](const an::consumer::AnyImage& image) { return widthOf(image) == 32u; };

    EXPECT_FALSE(cache.load(failed_key, [] { return WorkflowCachedImage{}; }));
    EXPECT_THROW((void)cache.load(failed_key, []() -> WorkflowCachedImage { throw std::runtime_error("load failed"); }),
                 std::runtime_error);
    std::atomic<bool> cancelled{false};
    EXPECT_TRUE(cache.load(
        cancelled_key,
        [&]() {
            cancelled.store(true);
            return makeImage();
        },
        [&]() { return cancelled.load(); }));
    EXPECT_FALSE(cache.load(incompatible_key, [this] { return makeImage(); }, {}, width_is_32));
    EXPECT_FALSE(cache.load(incompatible_key, [this] { return makeImage(); }, {}, width_is_32));
    EXPECT_THROW((void)cache.load(
                     incompatible_key, [this] { return makeImage(); }, {},
                     [](const an::consumer::AnyImage&) -> bool { throw std::runtime_error("validation failed"); }),
                 std::runtime_error);
    EXPECT_TRUE(cache.load(incompatible_key, [this] { return makeImage(); }));
    cache.clear();

    const auto diagnostics = cache.diagnostics();
    EXPECT_EQ(diagnostics.publications, 1u);
    EXPECT_EQ(diagnostics.failedBuilds, 5u);
    EXPECT_EQ(diagnostics.cancelledBuilds, 1u);
    EXPECT_EQ(diagnostics.currentBytes, 0u);
}

TEST_F(WorkflowImageCacheP213, CancellationDuringBuildReturnsTheUnpublishedProduct)
{
    WorkflowImageCache cache(256 * 1024);
    const auto key = makeDecodedKey(QStringLiteral("cancelled-after-load"));
    std::atomic<bool> cancelled{false};
    std::atomic<int> loads{0};

    auto cancelled_product = cache.load(
        key,
        [&]() {
            ++loads;
            cancelled.store(true);
            return makeImage();
        },
        [&]() { return cancelled.load(); });

    ASSERT_TRUE(cancelled_product);
    EXPECT_EQ(loads.load(), 1);
    auto diagnostics = cache.diagnostics();
    EXPECT_EQ(diagnostics.cancelledBuilds, 1u);
    EXPECT_EQ(diagnostics.publications, 0u);
    EXPECT_EQ(diagnostics.currentBytes, 0u);

    cancelled.store(false);
    auto published_product = cache.load(key, [&]() {
        ++loads;
        return makeImage();
    });
    ASSERT_TRUE(published_product);
    EXPECT_EQ(loads.load(), 2);
    diagnostics = cache.diagnostics();
    EXPECT_EQ(diagnostics.publications, 1u);
    EXPECT_GT(diagnostics.currentBytes, 0u);
}

TEST_F(WorkflowImageCacheP213, ThrowingPreBuildCallbacksCannotPoisonSingleFlight)
{
    WorkflowImageCache cache(256 * 1024);
    const auto key = makeDecodedKey(QStringLiteral("throwing-generation"));
    EXPECT_THROW((void)cache.load(
                     key, [this] { return makeImage(); }, {}, {},
                     []() -> bool { throw std::runtime_error("generation failed"); }),
                 std::runtime_error);

    std::atomic<int> loads{0};
    auto recovered = cache.load(key, [&]() {
        ++loads;
        return makeImage();
    });
    ASSERT_TRUE(recovered);
    EXPECT_EQ(loads.load(), 1);
    EXPECT_EQ(cache.diagnostics().failedBuilds, 1u);
    EXPECT_EQ(cache.diagnostics().sameKeyWaits, 0u);
}

TEST_F(WorkflowImageCacheP213, PreCancelledLoadNeverInvokesLoaderOrReturnsCachedHit)
{
    WorkflowImageCache cache(256 * 1024);
    const auto key = makeDecodedKey(QStringLiteral("pre-cancelled"));
    std::atomic<int> loads{0};
    const auto loader = [&]() {
        ++loads;
        return makeImage();
    };

    ASSERT_TRUE(cache.load(key, loader));
    ASSERT_EQ(loads.load(), 1);

    const auto cancelled = [] { return true; };
    EXPECT_FALSE(cache.load(key, loader, cancelled));
    EXPECT_EQ(loads.load(), 1);
    EXPECT_EQ(cache.diagnostics().cancelledBuilds, 1u);
}

TEST_F(WorkflowImageCacheP213, RevalidatesCompatibilityBeforeReturningCachedHit)
{
    WorkflowImageCache cache(256 * 1024);
    const auto key = makeDecodedKey(QStringLiteral("compatibility-hit"));
    std::atomic<int> loads{0};
    const auto loader = [&]() {
        ++loads;
        return makeImage();
    };

    ASSERT_TRUE(cache.load(key, loader));
    EXPECT_FALSE(
        cache.load(key, loader, {}, [](const an::consumer::AnyImage& image) { return widthOf(image) == 32u; }));
    EXPECT_TRUE(cache.load(key, loader, {}, [](const an::consumer::AnyImage& image) { return widthOf(image) == 64u; }));

    EXPECT_EQ(loads.load(), 3);
    const auto diagnostics = cache.diagnostics();
    EXPECT_EQ(diagnostics.hits, 0u);
    EXPECT_EQ(diagnostics.publications, 2u);
    EXPECT_EQ(diagnostics.failedBuilds, 1u);
}

TEST_F(WorkflowImageCacheP213, SameKeyConcurrentRequestsPublishOnlyOnce)
{
    std::promise<void> waiter_started;
    WorkflowImageCache cache(256 * 1024, [&waiter_started]() { waiter_started.set_value(); });
    const auto key = makeCalibratedKey(QStringLiteral("source"), QStringLiteral("masters"));
    std::atomic<int> calibrations{0};
    std::promise<void> started;
    std::promise<void> release_promise;
    const std::shared_future<void> release = release_promise.get_future().share();

    auto first = std::async(std::launch::async, [&]() {
        return cache.load(key, [&]() {
            ++calibrations;
            started.set_value();
            release.wait();
            return makeImage();
        });
    });
    started.get_future().wait();
    auto second = std::async(std::launch::async, [&]() {
        return cache.load(key, [&]() {
            ++calibrations;
            return makeImage();
        });
    });
    waiter_started.get_future().wait();
    EXPECT_EQ(cache.diagnostics().sameKeyWaits, 1u);
    release_promise.set_value();

    auto first_image = first.get();
    auto second_image = second.get();
    ASSERT_TRUE(first_image && second_image);
    EXPECT_EQ(first_image.get(), second_image.get());
    EXPECT_EQ(calibrations.load(), 1);
    EXPECT_EQ(cache.diagnostics().sameKeyWaits, 1u);
    EXPECT_EQ(cache.diagnostics().publications, 1u);
}

TEST_F(WorkflowImageCacheP213, SameKeyConcurrentOversizedRequestsShareOneUnpublishedBuild)
{
    std::promise<void> waiter_started;
    WorkflowImageCache cache(1, [&waiter_started]() { waiter_started.set_value(); });
    const auto key = makeDecodedKey(QStringLiteral("oversized-concurrent"));
    std::atomic<int> loads{0};
    std::promise<void> started;
    std::promise<void> release_promise;
    const auto release = release_promise.get_future().share();

    auto first = std::async(std::launch::async, [&]() {
        return cache.load(key, [&]() {
            ++loads;
            started.set_value();
            release.wait();
            return makeImage();
        });
    });
    started.get_future().wait();
    auto second = std::async(std::launch::async, [&]() {
        return cache.load(key, [&]() {
            ++loads;
            return makeImage();
        });
    });
    waiter_started.get_future().wait();
    release_promise.set_value();

    const auto first_image = first.get();
    const auto second_image = second.get();
    ASSERT_TRUE(first_image && second_image);
    EXPECT_EQ(first_image.get(), second_image.get());
    EXPECT_EQ(loads.load(), 1);
    EXPECT_EQ(cache.diagnostics().oversizedBypasses, 1u);
    EXPECT_EQ(cache.diagnostics().publications, 0u);
}

TEST_F(WorkflowImageCacheP213, SameKeyConcurrentCapacityBypassesShareOneUnpublishedBuild)
{
    const auto retained_key = makeDecodedKey(QStringLiteral("retained"));
    const auto bypassed_key = makeDecodedKey(QStringLiteral("bypassed"));
    const auto sizing_image = makeImage();
    ASSERT_TRUE(sizing_image);
    const std::uint64_t capacity = WorkflowImageCache::retainedBytes(retained_key, *sizing_image);
    std::promise<void> waiter_started;
    WorkflowImageCache cache(capacity, [&waiter_started]() { waiter_started.set_value(); });
    auto borrower = cache.load(retained_key, [this] { return makeImage(); });
    ASSERT_TRUE(borrower);

    std::atomic<int> bypassed_loads{0};
    std::promise<void> started;
    std::promise<void> release_promise;
    const auto release = release_promise.get_future().share();
    auto first = std::async(std::launch::async, [&]() {
        return cache.load(bypassed_key, [&]() {
            ++bypassed_loads;
            started.set_value();
            release.wait();
            return makeImage(64, 0.5f);
        });
    });
    started.get_future().wait();
    auto second = std::async(std::launch::async, [&]() {
        return cache.load(bypassed_key, [&]() {
            ++bypassed_loads;
            return makeImage(64, 0.5f);
        });
    });
    waiter_started.get_future().wait();
    release_promise.set_value();

    const auto first_image = first.get();
    const auto second_image = second.get();
    ASSERT_TRUE(first_image && second_image);
    EXPECT_EQ(first_image.get(), second_image.get());
    EXPECT_EQ(bypassed_loads.load(), 1);
    EXPECT_EQ(cache.diagnostics().capacityBypasses, 1u);
    borrower.reset();
}

TEST_F(WorkflowImageCacheP213, SameKeyConcurrentFailureIsSharedWithoutDuplicateBuild)
{
    std::promise<void> waiter_started;
    WorkflowImageCache cache(256 * 1024, [&waiter_started]() { waiter_started.set_value(); });
    const auto key = makeDecodedKey(QStringLiteral("failed-concurrent"));
    std::atomic<int> loads{0};
    std::promise<void> started;
    std::promise<void> release_promise;
    const auto release = release_promise.get_future().share();

    auto first = std::async(std::launch::async, [&]() {
        return cache.load(key, [&]() -> WorkflowCachedImage {
            ++loads;
            started.set_value();
            release.wait();
            throw std::runtime_error("shared failure");
        });
    });
    started.get_future().wait();
    auto second = std::async(std::launch::async, [&]() {
        return cache.load(key, [&]() {
            ++loads;
            return makeImage();
        });
    });
    waiter_started.get_future().wait();
    release_promise.set_value();

    EXPECT_THROW((void)first.get(), std::runtime_error);
    EXPECT_THROW((void)second.get(), std::runtime_error);
    EXPECT_EQ(loads.load(), 1);
    EXPECT_EQ(cache.diagnostics().failedBuilds, 1u);
    EXPECT_EQ(cache.diagnostics().publications, 0u);
}

TEST_F(WorkflowImageCacheP213, ClearDuringBuildPreventsLatePublication)
{
    WorkflowImageCache cache(256 * 1024);
    const auto key = makeDecodedKey(QStringLiteral("clear-in-flight"));
    std::atomic<int> loads{0};
    std::promise<void> started;
    std::promise<void> release_promise;
    const auto release = release_promise.get_future().share();

    auto building = std::async(std::launch::async, [&]() {
        return cache.load(key, [&]() {
            ++loads;
            started.set_value();
            release.wait();
            return makeImage();
        });
    });
    started.get_future().wait();
    cache.clear();
    auto after_clear = std::async(std::launch::async, [&]() {
        return cache.load(key, [&]() {
            ++loads;
            return makeImage(64, 0.75f);
        });
    });
    const auto after_clear_image = after_clear.get();
    ASSERT_TRUE(after_clear_image);
    EXPECT_FLOAT_EQ(firstSampleOf(*after_clear_image), 0.75f);
    release_promise.set_value();

    const auto pre_clear_image = building.get();
    ASSERT_TRUE(pre_clear_image);
    EXPECT_NE(pre_clear_image.get(), after_clear_image.get());
    EXPECT_EQ(loads.load(), 2);
    EXPECT_EQ(cache.diagnostics().publications, 1u);
    EXPECT_GT(cache.diagnostics().currentBytes, 0u);
}

TEST_F(WorkflowImageCacheP213, SeparateRunInstancesNeverReuseProducts)
{
    const auto key = makeDecodedKey(QStringLiteral("source"));
    std::atomic<int> loads{0};
    {
        WorkflowImageCache first_run(256 * 1024);
        ASSERT_TRUE(first_run.load(key, [&]() {
            ++loads;
            return makeImage();
        }));
    }
    {
        WorkflowImageCache second_run(256 * 1024);
        ASSERT_TRUE(second_run.load(key, [&]() {
            ++loads;
            return makeImage();
        }));
    }
    EXPECT_EQ(loads.load(), 2);
}

TEST_F(WorkflowImageCacheP213, ExecutorOwnsOneCachePerRunAndReportsCleanup)
{
    QTemporaryDir temporary_root;
    ASSERT_TRUE(temporary_root.isValid());
    const WorkflowManifest manifest = makeExecutorManifest(temporary_root.path());
    std::atomic<int> physical_loads{0};
    WorkflowExecutor executor;
    executor.addStage(std::make_unique<ExecutorCacheProbe>(physical_loads, StageResult::Status::Success, imageMaker()));
    WorkflowExecutionOptions options;
    options.collectCounters = true;
    options.workflowCacheCapacityBytes = 256 * 1024;

    const auto first = executor.execute(manifest, {}, options);
    const auto second = executor.execute(manifest, {}, options);

    ASSERT_TRUE(first.workflowCacheDiagnostics.has_value());
    ASSERT_TRUE(second.workflowCacheDiagnostics.has_value());
    EXPECT_EQ(physical_loads.load(), 2);
    EXPECT_EQ(first.workflowCacheDiagnostics->physicalDecodedLoads, 1u);
    EXPECT_EQ(second.workflowCacheDiagnostics->physicalDecodedLoads, 1u);
    EXPECT_EQ(first.workflowCacheDiagnostics->hits, 1u);
    EXPECT_EQ(second.workflowCacheDiagnostics->hits, 1u);
    EXPECT_EQ(first.workflowCacheDiagnostics->lookupEntries, 0u);
    EXPECT_EQ(second.workflowCacheDiagnostics->lookupEntries, 0u);
    EXPECT_EQ(first.workflowCacheDiagnostics->currentBytes, 0u);
    EXPECT_EQ(second.workflowCacheDiagnostics->currentBytes, 0u);
    EXPECT_EQ(first.workflowCacheDiagnostics->cleanupCalls, 1u);
    EXPECT_EQ(second.workflowCacheDiagnostics->cleanupCalls, 1u);
}

TEST_F(WorkflowImageCacheP213, ExecutorFailureAndCancellationClearPublishedProducts)
{
    QTemporaryDir temporary_root;
    ASSERT_TRUE(temporary_root.isValid());
    const WorkflowManifest manifest = makeExecutorManifest(temporary_root.path());
    WorkflowExecutionOptions options;
    options.collectCounters = true;
    options.workflowCacheCapacityBytes = 256 * 1024;

    std::atomic<int> failure_loads{0};
    WorkflowExecutor failing_executor;
    failing_executor.addStage(
        std::make_unique<ExecutorCacheProbe>(failure_loads, StageResult::Status::Failure, imageMaker()));
    const auto failed = failing_executor.execute(manifest, {}, options);
    ASSERT_TRUE(failed.workflowCacheDiagnostics.has_value());
    EXPECT_FALSE(failed.success);
    EXPECT_EQ(failed.workflowCacheDiagnostics->lookupEntries, 0u);
    EXPECT_EQ(failed.workflowCacheDiagnostics->currentBytes, 0u);

    std::atomic<int> cancellation_loads{0};
    WorkflowExecutor cancelling_executor;
    cancelling_executor.addStage(
        std::make_unique<ExecutorCacheProbe>(cancellation_loads, StageResult::Status::Cancelled, imageMaker()));
    const auto cancelled = cancelling_executor.execute(manifest, {}, options);
    ASSERT_TRUE(cancelled.workflowCacheDiagnostics.has_value());
    EXPECT_TRUE(cancelled.cancelled);
    EXPECT_EQ(cancelled.workflowCacheDiagnostics->lookupEntries, 0u);
    EXPECT_EQ(cancelled.workflowCacheDiagnostics->currentBytes, 0u);
}
