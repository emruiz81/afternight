// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/IO/ImageListManifest.h>

#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/DumpCodec.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/IO/ImageListSource.h>
#include <AfterNightEngine/IO/SequenceError.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <random>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace std::literals;

std::string parameterOf(const an::Status& status)
{
    const an::StatusContextField* const field = status.findContext(an::ContextKey::Parameter);
    return field != nullptr ? std::string(field->text) : std::string();
}

std::optional<std::uint64_t> requestedOf(const an::Status& status)
{
    const an::StatusContextField* const field = status.findContext(an::ContextKey::Requested);
    return field != nullptr ? std::optional<std::uint64_t>(field->count) : std::nullopt;
}

/// The declaration and start tag a list's facts are written with.
std::string headOf(std::string_view facts)
{
    return "<?xml version=\"1.0\"?>\n<sequence version=\"1\" " + std::string(facts);
}

class ImageListManifestTest : public testing::Test
{
protected:
    void SetUp() override
    {
        directory = std::filesystem::temp_directory_path() /
                    ("an-anseq-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        const std::array codecs{*an::createFitsCodec(), *an::createDumpCodec()};
        an::EngineContextOptions options;
        options.maxConcurrency = 4u;
        options.codecRegistry = *an::CodecRegistry::create(codecs);
        engine = std::move(*an::EngineContext::create(options));
        execution = std::move(*an::ExecutionContext::create(engine, {.maxConcurrency = 1u}));
    }

    void TearDown() override
    {
        execution = {};
        EXPECT_EQ(engine.memoryResource()->counters().liveBytes, 0u);
        EXPECT_TRUE(engine.shutdown().ok());
        std::error_code error;
        const auto root = std::filesystem::weakly_canonical(directory, error);
        ASSERT_FALSE(error);
        ASSERT_EQ(root.parent_path(), std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()));
        std::filesystem::remove_all(root, error);
        EXPECT_FALSE(error) << error.message();
        EXPECT_FALSE(std::filesystem::exists(root));
    }

    static std::string utf8(const std::filesystem::path& path)
    {
        const auto text = path.u8string();
        return {reinterpret_cast<const char*>(text.data()), text.size()};
    }

    static std::filesystem::path native(std::string_view path)
    {
        return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size()));
    }

    std::string pathOf(std::string_view name) const
    {
        return utf8(directory / native(name));
    }

    static std::string readAll(std::string_view path)
    {
        std::ifstream stream(native(path), std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    static void writeText(std::string_view path, std::string_view text)
    {
        std::ofstream stream(native(path), std::ios::binary | std::ios::trunc);
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    an::RawFrame frame(unsigned value, std::uint32_t width = 4u, std::uint32_t height = 2u, bool mosaiced = false)
    {
        const auto extent = *an::ImageExtent::create(width, height, 1u, engine.limits());
        const an::CfaDescriptor cfa =
            mosaiced ? *an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u) : an::CfaDescriptor{};
        const auto descriptor = *an::RawFrameDescriptor::create({extent,
                                                                 {1u, an::RawSampleClass::UnsignedInteger},
                                                                 an::ByteOrder::Native,
                                                                 an::ChannelSchema::Mono,
                                                                 an::ColorSpace::Unspecified,
                                                                 an::NumericDomain::RawSensorCode,
                                                                 cfa,
                                                                 an::CoordinateOrigin::TopLeft,
                                                                 1.0});
        const auto layout = *an::ImageLayout::create({extent, 1u, 1u, 1u, 1u}, engine.limits());
        auto buffer = an::SampleBuffer::createOwned(engine, layout, an::StorageInitialization::Uninitialized);
        auto result = an::RawFrame::create(descriptor, layout, std::move(*buffer));
        std::ranges::fill(*result->mutableBytes(), static_cast<std::byte>(value));
        return std::move(*result);
    }

    an::SequenceFrame sequenceFrame(unsigned value, std::uint64_t index, std::uint32_t width = 4u,
                                    std::uint32_t height = 2u, bool mosaiced = false)
    {
        return {frame(value, width, height, mosaiced), {}, {index, {}, {}}};
    }

    std::filesystem::path directory;
    an::EngineContext engine;
    an::ExecutionContext execution;
};

TEST_F(ImageListManifestTest, AManifestIsWrittenByteForByteInItsDocumentedForm)
{
    const an::ImageListManifest lights{.timestampSeconds = 1714595462u,
                                       .width = 640u,
                                       .height = 480u,
                                       .channels = 1u,
                                       .mosaiced = true,
                                       .framesPerSecond = 29.97,
                                       .framePaths = {"light-frames/light-0.fits", "/abs/path/light 1.fits"}};
    const std::string destination = pathOf("lights.anseq");
    auto written = an::writeImageListManifest(
        engine, execution, {.destinationPath = destination, .durability = an::OutputDurability::CloseOnly}, lights);
    ASSERT_TRUE(written.hasValue()) << written.status().message();
    const std::string expected =
        headOf("timestamp=\"1714595462\" width=\"640\" height=\"480\" channels=\"1\" isCFA=\"true\" fps=\"29.97\">\n") +
        "\t<frame path=\"light-frames/light-0.fits\" />\n\t<frame path=\"/abs/path/light 1.fits\" />\n</sequence>\n";
    EXPECT_EQ(readAll(destination), expected);
    EXPECT_EQ(written->bytesWritten, expected.size());

    // Attribute text escapes what the library escaped and nothing else.
    const an::ImageListManifest escaped{.width = 640u,
                                        .height = 480u,
                                        .channels = 1u,
                                        .framesPerSecond = 1000.25,
                                        .framePaths = {"a&b<c>\"d'e\tf\ng\rh.fits", "caf\xc3\xa9.fits"}};
    const std::string escapedPath = pathOf("escaped.anseq");
    ASSERT_TRUE(an::writeImageListManifest(engine, execution, {.destinationPath = escapedPath}, escaped).hasValue());
    EXPECT_EQ(readAll(escapedPath),
              headOf("timestamp=\"0\" width=\"640\" height=\"480\" channels=\"1\" isCFA=\"false\" fps=\"1000\">\n") +
                  "\t<frame path=\"a&amp;b&lt;c>&quot;d'e&#09;f&#10;g&#13;h.fits\" />\n"
                  "\t<frame path=\"caf\xc3\xa9.fits\" />\n</sequence>\n");

    // A list with no frame is one self-closed element, and a rate has four significant digits.
    const std::array rates{
        std::pair{25.0, "25"sv},          std::pair{0.5, "0.5"sv},     std::pair{123456.0, "1.235e+05"sv},
        std::pair{1.0 / 3.0, "0.3333"sv}, std::pair{1e-05, "1e-05"sv}, std::pair{0.0, "0"sv}};
    for (std::size_t index = 0u; index < rates.size(); ++index)
    {
        const std::string path = pathOf("rate-" + std::to_string(index) + ".anseq");
        const an::ImageListManifest empty{.framesPerSecond = rates[index].first};
        ASSERT_TRUE(an::writeImageListManifest(engine, execution, {.destinationPath = path}, empty).hasValue());
        EXPECT_EQ(readAll(path),
                  headOf("timestamp=\"0\" width=\"0\" height=\"0\" channels=\"0\" isCFA=\"false\" fps=\"" +
                         std::string(rates[index].second) + "\" />\n"));
    }
}

TEST_F(ImageListManifestTest, AWrittenManifestReadsBackAsTheListItStates)
{
    const an::ImageListManifest lights{.timestampSeconds = 1714595462u,
                                       .width = 4096u,
                                       .height = 2160u,
                                       .channels = 3u,
                                       .mosaiced = false,
                                       .framesPerSecond = 29.97,
                                       .framePaths = {"lights-frames/a.fits", "\xe6\x98\x9f/b c.fits", "&<>\"'.fits"}};
    const std::string destination = pathOf("\xe6\x98\x9f.anseq");
    ASSERT_TRUE(an::writeImageListManifest(engine, execution, {.destinationPath = destination}, lights).hasValue());
    auto read = an::readImageListManifest(engine, execution, destination);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    EXPECT_EQ(*read, lights);

    an::ImageListManifest third = lights;
    third.framesPerSecond = 1.0 / 3.0;
    const std::string thirdPath = pathOf("third.anseq");
    ASSERT_TRUE(an::writeImageListManifest(engine, execution, {.destinationPath = thirdPath}, third).hasValue());
    auto rounded = an::readImageListManifest(engine, execution, thirdPath);
    ASSERT_TRUE(rounded.hasValue());
    EXPECT_EQ(rounded->framesPerSecond, 0.3333);
}

TEST_F(ImageListManifestTest, AnAttributeIsReadLenientlyAndMarkupAroundTheFramesIsSkipped)
{
    const std::string path = pathOf("handwritten.anseq");
    writeText(path, "\xef\xbb\xbf<?xml version=\"1.0\"?>\n<!-- a comment -->\n"
                    "<sequence timestamp=\" 42x\" fps=\"2.5e1\" width=\"0x10\" height=\"-5\" "
                    "channels=\"18446744073709551616\" isCFA=\"yes\" version = '1'>"
                    "<other><frame path=\"nested\"/></other>"
                    "<frame path=\"x&amp;y&#9;z&#x41;&apos;&quot;\tq\r\nr\"/>"
                    "<![CDATA[<frame path=\"not a frame\"/>]]><?instruction data?>"
                    "<frame path='single &unknown; &#x; &amp'/>text</sequence>\n<!-- after -->");
    auto read = an::readImageListManifest(engine, execution, path);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    EXPECT_EQ(read->timestampSeconds, 42u);
    EXPECT_EQ(read->framesPerSecond, 25.0);
    EXPECT_EQ(read->width, 16u);
    EXPECT_EQ(read->height, 0u);
    EXPECT_EQ(read->channels, (std::numeric_limits<std::uint64_t>::max)());
    EXPECT_TRUE(read->mosaiced);
    ASSERT_EQ(read->framePaths.size(), 2u);
    EXPECT_EQ(read->framePaths[0], "x&y\tzA'\" q r");
    EXPECT_EQ(read->framePaths[1], "single &unknown; &#x; &amp");

    // Absent attributes read as zero and false, and the first of a repeated attribute is the one read.
    writeText(path, "<sequence width=\"7\" width=\"9\"><frame path=\"a\" path=\"b\"/></sequence>");
    auto sparse = an::readImageListManifest(engine, execution, path);
    ASSERT_TRUE(sparse.hasValue()) << sparse.status().message();
    EXPECT_EQ(*sparse, (an::ImageListManifest{.width = 7u, .framePaths = {"a"}}));
}

TEST_F(ImageListManifestTest, AMalformedManifestIsRefusedByWhatIsWrongWithIt)
{
    struct Case
    {
        std::string_view text;
        an::ErrorCode code;
        an::SequenceFailure reason;
        std::string_view parameter;
        std::optional<std::uint64_t> requested;
    };
    const std::array cases{
        Case{"<sequence><frame path=\"a\"></sequence>",
             an::ErrorCode::InvalidData,
             an::SequenceFailure::MalformedContainer,
             "document",
             {}},
        Case{"", an::ErrorCode::InvalidData, an::SequenceFailure::MalformedContainer, "document", {}},
        Case{"<sequence x=\"1\"y=\"2\"/>",
             an::ErrorCode::InvalidData,
             an::SequenceFailure::MalformedContainer,
             "document",
             {}},
        Case{"<!doctype sequence><sequence/>",
             an::ErrorCode::InvalidData,
             an::SequenceFailure::MalformedContainer,
             "document",
             {}},
        Case{"<!DOCTYPE sequence", an::ErrorCode::InvalidData, an::SequenceFailure::MalformedContainer, "document", {}},
        Case{"<sequence><frame path=\"a\"/>",
             an::ErrorCode::InvalidData,
             an::SequenceFailure::MalformedContainer,
             "document",
             {}},
        Case{"<list><sequence/></list>",
             an::ErrorCode::InvalidData,
             an::SequenceFailure::MalformedContainer,
             "sequence",
             {}},
        Case{"<sequence><frame/></sequence>", an::ErrorCode::InvalidData, an::SequenceFailure::MalformedContainer,
             "path", 0u},
        Case{"<sequence><frame path=\"a\"/><frame path=\"\"/></sequence>", an::ErrorCode::InvalidData,
             an::SequenceFailure::MalformedContainer, "path", 1u},
        Case{"<sequence><frame path=\"a&#0;b\"/></sequence>", an::ErrorCode::InvalidData,
             an::SequenceFailure::MalformedContainer, "path", 0u},
        Case{"<sequence><frame path=\"&#xD800;\"/></sequence>", an::ErrorCode::InvalidData,
             an::SequenceFailure::MalformedContainer, "path", 0u},
        Case{"<sequence><frame path=\"&#4294967361;\"/></sequence>", an::ErrorCode::InvalidData,
             an::SequenceFailure::MalformedContainer, "path", 0u},
        Case{"<sequence version=\"2\"/>",
             an::ErrorCode::Unsupported,
             an::SequenceFailure::UnsupportedEncoding,
             "version",
             {}},
        Case{"<sequence version=\"\"/>",
             an::ErrorCode::Unsupported,
             an::SequenceFailure::UnsupportedEncoding,
             "version",
             {}},
    };
    const std::string path = pathOf("malformed.anseq");
    for (const Case& one : cases)
    {
        writeText(path, one.text);
        auto refused = an::readImageListManifest(engine, execution, path);
        ASSERT_FALSE(refused.hasValue()) << one.text;
        EXPECT_EQ(refused.status().code(), one.code) << one.text;
        EXPECT_EQ(an::sequenceFailureReason(refused.status()), one.reason) << one.text;
        EXPECT_EQ(parameterOf(refused.status()), one.parameter) << one.text;
        EXPECT_EQ(requestedOf(refused.status()), one.requested) << one.text;
    }

    writeText(path, "<sequence version=\" 1\"><frame path=\"a\"/></sequence>");
    EXPECT_TRUE(an::readImageListManifest(engine, execution, path).hasValue());

    // A document type declaration before the manifest is skipped, internal subset and all, as the library skipped it;
    // only its lowercase spelling and an unterminated one are refused, above.
    writeText(path, "<!DOCTYPE sequence><sequence/>");
    auto declared = an::readImageListManifest(engine, execution, path);
    ASSERT_TRUE(declared.hasValue()) << declared.status().message();
    EXPECT_EQ(*declared, an::ImageListManifest{});
    writeText(path, "<!DOCTYPE sequence [<!ELEMENT sequence ANY>]><sequence width=\"3\"/>");
    auto subset = an::readImageListManifest(engine, execution, path);
    ASSERT_TRUE(subset.hasValue()) << subset.status().message();
    EXPECT_EQ(subset->width, 3u);

    auto missing = an::readImageListManifest(engine, execution, pathOf("missing.anseq"));
    ASSERT_FALSE(missing.hasValue());
    EXPECT_EQ(missing.status().code(), an::ErrorCode::ExternalFailure);
    EXPECT_EQ(an::sequenceFailureReason(missing.status()), an::SequenceFailure::IoFailure);
    EXPECT_NE(missing.status().findContext(an::ContextKey::LibraryCode), nullptr);
    auto folder = an::readImageListManifest(engine, execution, utf8(directory));
    ASSERT_FALSE(folder.hasValue());
    EXPECT_EQ(an::sequenceFailureReason(folder.status()), an::SequenceFailure::IoFailure);
    for (const std::string_view malformed : {""sv, std::string_view("a\0b.anseq", 9u), "\xff.anseq"sv})
    {
        auto refused = an::readImageListManifest(engine, execution, malformed);
        ASSERT_FALSE(refused.hasValue());
        EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);
        EXPECT_EQ(parameterOf(refused.status()), "path");
    }
}

TEST_F(ImageListManifestTest, AManifestBeyondItsBoundsIsRefused)
{
    const std::string large = pathOf("large.anseq");
    writeText(large, "");
    std::filesystem::resize_file(native(large), an::kMaxImageListManifestBytes + 1u);
    auto tooLarge = an::readImageListManifest(engine, execution, large);
    ASSERT_FALSE(tooLarge.hasValue());
    EXPECT_EQ(tooLarge.status().code(), an::ErrorCode::LimitExceeded);
    EXPECT_EQ(parameterOf(tooLarge.status()), "path");
    EXPECT_EQ(engine.memoryResource()->counters().peakBytes, 0u);
    std::filesystem::remove(native(large));

    std::string text = "<sequence>";
    text.reserve(18u * (an::kMaxImageListManifestFrames + 1u) + 24u);
    for (std::uint64_t index = 0u; index <= an::kMaxImageListManifestFrames; ++index)
        text += "<frame path=\"a\"/>";
    text += "</sequence>";
    const std::string crowded = pathOf("crowded.anseq");
    writeText(crowded, text);
    auto tooMany = an::readImageListManifest(engine, execution, crowded);
    ASSERT_FALSE(tooMany.hasValue());
    EXPECT_EQ(tooMany.status().code(), an::ErrorCode::LimitExceeded);
    EXPECT_EQ(parameterOf(tooMany.status()), "frame");

    const an::ImageListManifest overlong{.framePaths =
                                             std::vector<std::string>(an::kMaxImageListManifestFrames + 1u, "a")};
    auto refused =
        an::writeImageListManifest(engine, execution, {.destinationPath = pathOf("overlong.anseq")}, overlong);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::LimitExceeded);
    EXPECT_EQ(parameterOf(refused.status()), "framePaths");
    EXPECT_FALSE(std::filesystem::exists(native(pathOf("overlong.anseq"))));
}

TEST_F(ImageListManifestTest, AManifestThatCannotBeWrittenReservesNothing)
{
    const std::string destination = pathOf("refused.anseq");
    const std::array refusals{
        std::pair{an::ImageListManifest{.framePaths = {"a", ""}}, "framePaths"sv},
        std::pair{an::ImageListManifest{.framePaths = {std::string("a\0b", 3u)}}, "framePaths"sv},
        std::pair{an::ImageListManifest{.framesPerSecond = std::numeric_limits<double>::infinity()},
                  "framesPerSecond"sv},
        std::pair{an::ImageListManifest{.framesPerSecond = std::numeric_limits<double>::quiet_NaN()},
                  "framesPerSecond"sv},
    };
    for (const auto& [manifest, parameter] : refusals)
    {
        auto refused = an::writeImageListManifest(engine, execution, {.destinationPath = destination}, manifest);
        ASSERT_FALSE(refused.hasValue()) << parameter;
        EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);
        EXPECT_EQ(parameterOf(refused.status()), parameter);
    }
    EXPECT_EQ(requestedOf(an::writeImageListManifest(engine, execution, {.destinationPath = destination},
                                                     an::ImageListManifest{.framePaths = {"a", ""}})
                              .status()),
              1u);
    EXPECT_TRUE(std::filesystem::is_empty(directory));
}

TEST_F(ImageListManifestTest, AFramePathResolvesAgainstTheManifestsDirectory)
{
    const auto resolved = [](std::string_view manifest, std::string_view frame) {
        auto path = an::resolveImageListFramePath(manifest, frame);
        return path.hasValue() ? *path : "<" + parameterOf(path.status()) + ">";
    };
    EXPECT_EQ(resolved("lists/sub/lights.anseq", "lights-frames/a.fits"), "lists/sub/lights-frames/a.fits");
    EXPECT_EQ(resolved("C:\\data\\lights.anseq", "lights-frames/a.fits"), "C:\\data\\lights-frames/a.fits");
    EXPECT_EQ(resolved("lights.anseq", "a.fits"), "a.fits");
    EXPECT_EQ(resolved("/lights.anseq", "a.fits"), "/a.fits");
    EXPECT_EQ(resolved("lists/sub/lights.anseq", "../a.fits"), "lists/sub/../a.fits");
#ifdef _WIN32
    EXPECT_EQ(resolved("lists/lights.anseq", "D:\\frames\\a.fits"), "D:\\frames\\a.fits");
    EXPECT_EQ(resolved("lists/lights.anseq", "\\\\server\\share\\a.fits"), "\\\\server\\share\\a.fits");
    EXPECT_EQ(resolved("D:\\lists\\lights.anseq", "C:frames\\a.fits"), "<framePath>");
    EXPECT_EQ(resolved("D:\\lists\\lights.anseq", "\\frames\\a.fits"), "<framePath>");
#else
    EXPECT_EQ(resolved("lists/lights.anseq", "/frames/a.fits"), "/frames/a.fits");
    EXPECT_EQ(resolved("lists/lights.anseq", "D:\\frames\\a.fits"), "lists/D:\\frames\\a.fits");
#endif
    EXPECT_EQ(resolved("lights.anseq", ""), "<framePath>");
    EXPECT_EQ(resolved("", "a.fits"), "<manifestPath>");
    EXPECT_EQ(resolved(std::string_view("a\0.anseq", 8u), "a.fits"), "<manifestPath>");

    const std::string deep = std::string(131000u, 'd') + "/lights.anseq";
    auto overlong = an::resolveImageListFramePath(deep, std::string(200u, 'f'));
    ASSERT_FALSE(overlong.hasValue());
    EXPECT_EQ(overlong.status().code(), an::ErrorCode::LimitExceeded);
    EXPECT_EQ(parameterOf(overlong.status()), "framePath");
}

TEST_F(ImageListManifestTest, TheFramesDirectoryIsNamedAfterTheManifest)
{
    const auto framesDirectory = [](std::string_view manifest) {
        auto named = an::imageListFramesDirectory(manifest);
        return named.hasValue() ? *named : "<" + parameterOf(named.status()) + ">";
    };
    EXPECT_EQ(framesDirectory("lists/lights.anseq"), "lists/lights-frames");
    EXPECT_EQ(framesDirectory("lists\\LIGHTS.ANSEQ"), "lists\\LIGHTS-frames");
    EXPECT_EQ(framesDirectory("a.b.anseq"), "a.b-frames");
    EXPECT_EQ(framesDirectory("\xe6\x98\x9f.AnSeq"), "\xe6\x98\x9f-frames");
    for (const std::string_view refused : {"lists/lights.ser"sv, "lists/.anseq"sv, ".anseq"sv, ""sv,
                                           "lists/lights.anseq/"sv, "anseq"sv, "lights.anseq.bak"sv})
    {
        auto directoryOf = an::imageListFramesDirectory(refused);
        ASSERT_FALSE(directoryOf.hasValue()) << refused;
        EXPECT_EQ(directoryOf.status().code(), an::ErrorCode::InvalidArgument);
        EXPECT_EQ(parameterOf(directoryOf.status()), "manifestPath");
    }
}

TEST_F(ImageListManifestTest, RemovingAListRemovesItsFramesDirectoryAndManifestAndNothingElse)
{
    const std::string manifest = pathOf("lights.anseq");
    writeText(manifest, "<sequence/>");
    std::filesystem::create_directories(native(pathOf("lights-frames/nested")));
    writeText(pathOf("lights-frames/a.fits"), "a");
    writeText(pathOf("lights-frames/nested/b.fits"), "b");
    writeText(pathOf("lights-other.fits"), "kept");
    ASSERT_TRUE(an::removeImageListFromDisk(manifest).ok());
    EXPECT_FALSE(std::filesystem::exists(native(manifest)));
    EXPECT_FALSE(std::filesystem::exists(native(pathOf("lights-frames"))));
    EXPECT_EQ(readAll(pathOf("lights-other.fits")), "kept");
    EXPECT_TRUE(an::removeImageListFromDisk(manifest).ok());

    // A frames directory's name taken by a file is not the list's to remove.
    const std::string darks = pathOf("darks.anseq");
    writeText(darks, "<sequence/>");
    writeText(pathOf("darks-frames"), "not a directory");
    ASSERT_TRUE(an::removeImageListFromDisk(darks).ok());
    EXPECT_FALSE(std::filesystem::exists(native(darks)));
    EXPECT_EQ(readAll(pathOf("darks-frames")), "not a directory");

    auto refused = an::removeImageListFromDisk(pathOf("lights.ser"));
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(parameterOf(refused), "manifestPath");
}

TEST_F(ImageListManifestTest, RemovingAListRemovesADanglingFramesDirectoryLinkWithoutFollowingIt)
{
    const std::string manifest = pathOf("linked.anseq");
    writeText(manifest, "<sequence/>");
    const std::filesystem::path link = native(pathOf("linked-frames"));
    const std::filesystem::path absentTarget = native(pathOf("absent-target"));
    std::error_code error;
    std::filesystem::create_directory_symlink(absentTarget, link, error);
    if (error)
        GTEST_SKIP() << "this host cannot create a directory symbolic link: " << error.message();

    ASSERT_TRUE(std::filesystem::is_symlink(std::filesystem::symlink_status(link)));
    ASSERT_TRUE(an::removeImageListFromDisk(manifest).ok());
    EXPECT_EQ(std::filesystem::symlink_status(link).type(), std::filesystem::file_type::not_found);
    EXPECT_FALSE(std::filesystem::exists(native(manifest)));
}

TEST_F(ImageListManifestTest, ASinkPublishesEachFrameAndThenTheManifestItsOutputCommits)
{
    const std::string destination = pathOf("lights.anseq");
    auto output = an::SequenceOutput::begin(
        engine, execution, {.destinationPath = destination, .durability = an::OutputDurability::CloseOnly});
    ASSERT_TRUE(output.hasValue()) << output.status().message();
    const std::vector<std::string> names{"light_001"};
    const an::SequenceDescriptor descriptor{.format = an::SequenceFormat::ImageList,
                                            .nominalFrameRate = an::SequenceRate{30000u, 1001u}};
    auto sink = an::createImageListSink(
        engine, execution, *output, descriptor,
        {.frameDurability = an::OutputDurability::CloseOnly, .timestampSeconds = 1714595462u, .frameNames = names});
    ASSERT_TRUE(sink.hasValue()) << sink.status().message();
    EXPECT_FALSE(std::filesystem::exists(native(pathOf("lights-frames"))));

    auto first = sink->append(engine, execution, sequenceFrame(11u, 0u));
    ASSERT_TRUE(first.hasValue()) << first.status().message();
    EXPECT_EQ(first->framesWritten, 1u);
    EXPECT_EQ(first->bytesWritten, 0u);
    EXPECT_TRUE(std::filesystem::is_regular_file(native(pathOf("lights-frames/light_001.fits"))));
    EXPECT_FALSE(std::filesystem::exists(native(destination)));
    auto second = sink->append(engine, execution, sequenceFrame(22u, 1u, 6u, 3u));
    ASSERT_TRUE(second.hasValue()) << second.status().message();
    EXPECT_TRUE(std::filesystem::is_regular_file(native(pathOf("lights-frames/lights-1.fits"))));

    auto finished = sink->finish(execution);
    ASSERT_TRUE(finished.hasValue()) << finished.status().message();
    EXPECT_EQ(finished->framesWritten, 2u);
    EXPECT_FALSE(std::filesystem::exists(native(destination)));
    auto committed = output->commit(execution, *finished);
    ASSERT_TRUE(committed.hasValue()) << committed.status().message();
    EXPECT_TRUE(sink->close().ok());
    const std::string expected =
        headOf("timestamp=\"1714595462\" width=\"4\" height=\"2\" channels=\"1\" isCFA=\"false\" fps=\"29.97\">\n") +
        "\t<frame path=\"lights-frames/light_001.fits\" />\n\t<frame path=\"lights-frames/lights-1.fits\" />\n"
        "</sequence>\n";
    EXPECT_EQ(readAll(destination), expected);
    EXPECT_EQ(finished->bytesWritten, expected.size());

    // The manifest opens as the list it names, each frame in its own representation.
    auto manifest = an::readImageListManifest(engine, execution, destination);
    ASSERT_TRUE(manifest.hasValue()) << manifest.status().message();
    std::vector<std::string> paths;
    for (const std::string& framePath : manifest->framePaths)
    {
        auto resolvedPath = an::resolveImageListFramePath(destination, framePath);
        ASSERT_TRUE(resolvedPath.hasValue()) << resolvedPath.status().message();
        paths.push_back(std::move(*resolvedPath));
    }
    std::vector<an::CodecReadRequest> requests;
    for (const std::string& path : paths)
        requests.push_back({path, an::CodecFormat::Fits});
    auto source = an::createImageListSource(engine, execution, requests, {});
    ASSERT_TRUE(source.hasValue()) << source.status().message();
    for (const unsigned value : {11u, 22u})
    {
        auto read = source->read(engine, execution);
        ASSERT_TRUE(read.hasValue()) << read.status().message();
        ASSERT_TRUE(read->frame.has_value());
        EXPECT_EQ(read->frame->frame.descriptor()->sampleFormat().sizeBytes, 1u);
        ASSERT_FALSE(read->frame->frame.bytes().empty());
        EXPECT_EQ(read->frame->frame.bytes().front(), static_cast<std::byte>(value));
    }
}

TEST_F(ImageListManifestTest, AMosaicListStatesItsFirstFrameAndAnIntentIsHonouredInEachFramesOwnRepresentation)
{
    const std::string mosaics = pathOf("mosaics.anseq");
    {
        auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = mosaics});
        ASSERT_TRUE(output.hasValue());
        auto sink = an::createImageListSink(engine, execution, *output, {.format = an::SequenceFormat::ImageList},
                                            {.timestampSeconds = 5u});
        ASSERT_TRUE(sink.hasValue()) << sink.status().message();
        auto appended = sink->append(engine, execution, sequenceFrame(7u, 0u, 8u, 4u, true));
        ASSERT_TRUE(appended.hasValue()) << appended.status().message();
        auto finished = sink->finish(execution);
        ASSERT_TRUE(finished.hasValue());
        ASSERT_TRUE(output->commit(execution, *finished).hasValue());
    }
    EXPECT_EQ(readAll(mosaics),
              headOf("timestamp=\"5\" width=\"8\" height=\"4\" channels=\"1\" isCFA=\"true\" fps=\"0\">\n") +
                  "\t<frame path=\"mosaics-frames/mosaics-0.fits\" />\n</sequence>\n");

    // An intent is honoured in the representation each frame already has, because the writer converts nothing: a
    // frame the intent would convert is refused unwritten and fails the sink, whose unfinished list leaves nothing.
    const std::string floats = pathOf("floats.anseq");
    auto floatIntent = an::OutputIntent::create({.sampleType = an::SampleType::Float32});
    ASSERT_TRUE(floatIntent.hasValue()) << floatIntent.status().message();
    {
        auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = floats});
        ASSERT_TRUE(output.hasValue());
        auto sink = an::createImageListSink(engine, execution, *output, {.format = an::SequenceFormat::ImageList},
                                            {.frameIntent = *floatIntent});
        ASSERT_TRUE(sink.hasValue()) << sink.status().message();
        auto refused = sink->append(engine, execution, sequenceFrame(9u, 0u));
        ASSERT_FALSE(refused.hasValue());
        EXPECT_EQ(refused.status().code(), an::ErrorCode::IncompatibleArguments);
        EXPECT_FALSE(sink->finish(execution).hasValue());
        EXPECT_TRUE(sink->close().ok());
    }
    EXPECT_FALSE(std::filesystem::exists(native(floats)));
    EXPECT_FALSE(std::filesystem::exists(native(pathOf("floats-frames"))));

    // In the frame's own representation the intent is what each frame is written under, cards and all.
    const std::string bytes = pathOf("bytes.anseq");
    auto card = an::CodecCard::create(an::CodecNamespace::Fits, "ANLISTOK", "1", "");
    ASSERT_TRUE(card.hasValue()) << card.status().message();
    auto byteIntent = an::OutputIntent::create({.sampleType = an::SampleType::UInt8, .cards = {*card}});
    ASSERT_TRUE(byteIntent.hasValue()) << byteIntent.status().message();
    {
        auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = bytes});
        ASSERT_TRUE(output.hasValue());
        auto sink = an::createImageListSink(engine, execution, *output, {.format = an::SequenceFormat::ImageList},
                                            {.frameIntent = *byteIntent});
        ASSERT_TRUE(sink.hasValue()) << sink.status().message();
        auto appended = sink->append(engine, execution, sequenceFrame(9u, 0u));
        ASSERT_TRUE(appended.hasValue()) << appended.status().message();
        auto finished = sink->finish(execution);
        ASSERT_TRUE(finished.hasValue()) << finished.status().message();
        ASSERT_TRUE(output->commit(execution, *finished).hasValue());
        EXPECT_TRUE(sink->close().ok());
    }
    const std::string framePath = pathOf("bytes-frames/bytes-0.fits");
    auto decoded = an::decodeImage(engine, execution, {framePath, an::CodecFormat::Fits});
    ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();
    EXPECT_EQ(decoded->frame.descriptor()->sampleFormat().sizeBytes, 1u);
    EXPECT_NE(readAll(framePath).find("ANLISTOK"), std::string::npos) << "the intent's card is written with the frame";
}

TEST_F(ImageListManifestTest, AnEmptyListIsAManifestOfNoFramesAndCreatesNoDirectory)
{
    const std::string destination = pathOf("empty.anseq");
    auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = destination});
    ASSERT_TRUE(output.hasValue());
    auto sink =
        an::createImageListSink(engine, execution, *output, {.format = an::SequenceFormat::ImageList, .frameCount = 0u},
                                {.timestampSeconds = 9u});
    ASSERT_TRUE(sink.hasValue()) << sink.status().message();
    auto finished = sink->finish(execution);
    ASSERT_TRUE(finished.hasValue()) << finished.status().message();
    ASSERT_TRUE(output->commit(execution, *finished).hasValue());
    EXPECT_EQ(readAll(destination),
              headOf("timestamp=\"9\" width=\"0\" height=\"0\" channels=\"0\" isCFA=\"false\" fps=\"0\" />\n"));
    EXPECT_FALSE(std::filesystem::exists(native(pathOf("empty-frames"))));
}

TEST_F(ImageListManifestTest, ASinkRefusesWhatAManifestCannotStateBeforeItClaimsTheOutput)
{
    const std::string destination = pathOf("lights.anseq");
    auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = destination});
    ASSERT_TRUE(output.hasValue());
    const an::SequenceDescriptor list{.format = an::SequenceFormat::ImageList};
    const std::vector<std::string> separator{"a/b"};
    const std::vector<std::string> emptySecond{"ok", ""};
    const std::vector<std::string> dots{".."};
    const std::vector<std::string> backslash{"x\\y"};
    struct Case
    {
        const char* label;
        an::SequenceDescriptor descriptor;
        an::ImageListSinkRequest request;
        an::ErrorCode code;
        std::string_view parameter;
        std::optional<std::uint64_t> requested;
    };
    const std::array cases{
        Case{"container", {.format = an::SequenceFormat::Ser}, {}, an::ErrorCode::InvalidArgument, "format", {}},
        Case{"seek",
             {.format = an::SequenceFormat::ImageList, .frameCount = 2u, .seekByIndex = true},
             {},
             an::ErrorCode::InvalidArgument,
             "seekByIndex",
             {}},
        Case{"timestamps",
             {.format = an::SequenceFormat::ImageList,
              .timeBase = an::SequenceRate{1u, 1000u},
              .timestampsRequired = true},
             {},
             an::ErrorCode::Unsupported,
             "timestampsRequired",
             {}},
        Case{"dump", list, {.frameFormat = an::CodecFormat::Dump}, an::ErrorCode::InvalidArgument, "frameFormat", {}},
        Case{"unknown",
             list,
             {.frameFormat = an::CodecFormat::Unknown},
             an::ErrorCode::InvalidArgument,
             "frameFormat",
             {}},
        Case{"outside",
             list,
             {.frameFormat = static_cast<an::CodecFormat>(99)},
             an::ErrorCode::InvalidArgument,
             "frameFormat",
             {}},
        Case{"unencodable", list, {.frameFormat = an::CodecFormat::Png}, an::ErrorCode::Unsupported, "frameFormat", {}},
        Case{"durability",
             list,
             {.frameDurability = static_cast<an::OutputDurability>(7)},
             an::ErrorCode::InvalidArgument,
             "frameDurability",
             {}},
        Case{"separator", list, {.frameNames = separator}, an::ErrorCode::InvalidArgument, "frameNames", 0u},
        Case{"empty name", list, {.frameNames = emptySecond}, an::ErrorCode::InvalidArgument, "frameNames", 1u},
        Case{"dot directory", list, {.frameNames = dots}, an::ErrorCode::InvalidArgument, "frameNames", 0u},
        Case{"backslash", list, {.frameNames = backslash}, an::ErrorCode::InvalidArgument, "frameNames", 0u},
    };
    for (const Case& one : cases)
    {
        auto refused = an::createImageListSink(engine, execution, *output, one.descriptor, one.request);
        ASSERT_FALSE(refused.hasValue()) << one.label;
        EXPECT_EQ(refused.status().code(), one.code) << one.label;
        EXPECT_EQ(parameterOf(refused.status()), one.parameter) << one.label;
        EXPECT_EQ(requestedOf(refused.status()), one.requested) << one.label;
    }
    auto stillFree = output->claimWriter();
    EXPECT_TRUE(stillFree.hasValue()) << "a refused sink leaves the output unclaimed";
    stillFree->release();
    EXPECT_FALSE(std::filesystem::exists(native(pathOf("lights-frames"))));

    auto series = an::SequenceOutput::begin(engine, execution, {.destinationPath = pathOf("lights.ser")});
    ASSERT_TRUE(series.hasValue());
    auto notManifest = an::createImageListSink(engine, execution, *series, list, {});
    ASSERT_FALSE(notManifest.hasValue());
    EXPECT_EQ(notManifest.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(parameterOf(notManifest.status()), "destinationPath");

    an::SequenceOutput nothing;
    EXPECT_EQ(an::createImageListSink(engine, execution, nothing, list, {}).status().code(),
              an::ErrorCode::InvalidState);
    {
        auto other = an::EngineContext::create({.maxConcurrency = 1u});
        ASSERT_TRUE(other.hasValue());
        auto otherExecution = an::ExecutionContext::create(*other, {.maxConcurrency = 1u});
        ASSERT_TRUE(otherExecution.hasValue());
        auto foreign = an::SequenceOutput::begin(*other, *otherExecution, {.destinationPath = pathOf("foreign.anseq")});
        ASSERT_TRUE(foreign.hasValue());
        EXPECT_EQ(an::createImageListSink(engine, execution, *foreign, list, {}).status().code(),
                  an::ErrorCode::IncompatibleArguments);
    }
}

TEST_F(ImageListManifestTest, AnUnfinishedSinkRemovesWhatItPublishedAndNothingElse)
{
    const std::string lights = pathOf("lights.anseq");
    auto output = an::SequenceOutput::begin(engine, execution, {.destinationPath = lights});
    ASSERT_TRUE(output.hasValue());
    {
        auto sink = an::createImageListSink(engine, execution, *output, {.format = an::SequenceFormat::ImageList}, {});
        ASSERT_TRUE(sink.hasValue()) << sink.status().message();
        ASSERT_TRUE(sink->append(engine, execution, sequenceFrame(1u, 0u)).hasValue());
        ASSERT_TRUE(sink->append(engine, execution, sequenceFrame(2u, 1u)).hasValue());
        EXPECT_TRUE(sink->close().ok());
    }
    EXPECT_FALSE(std::filesystem::exists(native(pathOf("lights-frames"))));
    EXPECT_FALSE(std::filesystem::exists(native(lights)));
    // The output is back to its caller unpublished, and a second sink may write it.
    {
        auto again = an::createImageListSink(engine, execution, *output, {.format = an::SequenceFormat::ImageList}, {});
        ASSERT_TRUE(again.hasValue()) << again.status().message();
        ASSERT_TRUE(again->append(engine, execution, sequenceFrame(3u, 0u)).hasValue());
        // Destroyed without a close: the frame it published goes all the same.
    }
    EXPECT_FALSE(std::filesystem::exists(native(pathOf("lights-frames"))));

    // A directory the sink found is left with what it held before.
    const std::string darks = pathOf("darks.anseq");
    std::filesystem::create_directory(native(pathOf("darks-frames")));
    writeText(pathOf("darks-frames/keep.txt"), "kept");
    auto darkOutput = an::SequenceOutput::begin(engine, execution, {.destinationPath = darks});
    ASSERT_TRUE(darkOutput.hasValue());
    {
        auto sink =
            an::createImageListSink(engine, execution, *darkOutput, {.format = an::SequenceFormat::ImageList}, {});
        ASSERT_TRUE(sink.hasValue());
        ASSERT_TRUE(sink->append(engine, execution, sequenceFrame(4u, 0u)).hasValue());
        EXPECT_TRUE(std::filesystem::exists(native(pathOf("darks-frames/darks-0.fits"))));
        EXPECT_TRUE(sink->close().ok());
    }
    EXPECT_FALSE(std::filesystem::exists(native(pathOf("darks-frames/darks-0.fits"))));
    EXPECT_EQ(readAll(pathOf("darks-frames/keep.txt")), "kept");

    // A stopped append publishes nothing, and closing removes the directory it had created.
    std::stop_source stop;
    an::ExecutionOptions stoppable;
    stoppable.maxConcurrency = 1u;
    stoppable.stopToken = stop.get_token();
    auto stopping = an::ExecutionContext::create(engine, stoppable);
    ASSERT_TRUE(stopping.hasValue());
    const std::string flats = pathOf("flats.anseq");
    auto flatOutput = an::SequenceOutput::begin(engine, *stopping, {.destinationPath = flats});
    ASSERT_TRUE(flatOutput.hasValue());
    auto sink = an::createImageListSink(engine, *stopping, *flatOutput, {.format = an::SequenceFormat::ImageList}, {});
    ASSERT_TRUE(sink.hasValue());
    stop.request_stop();
    auto stopped = sink->append(engine, *stopping, sequenceFrame(5u, 0u));
    ASSERT_FALSE(stopped.hasValue());
    EXPECT_EQ(stopped.status().code(), an::ErrorCode::Cancelled);
    EXPECT_FALSE(std::filesystem::exists(native(pathOf("flats-frames/flats-0.fits"))));
    EXPECT_TRUE(sink->close().ok());
    EXPECT_FALSE(std::filesystem::exists(native(pathOf("flats-frames"))));
}

TEST_F(ImageListManifestTest, MutatedManifestsAreReadOrRefusedWithoutAnythingElseHappening)
{
    const std::string original =
        headOf("timestamp=\"1714595462\" width=\"640\" height=\"480\" channels=\"1\" isCFA=\"true\" fps=\"29.97\">\n") +
        "\t<frame path=\"light-frames/a&amp;b.fits\" />\n\t<frame path=\"/abs/&#233; &#x41;.fits\" />\n</sequence>\n";
    const std::string path = pathOf("mutated.anseq");
    std::mt19937_64 random(20260914u);
    constexpr std::string_view alphabet("<>/=\"'&;#x!?-[] \t\r\n\0\xff"
                                        "sequenceframepath0123456789",
                                        43u);
    std::uint64_t read = 0u;
    for (int round = 0; round < 4000; ++round)
    {
        std::string text = original;
        const auto edits = 1u + random() % 6u;
        for (std::uint64_t edit = 0u; edit < edits; ++edit)
        {
            const std::size_t at = random() % (text.size() + 1u);
            const char c = alphabet[random() % alphabet.size()];
            switch (random() % 3u)
            {
            case 0u:
                text.insert(text.begin() + static_cast<std::ptrdiff_t>(at), c);
                break;
            case 1u:
                if (at < text.size())
                    text.erase(at, 1u);
                break;
            default:
                if (at < text.size())
                    text[at] = c;
                break;
            }
        }
        writeText(path, text);
        auto manifest = an::readImageListManifest(engine, execution, path);
        if (manifest.hasValue())
        {
            ++read;
            for (const std::string& framePath : manifest->framePaths)
            {
                const auto resolved = an::resolveImageListFramePath(path, framePath);
                if (!resolved.hasValue())
                {
                    EXPECT_EQ(resolved.status().code(), an::ErrorCode::InvalidArgument);
                    EXPECT_EQ(parameterOf(resolved.status()), "framePath");
                }
            }
            continue;
        }
        const an::ErrorCode code = manifest.status().code();
        EXPECT_TRUE(code == an::ErrorCode::InvalidData || code == an::ErrorCode::Unsupported) << text;
        EXPECT_TRUE(an::sequenceFailureReason(manifest.status()).has_value()) << text;
    }
    EXPECT_GT(read, 0u);
    RecordProperty("manifestsRead", static_cast<int>(read));
}

TEST_F(ImageListManifestTest, EditingAListIsEditingItsPathsBeforeTheyAreWritten)
{
    an::ImageListManifest list{.framePaths = {"a.fits", "b.fits", "a.fits"}};
    // The lazy list's `update_image_path`: a frame past the end is refused, one within is replaced in place.
    const auto replace = [](an::ImageListManifest& manifest, std::size_t index, std::string path) {
        if (index >= manifest.framePaths.size())
            return false;
        manifest.framePaths[index] = std::move(path);
        return true;
    };
    // Its `remove_image_path`: the first path spelled the same goes, and a path the list lacks changes nothing.
    const auto remove = [](an::ImageListManifest& manifest, std::string_view path) {
        const auto found = std::find(manifest.framePaths.begin(), manifest.framePaths.end(), path);
        if (found != manifest.framePaths.end())
            manifest.framePaths.erase(found);
    };
    EXPECT_FALSE(replace(list, 3u, "z.fits"));
    EXPECT_TRUE(replace(list, 1u, "c.fits"));
    remove(list, "a.fits");
    remove(list, "missing.fits");
    EXPECT_EQ(list.framePaths, (std::vector<std::string>{"c.fits", "a.fits"}));
    const std::string path = pathOf("edited.anseq");
    ASSERT_TRUE(an::writeImageListManifest(engine, execution, {.destinationPath = path}, list).hasValue());
    auto read = an::readImageListManifest(engine, execution, path);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    EXPECT_EQ(read->framePaths, list.framePaths);
}

} // namespace
