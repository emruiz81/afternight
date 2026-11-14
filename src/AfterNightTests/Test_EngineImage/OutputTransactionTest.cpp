// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/OutputTransaction.h>

#include <AfterNightEngine/IO/ArtifactIdentity.h>
#include <AfterNightEngine/IO/CodecError.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/ImageCodec.h>
#include <AfterNightEngine/IO/PendingOutput.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include "CodecAllocationFault.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace
{
std::filesystem::path pathOf(std::string_view utf8)
{
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string utf8Of(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

/// A scratch directory that removes itself, so a failing case leaves no tree behind either.
class ScratchTree
{
public:
    explicit ScratchTree(const char* name, const std::filesystem::path& parent = std::filesystem::temp_directory_path())
    {
        static std::atomic<unsigned> counter{0u};
        const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                            std::to_string(counter.fetch_add(1u));
        m_path = parent / (std::string("an-output-") + name + "-" + unique);
        std::error_code ignored;
        std::filesystem::create_directories(m_path, ignored);
    }
    ScratchTree(const ScratchTree&) = delete;
    ScratchTree& operator=(const ScratchTree&) = delete;
    ~ScratchTree()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }
    [[nodiscard]] std::string file(const char* name) const
    {
        return utf8Of(m_path / name);
    }
    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return m_path;
    }
    [[nodiscard]] std::vector<std::string> entries() const
    {
        std::vector<std::string> names;
        std::error_code ignored;
        for (const auto& entry : std::filesystem::directory_iterator(m_path, ignored))
            names.push_back(utf8Of(entry.path().filename()));
        return names;
    }

private:
    std::filesystem::path m_path;
};

std::vector<std::byte> readAll(std::string_view utf8)
{
    std::ifstream in(pathOf(utf8), std::ios::binary);
    std::vector<std::byte> bytes;
    for (std::istreambuf_iterator<char> it(in), end; it != end; ++it)
        bytes.push_back(static_cast<std::byte>(*it));
    return bytes;
}

void writeAll(std::string_view utf8, std::string_view content)
{
    std::ofstream out(pathOf(utf8), std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

bool exists(std::string_view utf8)
{
    std::error_code ignored;
    return std::filesystem::exists(pathOf(utf8), ignored);
}

an::RawFrameDescriptor description(std::uint32_t width = 8u, std::uint32_t height = 4u)
{
    return *an::RawFrameDescriptor::create(
        {.extent = *an::ImageExtent::create(width, height, 1u, an::defaultEngineLimits()),
         .sampleFormat = {2u, an::RawSampleClass::UnsignedInteger},
         .byteOrder = an::ByteOrder::Native,
         .channelSchema = an::ChannelSchema::Mono,
         .colorSpace = an::ColorSpace::Unspecified,
         .numericDomain = an::NumericDomain::RawSensorCode,
         .cfa = {},
         .coordinateOrigin = an::CoordinateOrigin::TopLeft,
         .pixelAspectRatio = 1.0});
}

an::RawFrame makeFrame(const an::EngineContext& engine, std::uint32_t width = 8u, std::uint32_t height = 4u)
{
    const auto value = description(width, height);
    const auto layout = *an::ImageLayout::create({value.extent(), 2u, 1u, 1u, 1u}, an::defaultEngineLimits());
    auto buffer = an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Zeroed);
    EXPECT_TRUE(buffer.hasValue());
    auto frame = an::RawFrame::create(value, layout, std::move(*buffer));
    EXPECT_TRUE(frame.hasValue());
    auto bytes = frame->mutableBytes();
    EXPECT_TRUE(bytes.has_value());
    for (std::size_t i = 0u; i < bytes->size(); ++i)
        (*bytes)[i] = static_cast<std::byte>((i * 7u + 3u) & 0xffu);
    return std::move(*frame);
}

/// An encoder that serializes the frame's bytes to the private pending path, with every failure
/// the transaction has to survive available as a switch.
class FileCodec final : public an::ImageCodec
{
public:
    an::CodecCapabilities capabilities() const override
    {
        return caps;
    }
    an::Result<an::CodecImageInfo> probe(const an::EngineContext&, const an::ExecutionContext&,
                                         const an::CodecReadRequest&) const override
    {
        return an::Status::failure(an::ErrorCode::Unsupported);
    }
    an::Result<an::CodecEncodeReport> encode(const an::EngineContext&, const an::ExecutionContext&,
                                             const an::CodecEncodeRequest& request, const an::RawFrame& frame,
                                             const an::ImageMetadata& metadata) const override
    {
        ++encodes;
        {
            const std::lock_guard<std::mutex> lock(mutex);
            lastPendingPath = std::string(request.pendingPath);
            pendingPaths.push_back(lastPendingPath);
            lastFormat = request.format;
            sawIntent = request.intent != nullptr;
            intentCards.clear();
            if (request.intent != nullptr)
                for (const auto& card : request.intent->cards())
                    intentCards.push_back(card);
            metadataCards.assign(metadata.codec().cards().begin(), metadata.codec().cards().end());
            metadataHadIntent = metadata.outputIntent().has_value();
            metadataProvenanceEntries = metadata.provenance().entries().size();
        }
        if (throwEncode)
            throw std::runtime_error("encoder failure");
        if (refuseBeforeOpening)
            return an::codecFailure(an::ErrorCode::Unsupported, an::CodecFailure::UnsupportedRepresentation,
                                    request.format, "FileCodec::encode", request.pendingPath);
        const auto bytes = frame.bytes();
        const auto count = static_cast<std::size_t>(static_cast<double>(bytes.size()) * writeFraction);
        if (!skipWriting)
        {
            std::ofstream out(pathOf(request.pendingPath), std::ios::binary | std::ios::trunc);
            if (!out)
                return an::Status::failure(an::ErrorCode::ExternalFailure, "the pending path could not be opened");
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(count));
        }
        if (failAfterWrite)
            return an::Status::failure(
                an::ErrorCode::ExternalFailure,
                {.message = "no space left on device",
                 .operation = "FileCodec::encode",
                 .domain = an::ErrorDomain::Io,
                 .context = {an::StatusContextField::ofNumber(an::ContextKey::LibraryCode, 28)}});
        if (stopAfterWrite != nullptr)
            stopAfterWrite->request_stop();
        return an::CodecEncodeReport{reportOverride.value_or(bytes.size())};
    }

    an::CodecCapabilities caps{.format = an::CodecFormat::Fits,
                               .decodeSamples = 0u,
                               .encodeSamples = an::codecSampleBit({2u, an::RawSampleClass::UnsignedInteger}),
                               .regionDecode = false};
    double writeFraction = 1.0;
    std::optional<std::uint64_t> reportOverride;
    bool throwEncode = false, refuseBeforeOpening = false, skipWriting = false, failAfterWrite = false;
    std::stop_source* stopAfterWrite = nullptr;
    mutable std::mutex mutex;
    mutable std::atomic<std::uint32_t> encodes{0u};
    mutable std::string lastPendingPath;
    mutable std::vector<std::string> pendingPaths;
    mutable an::CodecFormat lastFormat = an::CodecFormat::Unknown;
    mutable bool sawIntent = false, metadataHadIntent = false;
    mutable std::vector<an::CodecCard> intentCards, metadataCards;
    mutable std::size_t metadataProvenanceEntries = 0u;
};

class CollectingSink final : public an::OutputByteSink
{
public:
    void observe(std::span<const std::byte> bytes) noexcept override
    {
        collected.insert(collected.end(), bytes.begin(), bytes.end());
        ++calls;
    }
    std::vector<std::byte> collected;
    std::uint32_t calls = 0u;
};

an::EngineContext makeEngine(const std::shared_ptr<FileCodec>& codec, std::uint32_t concurrency = 2u)
{
    const std::array<std::shared_ptr<const an::ImageCodec>, 1u> entries{codec};
    const auto registry = an::CodecRegistry::create(entries);
    EXPECT_TRUE(registry.hasValue()) << registry.status().message();
    auto engine = an::EngineContext::create({.maxConcurrency = concurrency, .codecRegistry = *registry});
    EXPECT_TRUE(engine.hasValue());
    return std::move(*engine);
}

an::CodecCard card(an::CodecNamespace origin, const char* keyword, const char* value)
{
    return *an::CodecCard::create(origin, keyword, value, "");
}

const an::CodecCard* findCard(const std::vector<an::CodecCard>& cards, std::string_view keyword)
{
    for (const auto& value : cards)
        if (value.keyword() == keyword)
            return &value;
    return nullptr;
}

TEST(OutputTransactionTest, ABlankHeaderPrologIsRefusedForAContainerWhoseHeaderIsNoMarkup)
{
    // Only XISF opens its header with markup. Asked of any other container, a blank prolog is a request the
    // container cannot honour, refused before its encoder is called, with nothing left behind.
    const ScratchTree tree("prolog");
    auto codec = std::make_shared<FileCodec>();
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    const auto frame = makeFrame(engine);
    const auto intent = *an::OutputIntent::create({.sampleType = an::SampleType::UInt16});
    const auto destination = tree.file("stack.fits");
    const auto refused = an::writeImage(
        engine, *execution,
        {.destinationPath = destination, .intent = &intent, .headerProlog = an::OutputHeaderProlog::Blank}, frame, {});
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::Unsupported);
    EXPECT_EQ(codec->encodes.load(), 0u);
    EXPECT_TRUE(tree.entries().empty());
    // Stated as the specification's, which is what a request that says nothing asks for, it is written.
    ASSERT_TRUE(
        an::writeImage(
            engine, *execution,
            {.destinationPath = destination, .intent = &intent, .headerProlog = an::OutputHeaderProlog::Specified},
            frame, {})
            .hasValue());
    EXPECT_EQ(codec->encodes.load(), 1u);
}

TEST(OutputTransactionTest, CommitsThroughAPendingSiblingAndPublishesTheCompletePayload)
{
    const ScratchTree tree("commit");
    auto codec = std::make_shared<FileCodec>();
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    const auto frame = makeFrame(engine);
    const auto intent = *an::OutputIntent::create({.sampleType = an::SampleType::UInt16});
    const auto destination = tree.file("stack.fits");
    const auto before = engine.outstandingLeases();
    auto transaction =
        an::OutputTransaction::begin(engine, *execution, {.destinationPath = destination, .intent = &intent});
    ASSERT_TRUE(transaction.hasValue()) << transaction.status().message();
    EXPECT_EQ(engine.outstandingLeases(), before + 1u);
    EXPECT_EQ(transaction->destinationPath(), destination);
    EXPECT_TRUE(an::isPendingOutputPath(transaction->pendingPath()));
    EXPECT_EQ(pathOf(transaction->pendingPath()).parent_path(), tree.path());
    EXPECT_EQ(transaction->format(), an::CodecFormat::Fits);
    EXPECT_TRUE(exists(transaction->pendingPath()));
    EXPECT_FALSE(transaction->encoded());
    ASSERT_TRUE(transaction->encode(frame, {}).ok());
    EXPECT_TRUE(transaction->encoded());
    EXPECT_EQ(codec->lastPendingPath, transaction->pendingPath());
    EXPECT_EQ(codec->lastFormat, an::CodecFormat::Fits);
    EXPECT_FALSE(exists(destination));
    auto report = transaction->commit();
    ASSERT_TRUE(report.hasValue()) << report.status().message();
    EXPECT_TRUE(transaction->committed());
    EXPECT_EQ(report->bytesWritten, frame.bytes().size());
    EXPECT_EQ(report->observedBytes, 0u);
    EXPECT_TRUE(report->flushed);
    EXPECT_FALSE(report->replacedExisting);
    ASSERT_TRUE(report->intent.has_value());
    EXPECT_EQ(report->intent->sampleType(), an::SampleType::UInt16);
    ASSERT_TRUE(report->transition.outputIntent.has_value());
    ASSERT_TRUE(report->transition.record.has_value());
    EXPECT_EQ(report->transition.record->operation(), "OutputTransaction::commit");
    EXPECT_NE(report->transition.record->detail().find("stack.fits"), std::string::npos);
    EXPECT_FALSE(an::changesMeaning(report->transition));
    EXPECT_FALSE(exists(transaction->pendingPath()));
    const auto published = readAll(destination);
    ASSERT_EQ(published.size(), frame.bytes().size());
    EXPECT_TRUE(std::equal(published.begin(), published.end(), frame.bytes().begin()));
    EXPECT_EQ(tree.entries().size(), 1u);
    transaction->abort();
    EXPECT_TRUE(exists(destination));
    *transaction = an::OutputTransaction();
    EXPECT_EQ(engine.outstandingLeases(), before);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, frame.bytes().size());
}

TEST(OutputTransactionTest, PublishesLiteralUnicodeAndRelativePathsWithoutExtraNames)
{
    const ScratchTree tree("literal-paths", std::filesystem::current_path());
    auto codec = std::make_shared<FileCodec>();
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    const auto frame = makeFrame(engine);
    for (std::size_t length = 1u; length <= 12u; ++length)
    {
        auto destination = tree.path() / std::filesystem::path(u8"estrella-星-");
        destination += std::string(length, 'a') + ".fits";
        const auto relative = utf8Of(destination.lexically_relative(std::filesystem::current_path()));
        for (const auto durability : {an::OutputDurability::CloseOnly, an::OutputDurability::FlushBeforePublish})
        {
            const auto report =
                an::writeImage(engine, *execution, {.destinationPath = relative, .durability = durability}, frame, {});
            ASSERT_TRUE(report.hasValue()) << report.status().message();
            EXPECT_EQ(readAll(utf8Of(destination)).size(), frame.bytes().size());
            EXPECT_EQ(report->flushed, durability == an::OutputDurability::FlushBeforePublish);
        }
    }
    EXPECT_EQ(tree.entries().size(), 12u);
}

TEST(OutputTransactionTest, TheReportedTransitionAppliesToTheWrittenImage)
{
    const ScratchTree tree("transition");
    auto codec = std::make_shared<FileCodec>();
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    const auto frame = makeFrame(engine);
    const auto intent = *an::OutputIntent::create({.sampleType = an::SampleType::UInt16});
    const auto identity = *an::ArtifactIdentity::create("9fc0769b6ea4be30144c8d5060", "20260910_211405_481");
    const auto source = an::ImageMetadata::create(
        {}, {}, {}, *an::Provenance::create(an::ProvenanceSourceKind::Generated, "fixture"), {}, std::nullopt);
    const auto report = an::writeImage(
        engine, *execution, {.destinationPath = tree.file("light_cal.fits"), .intent = &intent, .identity = &identity},
        frame, source);
    ASSERT_TRUE(report.hasValue()) << report.status().message();
    const auto updated = an::applyMetadataTransition(source, report->transition);
    ASSERT_TRUE(updated.hasValue()) << updated.status().message();
    ASSERT_TRUE(updated->outputIntent().has_value());
    EXPECT_EQ(*updated->outputIntent(), *report->intent);
    ASSERT_EQ(updated->provenance().entries().size(), 1u);
    const auto& detail = updated->provenance().entries().front().detail();
    EXPECT_NE(detail.find("9fc0769b6ea4be30144c8d5060"), std::string::npos);
    EXPECT_NE(detail.find("20260910_211405_481"), std::string::npos);
    EXPECT_NE(detail.find("FITS"), std::string::npos);
    EXPECT_EQ(updated->acquisition(), source.acquisition());
    EXPECT_EQ(updated->codec(), source.codec());
}

TEST(OutputTransactionTest, MalformedRequestsAreRefusedBeforeAnythingIsReserved)
{
    const ScratchTree tree("refuse");
    auto codec = std::make_shared<FileCodec>();
    auto engine = makeEngine(codec);
    auto other = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    const auto intent = *an::OutputIntent::create({.sampleType = an::SampleType::UInt16});
    const auto identity = *an::ArtifactIdentity::create("abc", "");
    EXPECT_EQ(an::OutputTransaction::begin(engine, *execution, {.destinationPath = ""}).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::OutputTransaction::begin(engine, *execution, {.destinationPath = std::string_view("a\0.fits", 7u)})
                  .status()
                  .code(),
              an::ErrorCode::InvalidArgument);
    const auto unknown = an::OutputTransaction::begin(engine, *execution, {.destinationPath = tree.file("a.bin")});
    EXPECT_EQ(an::codecFailureReason(unknown.status()), an::CodecFailure::UnknownFormat);
    const auto unavailable = an::OutputTransaction::begin(engine, *execution, {.destinationPath = tree.file("a.png")});
    EXPECT_EQ(an::codecFailureReason(unavailable.status()), an::CodecFailure::Unavailable);
    EXPECT_EQ(an::OutputTransaction::begin(engine, *execution,
                                           {.destinationPath = tree.file("a.fits"), .identity = &identity})
                  .status()
                  .code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(
        an::OutputTransaction::begin(other, *execution, {.destinationPath = tree.file("a.fits"), .intent = &intent})
            .status()
            .code(),
        an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::OutputTransaction::begin(engine, an::ExecutionContext(), {.destinationPath = tree.file("a.fits")})
                  .status()
                  .code(),
              an::ErrorCode::NotInitialized);
    // The request borrows its path, so the string it names must outlive the request.
    const std::string badDurabilityPath = tree.file("a.fits");
    an::OutputRequest badDurability{.destinationPath = badDurabilityPath};
    badDurability.durability = static_cast<an::OutputDurability>(9u);
    EXPECT_EQ(an::OutputTransaction::begin(engine, *execution, badDurability).status().code(),
              an::ErrorCode::InvalidArgument);
    an::OutputRequest badProlog{.destinationPath = badDurabilityPath};
    badProlog.headerProlog = static_cast<an::OutputHeaderProlog>(9u);
    EXPECT_EQ(an::OutputTransaction::begin(engine, *execution, badProlog).status().code(),
              an::ErrorCode::InvalidArgument);
    const std::string longPath = tree.file("x") + std::string(131072u, 'y') + ".fits";
    EXPECT_EQ(an::OutputTransaction::begin(engine, *execution, {.destinationPath = longPath}).status().code(),
              an::ErrorCode::InvalidArgument);
    // A destination at the path bound leaves no room for the pending suffix: a limit, not a reservation.
    const std::string almost = tree.file("x") + std::string(131072u - 5u - tree.file("x").size(), 'y') + ".fits";
    const auto limited = an::OutputTransaction::begin(engine, *execution, {.destinationPath = almost});
    EXPECT_EQ(limited.status().code(), an::ErrorCode::LimitExceeded);
    EXPECT_NE(limited.status().findContext(an::ContextKey::Limit), nullptr);
    EXPECT_EQ(codec->encodes.load(), 0u);
    EXPECT_TRUE(tree.entries().empty());
    const auto missing = an::OutputTransaction::begin(
        engine, *execution, {.destinationPath = utf8Of(tree.path() / "no-such-dir" / "a.fits")});
    EXPECT_EQ(missing.status().code(), an::ErrorCode::ExternalFailure);
    EXPECT_EQ(an::codecFailureReason(missing.status()), an::CodecFailure::DestinationUnavailable);
    EXPECT_NE(missing.status().findContext(an::ContextKey::LibraryCode), nullptr);
}

TEST(OutputTransactionTest, IdentityTravelsInTheIntentAndIsNeverInheritedFromTheSource)
{
    const ScratchTree tree("identity");
    auto codec = std::make_shared<FileCodec>();
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    const auto frame = makeFrame(engine);
    const auto intent = *an::OutputIntent::create(
        {.sampleType = an::SampleType::UInt16, .cards = {card(an::CodecNamespace::Fits, "OBJECT", "M42")}});
    const auto sourceCodec = *an::CodecMetadata::create({}, {card(an::CodecNamespace::Fits, "ANARTID", "'oldartifact'"),
                                                             card(an::CodecNamespace::Fits, "ANRUNID", "'oldrun'"),
                                                             card(an::CodecNamespace::Fits, "FILTER", "Ha"),
                                                             card(an::CodecNamespace::Fits, "NAXIS", "2")});
    const auto source = an::ImageMetadata::create({}, {}, {}, {}, sourceCodec,
                                                  *an::OutputIntent::create({.sampleType = an::SampleType::Float32}));
    const auto identity = *an::ArtifactIdentity::create("newartifact", "newrun");
    auto report = an::writeImage(
        engine, *execution, {.destinationPath = tree.file("stamped.fits"), .intent = &intent, .identity = &identity},
        frame, source);
    ASSERT_TRUE(report.hasValue()) << report.status().message();
    EXPECT_TRUE(codec->sawIntent);
    ASSERT_NE(findCard(codec->intentCards, "ANARTID"), nullptr);
    EXPECT_EQ(findCard(codec->intentCards, "ANARTID")->value(), "newartifact");
    EXPECT_EQ(findCard(codec->intentCards, "ANARTID")->origin(), an::CodecNamespace::Fits);
    ASSERT_NE(findCard(codec->intentCards, "ANRUNID"), nullptr);
    EXPECT_EQ(findCard(codec->intentCards, "ANRUNID")->value(), "newrun");
    ASSERT_NE(findCard(codec->intentCards, "OBJECT"), nullptr);
    EXPECT_EQ(codec->intentCards.front().keyword(), "OBJECT");
    EXPECT_EQ(findCard(codec->metadataCards, "ANARTID"), nullptr);
    EXPECT_EQ(findCard(codec->metadataCards, "ANRUNID"), nullptr);
    EXPECT_EQ(findCard(codec->metadataCards, "NAXIS"), nullptr);
    ASSERT_NE(findCard(codec->metadataCards, "FILTER"), nullptr);
    EXPECT_FALSE(codec->metadataHadIntent);
    ASSERT_TRUE(report->intent.has_value());
    const auto written = *an::CodecMetadata::create(
        {}, std::vector<an::CodecCard>(report->intent->cards().begin(), report->intent->cards().end()));
    const auto readBack = an::artifactIdentityOf(written, an::CodecNamespace::Fits);
    ASSERT_TRUE(readBack.hasValue());
    ASSERT_TRUE(readBack->has_value());
    EXPECT_EQ(**readBack, identity);
    // Without an identity the source's stamp is dropped rather than carried forward.
    report = an::writeImage(engine, *execution, {.destinationPath = tree.file("unstamped.fits"), .intent = &intent},
                            frame, source);
    ASSERT_TRUE(report.hasValue()) << report.status().message();
    EXPECT_EQ(findCard(codec->intentCards, "ANARTID"), nullptr);
    EXPECT_EQ(findCard(codec->intentCards, "ANRUNID"), nullptr);
    EXPECT_EQ(findCard(codec->metadataCards, "ANARTID"), nullptr);
    EXPECT_EQ(findCard(codec->metadataCards, "ANRUNID"), nullptr);
    EXPECT_EQ(report->intent->cards().size(), 1u);
    // A raw pass-through states neither an intent nor a stamp.
    report = an::writeImage(engine, *execution, {.destinationPath = tree.file("raw.fits")}, frame, source);
    ASSERT_TRUE(report.hasValue()) << report.status().message();
    EXPECT_FALSE(codec->sawIntent);
    EXPECT_FALSE(report->intent.has_value());
    EXPECT_FALSE(report->transition.outputIntent.has_value());
    EXPECT_TRUE(report->transition.record.has_value());
    EXPECT_EQ(findCard(codec->metadataCards, "ANARTID"), nullptr);
}

TEST(OutputTransactionTest, ShortWritesArePublishedNowhere)
{
    const ScratchTree tree("short");
    auto codec = std::make_shared<FileCodec>();
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    const auto frame = makeFrame(engine);
    const auto destination = tree.file("short.fits");
    writeAll(destination, "previous");
    codec->writeFraction = 0.5;
    auto report = an::writeImage(engine, *execution, {.destinationPath = destination}, frame, {});
    EXPECT_EQ(report.status().code(), an::ErrorCode::InvalidData);
    EXPECT_EQ(an::codecFailureReason(report.status()), an::CodecFailure::ShortWrite);
    ASSERT_NE(report.status().findContext(an::ContextKey::Requested), nullptr);
    EXPECT_EQ(report.status().findContext(an::ContextKey::Requested)->count, frame.bytes().size());
    ASSERT_NE(report.status().findContext(an::ContextKey::Available), nullptr);
    EXPECT_EQ(report.status().findContext(an::ContextKey::Available)->count, frame.bytes().size() / 2u);
    EXPECT_EQ(tree.entries().size(), 1u);
    EXPECT_EQ(readAll(destination).size(), 8u);
    codec->writeFraction = 1.0;
    codec->skipWriting = true;
    report = an::writeImage(engine, *execution, {.destinationPath = destination}, frame, {});
    EXPECT_EQ(an::codecFailureReason(report.status()), an::CodecFailure::ShortWrite);
    EXPECT_EQ(report.status().findContext(an::ContextKey::Available)->count, 0u);
    codec->skipWriting = false;
    codec->reportOverride = frame.bytes().size() + 1u;
    report = an::writeImage(engine, *execution, {.destinationPath = destination}, frame, {});
    EXPECT_EQ(an::codecFailureReason(report.status()), an::CodecFailure::ShortWrite);
    codec->reportOverride = 0u;
    report = an::writeImage(engine, *execution, {.destinationPath = destination}, frame, {});
    EXPECT_EQ(an::codecFailureReason(report.status()), an::CodecFailure::ShortWrite);
    EXPECT_EQ(tree.entries().size(), 1u);
    EXPECT_EQ(readAll(destination).size(), 8u);
}

TEST(OutputTransactionTest, EncoderFailuresLeaveTheDestinationUntouchedAndKeepTheirCause)
{
    const ScratchTree tree("diskfull");
    auto codec = std::make_shared<FileCodec>();
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    const auto frame = makeFrame(engine);
    const auto destination = tree.file("full.fits");
    writeAll(destination, "previous");
    codec->writeFraction = 0.25;
    codec->failAfterWrite = true;
    auto transaction = an::OutputTransaction::begin(engine, *execution, {.destinationPath = destination});
    ASSERT_TRUE(transaction.hasValue());
    const auto pending = std::string(transaction->pendingPath());
    const auto failed = transaction->encode(frame, {});
    EXPECT_EQ(failed.code(), an::ErrorCode::ExternalFailure);
    ASSERT_NE(failed.rootCause().findContext(an::ContextKey::LibraryCode), nullptr);
    EXPECT_EQ(failed.rootCause().findContext(an::ContextKey::LibraryCode)->number, 28);
    EXPECT_FALSE(transaction->encoded());
    EXPECT_FALSE(exists(pending));
    EXPECT_EQ(transaction->commit().status().code(), an::ErrorCode::InvalidState);
    EXPECT_EQ(readAll(destination).size(), 8u);
    codec->failAfterWrite = false;
    codec->throwEncode = true;
    auto thrown = an::writeImage(engine, *execution, {.destinationPath = destination}, frame, {});
    EXPECT_EQ(thrown.status().code(), an::ErrorCode::ExternalFailure);
    codec->throwEncode = false;
    codec->refuseBeforeOpening = true;
    thrown = an::writeImage(engine, *execution, {.destinationPath = destination}, frame, {});
    EXPECT_EQ(an::codecFailureReason(thrown.status().rootCause()), an::CodecFailure::UnsupportedRepresentation);
    EXPECT_EQ(tree.entries().size(), 1u);
    EXPECT_EQ(readAll(destination).size(), 8u);
}

TEST(OutputTransactionTest, PublishFailuresRemoveThePendingPayloadAndLeaveTheDestination)
{
    const ScratchTree tree("publish");
    auto codec = std::make_shared<FileCodec>();
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    const auto frame = makeFrame(engine);
    const auto blocked = tree.file("directory.fits");
    std::error_code ignored;
    std::filesystem::create_directory(pathOf(blocked), ignored);
    const auto report = an::writeImage(engine, *execution, {.destinationPath = blocked}, frame, {});
    EXPECT_EQ(report.status().code(), an::ErrorCode::ExternalFailure);
    EXPECT_EQ(an::codecFailureReason(report.status()), an::CodecFailure::PublishFailure);
    EXPECT_NE(report.status().findContext(an::ContextKey::LibraryCode), nullptr);
    EXPECT_TRUE(std::filesystem::is_directory(pathOf(blocked), ignored));
    EXPECT_EQ(tree.entries().size(), 1u);
#ifdef _WIN32
    const auto locked = tree.file("locked.fits");
    writeAll(locked, "held open");
    {
        std::ifstream holder(pathOf(locked), std::ios::binary);
        ASSERT_TRUE(holder.is_open());
        const auto held = an::writeImage(engine, *execution, {.destinationPath = locked}, frame, {});
        EXPECT_EQ(an::codecFailureReason(held.status()), an::CodecFailure::PublishFailure);
    }
    EXPECT_EQ(readAll(locked).size(), 9u);
    EXPECT_EQ(tree.entries().size(), 2u);
#endif
}

TEST(OutputTransactionTest, CancellationBeforePublicationAbortsWithoutWriting)
{
    const ScratchTree tree("cancel");
    auto codec = std::make_shared<FileCodec>();
    auto engine = makeEngine(codec);
    const auto frame = makeFrame(engine);
    std::stop_source stop;
    codec->stopAfterWrite = &stop;
    an::ExecutionOptions options;
    options.stopToken = stop.get_token();
    auto execution = an::ExecutionContext::create(engine, options);
    const auto destination = tree.file("cancelled.fits");
    auto transaction = an::OutputTransaction::begin(engine, *execution, {.destinationPath = destination});
    ASSERT_TRUE(transaction.hasValue());
    const auto pending = std::string(transaction->pendingPath());
    const auto encoded = transaction->encode(frame, {});
    EXPECT_EQ(encoded.code(), an::ErrorCode::Cancelled);
    EXPECT_FALSE(exists(pending));
    EXPECT_FALSE(exists(destination));
    EXPECT_EQ(transaction->commit().status().code(), an::ErrorCode::InvalidState);
    EXPECT_EQ(an::OutputTransaction::begin(engine, *execution, {.destinationPath = destination}).status().code(),
              an::ErrorCode::Cancelled);
    EXPECT_TRUE(tree.entries().empty());
    EXPECT_EQ(codec->encodes.load(), 1u);
}

TEST(OutputTransactionTest, AnAbandonedTransactionRemovesItsPendingPayload)
{
    const ScratchTree tree("abandon");
    auto codec = std::make_shared<FileCodec>();
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    const auto frame = makeFrame(engine);
    const auto destination = tree.file("abandoned.fits");
    std::string pending;
    {
        auto transaction = an::OutputTransaction::begin(engine, *execution, {.destinationPath = destination});
        ASSERT_TRUE(transaction.hasValue());
        pending = std::string(transaction->pendingPath());
        ASSERT_TRUE(transaction->encode(frame, {}).ok());
        EXPECT_TRUE(exists(pending));
        EXPECT_EQ(transaction->encode(frame, {}).code(), an::ErrorCode::InvalidState);
    }
    EXPECT_FALSE(exists(pending));
    EXPECT_FALSE(exists(destination));
    auto transaction = an::OutputTransaction::begin(engine, *execution, {.destinationPath = destination});
    ASSERT_TRUE(transaction.hasValue());
    EXPECT_EQ(transaction->commit().status().code(), an::ErrorCode::InvalidState);
    transaction->abort();
    transaction->abort();
    EXPECT_FALSE(transaction->valid());
    EXPECT_EQ(transaction->encode(frame, {}).code(), an::ErrorCode::InvalidState);
    EXPECT_EQ(transaction->commit().status().code(), an::ErrorCode::InvalidState);
    an::OutputTransaction moved(std::move(*transaction));
    EXPECT_FALSE(moved.valid());
    EXPECT_TRUE(tree.entries().empty());
    auto committed = an::OutputTransaction::begin(engine, *execution, {.destinationPath = destination});
    ASSERT_TRUE(committed.hasValue());
    ASSERT_TRUE(committed->encode(frame, {}).ok());
    ASSERT_TRUE(committed->commit().hasValue());
    EXPECT_EQ(committed->commit().status().code(), an::ErrorCode::InvalidState);
    EXPECT_EQ(committed->encode(frame, {}).code(), an::ErrorCode::InvalidState);
    EXPECT_TRUE(exists(destination));
}

TEST(OutputTransactionTest, PendingReservationsConflictAndAbandonedOnesAreSwept)
{
    const ScratchTree tree("sweep");
    auto codec = std::make_shared<FileCodec>();
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    const auto destination = tree.file("shared.fits");
    auto first = an::OutputTransaction::begin(engine, *execution, {.destinationPath = destination});
    ASSERT_TRUE(first.hasValue());
    const auto second = an::OutputTransaction::begin(engine, *execution, {.destinationPath = destination});
    EXPECT_EQ(second.status().code(), an::ErrorCode::Busy);
    EXPECT_EQ(an::codecFailureReason(second.status()), an::CodecFailure::PendingConflict);
    first->abort();
    EXPECT_TRUE(an::OutputTransaction::begin(engine, *execution, {.destinationPath = destination}).hasValue());
    EXPECT_TRUE(tree.entries().empty());
    writeAll(tree.file("stale.fits.1-2-3.antmp"), "abandoned");
    writeAll(tree.file("keep.fits"), "kept");
    writeAll(tree.file("keep.antmp.txt"), "kept");
    std::error_code ignored;
    std::filesystem::create_directory(pathOf(tree.file("nested.antmp")), ignored);
    writeAll(tree.file("nested.antmp/inner.fits.4-5-6.antmp"), "not swept: nested");
    const auto swept = an::sweepPendingOutputs(utf8Of(tree.path()));
    ASSERT_TRUE(swept.hasValue()) << swept.status().message();
    EXPECT_EQ(*swept, 1u);
    EXPECT_FALSE(exists(tree.file("stale.fits.1-2-3.antmp")));
    EXPECT_TRUE(exists(tree.file("keep.fits")));
    EXPECT_TRUE(exists(tree.file("keep.antmp.txt")));
    EXPECT_TRUE(exists(tree.file("nested.antmp/inner.fits.4-5-6.antmp")));
    const auto missing = an::sweepPendingOutputs(utf8Of(tree.path() / "absent"));
    ASSERT_TRUE(missing.hasValue());
    EXPECT_EQ(*missing, 0u);
    EXPECT_EQ(an::sweepPendingOutputs("").status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::sweepPendingOutputs(tree.file("keep.fits")).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_TRUE(an::isPendingOutputPath("a/b.fits.12-3-4.antmp"));
    EXPECT_TRUE(an::isPendingOutputPath("x.antmp"));
    EXPECT_FALSE(an::isPendingOutputPath(".antmp"));
    EXPECT_FALSE(an::isPendingOutputPath("x.ANTMP"));
    EXPECT_FALSE(an::isPendingOutputPath("x.antmp/"));
    EXPECT_FALSE(an::isPendingOutputPath("x.fits"));
    EXPECT_FALSE(an::isPendingOutputPath(""));
    EXPECT_EQ(an::kPendingOutputSuffix, ".antmp");
}

TEST(OutputTransactionTest, ConcurrentTransactionsInOneDirectoryUseDistinctPendingNames)
{
    const ScratchTree tree("concurrent");
    auto codec = std::make_shared<FileCodec>();
    auto engine = makeEngine(codec, 2u);
    auto first = an::ExecutionContext::create(engine, {.maxConcurrency = 1u});
    auto second = an::ExecutionContext::create(engine, {.maxConcurrency = 1u});
    ASSERT_TRUE(first.hasValue() && second.hasValue());
    const auto frame = makeFrame(engine);
    std::atomic<unsigned> successes{0u};
    const auto work = [&](const an::ExecutionContext& execution, const char* name) {
        for (unsigned i = 0u; i < 8u; ++i)
            if (an::writeImage(engine, execution, {.destinationPath = tree.file(name)}, frame, {}).hasValue())
                ++successes;
    };
    std::thread a([&] { work(*first, "a.fits"); });
    std::thread b([&] { work(*second, "b.fits"); });
    a.join();
    b.join();
    EXPECT_EQ(successes.load(), 16u);
    EXPECT_EQ(codec->encodes.load(), 16u);
    std::vector<std::string> names;
    for (const auto& path : codec->pendingPaths)
        names.push_back(path);
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    EXPECT_EQ(names.size(), 2u);
    for (const auto& name : names)
        EXPECT_TRUE(an::isPendingOutputPath(name));
    EXPECT_EQ(tree.entries().size(), 2u);
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, frame.bytes().size());
}

TEST(OutputTransactionTest, AByteSinkObservesTheCompletePayloadBeforePublication)
{
    const ScratchTree tree("observe");
    auto codec = std::make_shared<FileCodec>();
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    const auto frame = makeFrame(engine, 512u, 300u);
    CollectingSink sink;
    const auto destination = tree.file("observed.fits");
    const auto report =
        an::writeImage(engine, *execution, {.destinationPath = destination, .byteSink = &sink}, frame, {});
    ASSERT_TRUE(report.hasValue()) << report.status().message();
    EXPECT_EQ(report->bytesWritten, 307200u);
    EXPECT_EQ(report->observedBytes, 307200u);
    EXPECT_GE(sink.calls, 2u);
    ASSERT_EQ(sink.collected.size(), frame.bytes().size());
    EXPECT_TRUE(std::equal(sink.collected.begin(), sink.collected.end(), frame.bytes().begin()));
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, frame.bytes().size());
    EXPECT_GT(engine.memoryResource()->counters().peakBytes, frame.bytes().size());
    an::EngineContextOptions starved{.maxConcurrency = 1u};
    const std::array<std::shared_ptr<const an::ImageCodec>, 1u> entries{codec};
    starved.codecRegistry = *an::CodecRegistry::create(entries);
    starved.limits.memoryBudgetBytes = 64u;
    auto small = an::EngineContext::create(starved);
    auto smallExecution = an::ExecutionContext::create(*small, {});
    const auto borrowed =
        an::RawFrame::create(*frame.descriptor(), *frame.layout(), *an::SampleBuffer::borrowConst(frame.bytes()));
    ASSERT_TRUE(borrowed.hasValue());
    CollectingSink second;
    const auto refused = an::writeImage(
        *small, *smallExecution, {.destinationPath = tree.file("refused.fits"), .byteSink = &second}, *borrowed, {});
    EXPECT_EQ(refused.status().code(), an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_TRUE(second.collected.empty());
    EXPECT_FALSE(exists(tree.file("refused.fits")));
    EXPECT_EQ(tree.entries().size(), 1u);
}

TEST(OutputTransactionTest, ReplacePolicyDecidesWhetherAnExistingDestinationIsReplaced)
{
    const ScratchTree tree("replace");
    auto codec = std::make_shared<FileCodec>();
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    const auto frame = makeFrame(engine);
    const auto destination = tree.file("existing.fits");
    writeAll(destination, "previous");
    const auto refused = an::writeImage(
        engine, *execution, {.destinationPath = destination, .replacePolicy = an::OutputReplacePolicy::RefuseExisting},
        frame, {});
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidState);
    EXPECT_EQ(an::codecFailureReason(refused.status()), an::CodecFailure::ExistingDestination);
    EXPECT_EQ(codec->encodes.load(), 0u);
    EXPECT_EQ(readAll(destination).size(), 8u);
    EXPECT_EQ(tree.entries().size(), 1u);
    const auto replaced = an::writeImage(engine, *execution, {.destinationPath = destination}, frame, {});
    ASSERT_TRUE(replaced.hasValue()) << replaced.status().message();
    EXPECT_TRUE(replaced->replacedExisting);
    EXPECT_EQ(readAll(destination).size(), frame.bytes().size());
    const auto fresh = an::writeImage(
        engine, *execution,
        {.destinationPath = tree.file("fresh.fits"), .replacePolicy = an::OutputReplacePolicy::RefuseExisting}, frame,
        {});
    ASSERT_TRUE(fresh.hasValue());
    EXPECT_FALSE(fresh->replacedExisting);
}

TEST(OutputTransactionTest, DurabilityAndCardLimitsAreStatedNotAssumed)
{
    const ScratchTree tree("durability");
    auto codec = std::make_shared<FileCodec>();
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    const auto frame = makeFrame(engine);
    const auto report = an::writeImage(
        engine, *execution,
        {.destinationPath = tree.file("closed.fits"), .durability = an::OutputDurability::CloseOnly}, frame, {});
    ASSERT_TRUE(report.hasValue()) << report.status().message();
    EXPECT_FALSE(report->flushed);
    EXPECT_EQ(report->bytesWritten, frame.bytes().size());
    std::vector<an::CodecCard> cards;
    for (std::size_t i = 0u; i < an::kMaxCodecCards; ++i)
        cards.push_back(card(an::CodecNamespace::Fits, "CARD", "1"));
    const auto full = an::OutputIntent::create({.sampleType = an::SampleType::UInt16, .cards = cards});
    ASSERT_TRUE(full.hasValue());
    const auto identity = *an::ArtifactIdentity::create("abc", "run");
    const auto limited = an::OutputTransaction::begin(
        engine, *execution, {.destinationPath = tree.file("full.fits"), .intent = &*full, .identity = &identity});
    EXPECT_EQ(limited.status().code(), an::ErrorCode::LimitExceeded);
    EXPECT_EQ(codec->encodes.load(), 1u);
    EXPECT_EQ(tree.entries().size(), 1u);
}

TEST(OutputTransactionTest, AnOpenTransactionKeepsItsEngineContextFromShuttingDown)
{
    const ScratchTree tree("lease");
    auto codec = std::make_shared<FileCodec>();
    auto engine = makeEngine(codec);
    auto execution = an::ExecutionContext::create(engine, {});
    EXPECT_EQ(engine.outstandingLeases(), 1u);
    auto transaction = an::OutputTransaction::begin(engine, *execution, {.destinationPath = tree.file("lease.fits")});
    ASSERT_TRUE(transaction.hasValue());
    EXPECT_EQ(engine.outstandingLeases(), 2u);
    EXPECT_EQ(engine.shutdown().code(), an::ErrorCode::Busy);
    transaction->abort();
    EXPECT_EQ(engine.outstandingLeases(), 1u);
    EXPECT_EQ(engine.shutdown().code(), an::ErrorCode::Busy);
    *execution = an::ExecutionContext();
    EXPECT_EQ(engine.outstandingLeases(), 0u);
    EXPECT_TRUE(engine.shutdown().ok());
    EXPECT_TRUE(tree.entries().empty());
}

TEST(OutputTransactionTest, AByteSinkCanCancelBetweenChunksWithoutLeakingItsBuffer)
{
    const ScratchTree tree("sink-cancel");
    auto engine = makeEngine(std::make_shared<FileCodec>());
    const auto frame = makeFrame(engine, 1024u, 256u);
    std::stop_source stop;
    class CancellingSink final : public an::OutputByteSink
    {
    public:
        explicit CancellingSink(std::stop_source& source) : stop(source) {}
        void observe(std::span<const std::byte>) noexcept override
        {
            ++calls;
            stop.request_stop();
        }
        std::stop_source& stop;
        unsigned calls = 0u;
    } sink(stop);
    auto execution = an::ExecutionContext::create(engine, {.stopToken = stop.get_token()});
    const auto before = engine.memoryResource()->counters().liveBytes;
    const auto result =
        an::writeImage(engine, *execution, {.destinationPath = tree.file("cancel.fits"), .byteSink = &sink}, frame, {});
    EXPECT_EQ(result.status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(sink.calls, 1u);
    EXPECT_TRUE(tree.entries().empty());
    EXPECT_EQ(engine.memoryResource()->counters().liveBytes, before);
}

#if defined(_MSC_VER) && defined(_DEBUG)
TEST(OutputTransactionTest, AllocationFailuresCannotPublishOrFailAfterPublication)
{
    const ScratchTree tree("allocation");
    auto engine = makeEngine(std::make_shared<FileCodec>());
    auto execution = an::ExecutionContext::create(engine, {});
    const auto frame = makeFrame(engine);
    const auto intent = *an::OutputIntent::create(
        {.sampleType = an::SampleType::UInt16,
         .cards = {card(an::CodecNamespace::Fits, "OBSERVER", "an observer name longer than inline storage")}});
    for (const bool existing : {false, true})
        for (const bool afterPublication : {false, true})
        {
            bool reachedSuccess = false;
            for (std::size_t ordinal = 1u; ordinal <= 64u; ++ordinal)
            {
                const auto destination = tree.file("allocation.fits");
                std::filesystem::remove(pathOf(destination));
                if (existing)
                    writeAll(destination, "previous");
                const auto previous = readAll(destination);
                auto transaction = an::OutputTransaction::begin(engine, *execution,
                                                                {.destinationPath = destination, .intent = &intent});
                ASSERT_TRUE(transaction.hasValue());
                ASSERT_TRUE(transaction->encode(frame, {}).ok());
                const auto pending = pathOf(transaction->pendingPath());
                bool refused = false;
                auto result = [&] {
                    codec_test::AllocationFault fault(ordinal, afterPublication ? pending.c_str() : nullptr);
                    auto value = transaction->commit();
                    refused = fault.refused;
                    return value;
                }();
                SCOPED_TRACE(testing::Message() << existing << ":" << afterPublication << ":" << ordinal);
                EXPECT_EQ(transaction->committed(), result.hasValue());
                EXPECT_FALSE(std::filesystem::exists(pending));
                if (!result.hasValue())
                {
                    EXPECT_EQ(result.status().code(), an::ErrorCode::AllocationFailed);
                    EXPECT_TRUE(refused);
                    EXPECT_FALSE(transaction->valid());
                    EXPECT_EQ(exists(destination), existing);
                    if (existing)
                        EXPECT_EQ(readAll(destination), previous);
                }
                else
                {
                    EXPECT_FALSE(refused);
                    EXPECT_EQ(result->replacedExisting, existing);
                    ASSERT_TRUE(result->intent.has_value());
                    EXPECT_EQ(*result->intent, *result->transition.outputIntent);
                    EXPECT_EQ(readAll(destination).size(), frame.bytes().size());
                    reachedSuccess = true;
                    break;
                }
            }
            EXPECT_TRUE(reachedSuccess);
        }
}
#endif
} // namespace

TEST(OutputTransactionTest, SweepReportsAPayloadItCannotRemove)
{
#if defined(_WIN32)
    const ScratchTree tree("refused-sweep");
    const auto path = tree.file("locked.antmp");
    writeAll(path, "pending");
    std::ifstream holder(pathOf(path), std::ios::binary);
    ASSERT_TRUE(holder.is_open());
    const auto refused = an::sweepPendingOutputs(utf8Of(tree.path()));
    holder.close();
    EXPECT_EQ(refused.code(), an::ErrorCode::ExternalFailure);
    EXPECT_TRUE(exists(path));
#else
    GTEST_SKIP() << "this fixture uses Windows open-file deletion semantics";
#endif
}

TEST(OutputTransactionTest, AnAdapterThatWritesNoReservedImageRefusesTheReservationAndLeavesNothing)
{
    // A reserved image is a private extension of the adapter interface: an adapter registered from outside the
    // engine writes a frame whole, and a reservation through it is refused before anything is written.
    const ScratchTree tree("reserve");
    auto codec = std::make_shared<FileCodec>();
    auto engine = makeEngine(codec);
    EXPECT_FALSE(an::supportsReservedImageOutput(engine, an::CodecFormat::Fits));
    EXPECT_FALSE(an::supportsReservedImageOutput(an::EngineContext(), an::CodecFormat::Fits));
    auto execution = an::ExecutionContext::create(engine, {});
    const auto frame = makeFrame(engine);
    const auto intent = *an::OutputIntent::create({.sampleType = an::SampleType::UInt16});
    const auto destination = tree.file("stack.fits");
    const auto before = engine.outstandingLeases();
    auto transaction =
        an::OutputTransaction::begin(engine, *execution, {.destinationPath = destination, .intent = &intent});
    ASSERT_TRUE(transaction.hasValue()) << transaction.status().message();
    const an::Status refused = transaction->reserve(*frame.descriptor(), an::StorageArrangement::Planar, {});
    EXPECT_EQ(refused.code(), an::ErrorCode::Unsupported);
    EXPECT_EQ(an::codecFailureReason(refused), an::CodecFailure::UnsupportedOperation);
    EXPECT_EQ(codec->lastPendingPath, "");
    EXPECT_FALSE(transaction->valid());
    EXPECT_FALSE(transaction->reserved());
    EXPECT_TRUE(tree.entries().empty());
    EXPECT_EQ(engine.outstandingLeases(), before);
    EXPECT_EQ(transaction->commit().status().code(), an::ErrorCode::InvalidState);
    EXPECT_EQ(transaction->writeRows(0u, *frame.layout(), frame.bytes()).code(), an::ErrorCode::InvalidState);
}
