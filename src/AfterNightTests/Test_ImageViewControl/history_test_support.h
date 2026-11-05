// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// What the history suites share: a session, a view with no widget, a writer the test decides when to let through, and
// small images whose every sample is known, held as assets of the case's session.

#include <gtest/gtest.h>

#include "Test_ControlsViewer/ControlsViewerTestSupport.h"

#include "controls/ImageHistoryBackupQueue.h"
#include "controls/ImageHistoryHost.h"
#include "controls/ImageHistoryTracker.h"
#include "controls/SequenceContainers.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace an::tests::history
{

using an::tests::viewer::kPatience;
using an::tests::viewer::kQuietPeriod;
using an::tests::viewer::pumpQuietly;
using an::tests::viewer::pumpUntil;
using an::tests::viewer::TestSessionProvider;

/// A session of two threads with the allowances and the engine budget the case states; fails the case when it cannot
/// be opened.
[[nodiscard]] inline std::optional<TestSessionProvider>
openHistorySession(const an::consumer::MemoryAllowances& memory = {}, const std::uint64_t engineBudget = 0u)
{
    an::consumer::EngineSessionSettings settings;
    settings.maxConcurrency = 2u;
    settings.memory = memory;
    if (engineBudget != 0u)
    {
        settings.limits.memoryBudgetBytes = engineBudget;
    }
    an::Result<an::consumer::EngineSession> session = an::consumer::EngineSession::open(settings);
    EXPECT_TRUE(session.hasValue()) << session.status().message();
    if (!session.hasValue())
    {
        return std::nullopt;
    }
    return std::optional<TestSessionProvider>(std::in_place, std::move(*session));
}

/// A view the history records: what it shows is a public field, and every restore is counted.
class FakeHistoryView final : public an::ui::history::ImageHistoryHost
{
public:
    std::string name = "M42";
    std::uint64_t document = 1u;
    std::shared_ptr<an::consumer::ImageAsset> image;
    std::optional<an::ui::DocumentSequence> sequence;
    bool autoDebayer = false;
    std::size_t imageRestores = 0;
    std::size_t sequenceRestores = 0;
    bool refuseRestores = false;

    [[nodiscard]] std::string historyName() const override
    {
        return name;
    }

    void setHistoryName(const std::string& newName) override
    {
        name = newName;
    }

    [[nodiscard]] std::uint64_t historyDocument() const override
    {
        return document;
    }

    [[nodiscard]] std::shared_ptr<const an::consumer::ImageAsset> historyAsset() const override
    {
        return image;
    }

    [[nodiscard]] std::optional<an::ui::DocumentSequence> historySequence() const override
    {
        return sequence;
    }

    [[nodiscard]] bool historyAutoDebayer() const override
    {
        return autoDebayer;
    }

    void restoreHistoryImage(an::consumer::ImageAsset&& restored, an::ui::history::RestoreDone done) override
    {
        if (refuseRestores)
        {
            done(an::Status::failure(an::ErrorCode::InvalidState, "the test refused the restore"));
            return;
        }
        image = std::make_shared<an::consumer::ImageAsset>(std::move(restored));
        ++imageRestores;
        done(an::Status::success());
    }

    void restoreHistorySequence(std::optional<an::ui::DocumentSequence> restored, const std::uint64_t index,
                                an::consumer::ImageAsset&& frame, an::ui::history::RestoreDone done) override
    {
        if (refuseRestores)
        {
            done(an::Status::failure(an::ErrorCode::InvalidState, "the test refused the restore"));
            return;
        }
        if (restored.has_value())
        {
            sequence = std::move(restored);
        }
        sequence->currentFrame = index;
        image = std::make_shared<an::consumer::ImageAsset>(std::move(frame));
        ++sequenceRestores;
        done(an::Status::success());
    }

    void historyChanged() override {}

    void activateForHistory() override {}

    [[nodiscard]] QWidget* historyDialogParent() const override
    {
        return nullptr;
    }
};

/// Turns the event loop until the tracker's restore has landed; false when it never does.
[[nodiscard]] inline bool settleRestore(const ImageHistoryTracker& tracker)
{
    return pumpUntil([&tracker]() { return !tracker.isRestoring(); });
}

/// Waits, polling, until `condition` holds; false when it has not held after a bound no passing run comes near.
inline bool waitUntil(const std::function<bool()>& condition)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!condition())
    {
        if (std::chrono::steady_clock::now() > deadline)
        {
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return true;
}

/// Lets a test decide when the backup writer may write and whether a write succeeds. A closed gate holds every write
/// at its start until the test opens it or the write is stopped; a refused write reports a failure and writes nothing.
class WriterGate
{
public:
    [[nodiscard]] an::ui::history::SnapshotWriter writer()
    {
        return
            [this](const an::consumer::EngineSession& session, const an::consumer::AssetReadLease& image,
                   const std::string& path, const an::ArtifactIdentity& identity, std::stop_token stop) -> an::Status {
                bool refuse = false;
                {
                    std::unique_lock lock(m_Mutex);
                    ++m_Started;
                    m_Changed.notify_all();
                    if (!m_Changed.wait_for(lock, stop, std::chrono::seconds(30), [this] { return m_Open; }) &&
                        !stop.stop_requested())
                    {
                        ADD_FAILURE() << "a history write waited at a gate nobody opened";
                    }

                    if (m_RefuseAll || m_RefuseNext > 0)
                    {
                        refuse = true;
                        if (m_RefuseNext > 0)
                        {
                            --m_RefuseNext;
                        }
                    }

                    m_Paths.push_back(path);
                }

                if (refuse)
                {
                    return an::Status::failure(an::ErrorCode::ExternalFailure, "the test refused this write");
                }

                return an::ui::history::writeHistorySnapshot(session, image, path, identity, stop);
            };
    }

    void close()
    {
        std::lock_guard lock(m_Mutex);
        m_Open = false;
    }

    void open()
    {
        {
            std::lock_guard lock(m_Mutex);
            m_Open = true;
        }
        m_Changed.notify_all();
    }

    /// Every write from now on fails, or none does.
    void refuseAll(const bool refuse)
    {
        std::lock_guard lock(m_Mutex);
        m_RefuseAll = refuse;
    }

    /// The next `count` writes fail.
    void refuseNext(const std::size_t count)
    {
        std::lock_guard lock(m_Mutex);
        m_RefuseNext = count;
    }

    [[nodiscard]] std::size_t started() const
    {
        std::lock_guard lock(m_Mutex);
        return m_Started;
    }

    /// Waits until `count` writes have started.
    [[nodiscard]] bool waitForStarted(const std::size_t count)
    {
        std::unique_lock lock(m_Mutex);
        return m_Changed.wait_for(lock, std::chrono::seconds(30), [this, count] { return m_Started >= count; });
    }

    [[nodiscard]] std::vector<std::string> paths() const
    {
        std::lock_guard lock(m_Mutex);
        return m_Paths;
    }

private:
    mutable std::mutex m_Mutex;
    std::condition_variable_any m_Changed;
    bool m_Open = true;
    bool m_RefuseAll = false;
    std::size_t m_RefuseNext = 0;
    std::size_t m_Started = 0;
    std::vector<std::string> m_Paths;
};

/// Opens a gate when it goes out of scope, so a failing test never leaves a writer waiting on it. Declare it after
/// whatever owns the writer: it is then destroyed first.
class GateOpener
{
public:
    explicit GateOpener(WriterGate& gate) : m_Gate(gate) {}
    ~GateOpener()
    {
        m_Gate.open();
    }

    GateOpener(const GateOpener&) = delete;
    GateOpener& operator=(const GateOpener&) = delete;

private:
    WriterGate& m_Gate;
};

/// A Float32 image whose sample at (x, y, c) is `base + x + 16 y + 256 c`, its object named `objectName`.
inline std::shared_ptr<an::consumer::ImageAsset> makeImage(const an::consumer::EngineSession& session,
                                                           const std::uint32_t width, const std::uint32_t height,
                                                           const std::uint32_t channels, const float base,
                                                           const std::string& objectName = {})
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    EXPECT_TRUE(hold.hasValue()) << hold.status().message();
    if (!hold.hasValue())
    {
        return nullptr;
    }
    const std::optional<an::ImageDescriptor> descriptor = an::tests::viewer::describeImage(
        hold->engine(), an::SampleType::Float32, width, height,
        channels == 1u ? an::tests::viewer::Channels::Grey : an::tests::viewer::Channels::Rgb,
        an::NumericDomain::PhysicalScaled);
    EXPECT_TRUE(descriptor.has_value());
    if (!descriptor.has_value())
    {
        return nullptr;
    }
    an::ImageMetadata metadata;
    if (!objectName.empty())
    {
        an::Result<an::AcquisitionMetadata> acquisition =
            an::AcquisitionMetadata::create(an::AcquisitionMetadataRequest{.objectName = objectName});
        EXPECT_TRUE(acquisition.hasValue()) << acquisition.status().message();
        if (acquisition.hasValue())
        {
            metadata = an::ImageMetadata::create(std::move(*acquisition), {}, {}, {}, {}, std::nullopt);
        }
    }
    an::Result<an::Image<float>> image =
        an::Image<float>::create(hold->engine(), *descriptor, an::StorageArrangement::Planar,
                                 an::StorageInitialization::Zeroed, std::move(metadata));
    EXPECT_TRUE(image.hasValue()) << image.status().message();
    if (!image.hasValue())
    {
        return nullptr;
    }
    {
        std::optional<an::ImageWriteAccess<float>> access = image->write();
        for (std::uint32_t c = 0u; c < channels; ++c)
        {
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                const auto row = access->view().row(c, y);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    (*row)[x] = base + static_cast<float>(x + 16u * y + 256u * c);
                }
            }
        }
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage{std::move(*image)});
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    return asset.hasValue() ? std::make_shared<an::consumer::ImageAsset>(std::move(*asset)) : nullptr;
}

/// A 16-bit mono frame whose sample at (x, y) is `base + x + 16 y`.
inline std::shared_ptr<an::consumer::ImageAsset> makeFrame(const an::consumer::EngineSession& session,
                                                           const std::uint32_t width, const std::uint32_t height,
                                                           const std::uint16_t base)
{
    std::optional<an::consumer::ImageAsset> frame =
        an::tests::viewer::makeAsset<std::uint16_t>(session, width, height, an::tests::viewer::Channels::Grey,
                                                    [base](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                                                        return static_cast<std::uint16_t>(base + x + 16u * y);
                                                    });
    return frame.has_value() ? std::make_shared<an::consumer::ImageAsset>(std::move(*frame)) : nullptr;
}

/// Writes a SER sequence of the frames, in order, published at `path`; fails the case when it cannot. SER keeps a
/// 16-bit frame's samples exactly.
inline void writeSerFrames(const an::consumer::EngineSession& session, const QString& path,
                           const std::vector<std::shared_ptr<an::consumer::ImageAsset>>& frames)
{
    an::Result<an::ui::SequenceWriter> writer = an::ui::SequenceWriter::create(
        path, an::ui::SequenceWriteSettings{.container = an::SequenceFormat::Ser, .frameCount = frames.size()});
    ASSERT_TRUE(writer.hasValue()) << writer.status().message();
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    ASSERT_TRUE(scope.has_value());
    for (const std::shared_ptr<an::consumer::ImageAsset>& frame : frames)
    {
        ASSERT_NE(frame, nullptr);
        an::Result<an::consumer::AssetReadLease> lease = frame->read();
        ASSERT_TRUE(lease.hasValue()) << lease.status().message();
        const an::Status appended = writer->append(scope->engine(), scope->execution(), lease->image());
        ASSERT_TRUE(appended.ok()) << appended.message();
    }
    an::Result<an::SequenceDescriptor> published = writer->finish(scope->execution());
    ASSERT_TRUE(published.hasValue()) << published.status().message();
}

/// Every sample, channel by channel, row by row.
inline std::vector<double> samplesOf(const an::consumer::ImageAsset& image)
{
    std::vector<double> samples;
    an::Result<an::consumer::AssetReadLease> lease = image.read();
    if (!lease.hasValue())
    {
        return samples;
    }
    lease->visit([&samples](const auto& typed) {
        const auto view = typed.view();
        if (!view.has_value())
        {
            return;
        }
        for (std::uint32_t c = 0u; c < view->planeCount(); ++c)
        {
            for (std::uint32_t y = 0u; y < view->height(); ++y)
            {
                const auto row = view->row(c, y);
                for (const auto sample : *row)
                {
                    samples.push_back(static_cast<double>(sample));
                }
            }
        }
    });
    return samples;
}

/// Every sample of a shared asset; empty for none.
inline std::vector<double> samplesOf(const std::shared_ptr<const an::consumer::ImageAsset>& image)
{
    return image ? samplesOf(*image) : std::vector<double>{};
}

/// The object the image's acquisition record names.
inline std::string objectNameOf(const an::consumer::ImageAsset& image)
{
    std::string name;
    an::Result<an::consumer::AssetReadLease> lease = image.read();
    if (lease.hasValue())
    {
        lease->visit([&name](const auto& typed) { name = typed.metadata().acquisition().objectName(); });
    }
    return name;
}

/// Overwrites every sample of a Float32 image with `value` through its writer, as an edit that writes in place would.
inline void overwriteSamples(an::consumer::ImageAsset& image, const float value)
{
    an::Result<an::consumer::AssetWriteLease> writer = image.write();
    ASSERT_TRUE(writer.hasValue()) << writer.status().message();
    std::optional<an::ImageWriteAccess<float>> access = writer->write<float>();
    ASSERT_TRUE(access.has_value());
    for (std::uint32_t c = 0u; c < access->view().planeCount(); ++c)
    {
        for (std::uint32_t y = 0u; y < access->view().height(); ++y)
        {
            const auto row = access->view().row(c, y);
            for (float& sample : *row)
            {
                sample = value;
            }
        }
    }
}

/// Runs an edit's preparation on a worker, holding the write lease on the image it replaces, and records the edit on
/// the calling thread, as the view does: true when the edit was recorded and may be shown.
inline bool recordEdit(ImageHistoryTracker& tracker, an::consumer::ImageAsset& replaced,
                       const an::consumer::ImageAsset& edited, const std::string& stepName)
{
    ImageHistoryTracker::EditPreparation prepare = tracker.editPreparation(stepName);
    an::Result<an::consumer::AssetWriteLease> writer = replaced.write();
    EXPECT_TRUE(writer.hasValue()) << writer.status().message();
    if (!writer.hasValue())
    {
        return false;
    }
    an::Result<std::shared_ptr<void>> prepared = an::Status::failure(an::ErrorCode::Internal, "not prepared");
    std::thread worker([&]() { prepared = prepare(*writer, edited, {}); });
    worker.join();
    writer->release();
    if (!prepared.hasValue())
    {
        return false;
    }
    return tracker.registerEditBeforePublishing(stepName, "", edited);
}

/// The history snapshots a directory holds, below it at any depth.
inline std::set<std::string> historyFilesIn(const std::filesystem::path& directory)
{
    std::set<std::string> files;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator entry(directory, error), end; !error && entry != end;
         entry.increment(error))
    {
        if (entry->path().extension() == ".ansnap")
        {
            files.insert(entry->path().string());
        }
    }

    return files;
}

} // namespace an::tests::history
