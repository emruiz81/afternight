// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngine/Core/ExceptionBoundary.h.
//
// There is no adapter to a real library yet, so the boundary under test is a stand-in that behaves
// the way the real ones do: one entry point throws its own exception type, one throws something
// that is not a `std::exception` at all, one runs out of memory, and one reports by returning a
// number the way a C library does. Every failure mode an adapter has to translate is here, and the
// test is that none of them escapes and that what each becomes is actionable without reading a
// message.
//
// The worked adapter at the end is the point of the whole header: a function that calls the
// stand-in library, translates whatever it does, and reports in this engine's vocabulary while
// keeping what the library said underneath as a cause.

#include <AfterNightEngine/Core/ExceptionBoundary.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <new>
#include <stdexcept>
#include <string>

namespace
{

// A stand-in for a library this engine adapts. It is deliberately unlike the engine: its own
// exception type, its own status numbering, and a negative code, because those are the shapes the
// translation has to survive.
namespace fakelib
{

constexpr std::int64_t kFileNotReadable = -105;

class Error : public std::runtime_error
{
public:
    explicit Error(const std::string& what) : std::runtime_error(what) {}
};

void throwsItsOwnType()
{
    throw Error("the library could not open the file");
}

void throwsSomethingElse()
{
    throw 17;
}

void throwsOutOfMemory()
{
    throw std::bad_alloc();
}

std::int64_t reportsByReturning()
{
    return kFileNotReadable;
}

int produces()
{
    return 4;
}

} // namespace fakelib

constexpr an::ExternalBoundary kBoundary{
    .library = "fakelib", .operation = "FakeDecoder::open", .domain = an::ErrorDomain::Io};

TEST(ExceptionBoundaryTest, ACallThatReturnsNormallyIsASuccess)
{
    bool ran = false;
    const an::Status status = an::callAcrossBoundary(kBoundary, [&] { ran = true; });
    EXPECT_TRUE(status.ok());
    EXPECT_TRUE(ran);
    EXPECT_FALSE(status.hasDetail());
}

TEST(ExceptionBoundaryTest, AnExceptionBecomesAFailureThatNamesTheLibraryAndTheOperation)
{
    const an::Status status = an::callAcrossBoundary(kBoundary, [] { fakelib::throwsItsOwnType(); });

    ASSERT_TRUE(status.failed());
    EXPECT_EQ(status.code(), an::ErrorCode::ExternalFailure);
    EXPECT_EQ(status.category(), an::ErrorCategory::External);
    EXPECT_STREQ(status.message(), "the library could not open the file");
    EXPECT_STREQ(status.operation(), "FakeDecoder::open");
    EXPECT_EQ(status.domain(), an::ErrorDomain::Io);

    // Which library refused is a typed field rather than something to find in the message.
    const an::StatusContextField* library = status.findContext(an::ContextKey::Library);
    ASSERT_NE(library, nullptr);
    EXPECT_EQ(library->kind, an::ContextValueKind::Text);
    EXPECT_EQ(library->text, "fakelib");
}

TEST(ExceptionBoundaryTest, RunningOutOfMemoryIsAResourceFailureRatherThanAGenericExternalOne)
{
    // The one classification the translator makes on its own, and it matters: a caller retries an
    // allocation failure smaller and does nothing about a library refusing its input.
    const an::Status status = an::callAcrossBoundary(kBoundary, [] { fakelib::throwsOutOfMemory(); });
    EXPECT_EQ(status.code(), an::ErrorCode::AllocationFailed);
    EXPECT_EQ(status.category(), an::ErrorCategory::Resource);
    EXPECT_STREQ(status.operation(), "FakeDecoder::open");
}

TEST(ExceptionBoundaryTest, AnObjectThatIsNotAStandardExceptionStillDoesNotEscape)
{
    const an::Status status = an::callAcrossBoundary(kBoundary, [] { fakelib::throwsSomethingElse(); });
    EXPECT_EQ(status.code(), an::ErrorCode::ExternalFailure);
    EXPECT_STREQ(status.message(), "the external library threw an object that is not a std::exception");
    EXPECT_STREQ(status.operation(), "FakeDecoder::open");
}

TEST(ExceptionBoundaryTest, AValueProducingCallIsTranslatedTheSameWay)
{
    const an::Result<int> produced = an::evaluateAcrossBoundary(kBoundary, [] { return fakelib::produces(); });
    ASSERT_TRUE(produced.hasValue());
    EXPECT_EQ(*produced, 4);

    const an::Result<int> refused = an::evaluateAcrossBoundary(kBoundary, [] {
        fakelib::throwsItsOwnType();
        return 0;
    });
    EXPECT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::ExternalFailure);
    EXPECT_STREQ(refused.status().message(), "the library could not open the file");
}

TEST(ExceptionBoundaryTest, ALibrarysOwnStatusNumberIsKeptAsANumber)
{
    const std::int64_t libraryCode = fakelib::reportsByReturning();
    const an::Status status =
        an::translateExternalCode(kBoundary, an::ErrorCode::InvalidData, libraryCode, "the file is not readable");

    EXPECT_EQ(status.code(), an::ErrorCode::InvalidData);
    EXPECT_STREQ(status.message(), "the file is not readable");
    const an::StatusContextField* reported = status.findContext(an::ContextKey::LibraryCode);
    ASSERT_NE(reported, nullptr);
    EXPECT_EQ(reported->kind, an::ContextValueKind::Number);
    EXPECT_EQ(reported->number, fakelib::kFileNotReadable);
    ASSERT_NE(status.findContext(an::ContextKey::Library), nullptr);
}

TEST(ExceptionBoundaryTest, ATranslationWithoutAMessageStillExplainsItself)
{
    const an::Status status = an::translateExternalCode(kBoundary, an::ErrorCode::Unsupported, 3, nullptr);
    EXPECT_EQ(status.code(), an::ErrorCode::Unsupported);
    EXPECT_STREQ(status.message(), an::errorCodeDescription(an::ErrorCode::Unsupported));
}

TEST(ExceptionBoundaryTest, ATranslationCannotReportSuccess)
{
    const an::Status status = an::translateExternalCode(kBoundary, an::ErrorCode::Ok, 0, "nothing went wrong");
    EXPECT_TRUE(status.failed());
    EXPECT_EQ(status.code(), an::ErrorCode::Internal);
}

TEST(ExceptionBoundaryTest, ABoundaryWithNothingNamedProducesABlankRatherThanACrash)
{
    constexpr an::ExternalBoundary anonymous{};
    const an::Status status = an::callAcrossBoundary(anonymous, [] { fakelib::throwsItsOwnType(); });
    EXPECT_EQ(status.code(), an::ErrorCode::ExternalFailure);
    EXPECT_STREQ(status.operation(), "");
    EXPECT_EQ(status.findContext(an::ContextKey::Library), nullptr);
    EXPECT_EQ(status.domain(), an::ErrorDomain::Interop);
}

// The worked example: an adapter that owns the last place the library's vocabulary exists, and
// reports in the engine's own while keeping what it was told.
an::Result<int> openThroughTheAdapter(bool libraryThrows, std::int64_t libraryStatus)
{
    const an::Result<int> produced = an::evaluateAcrossBoundary(kBoundary, [&] {
        if (libraryThrows)
        {
            fakelib::throwsItsOwnType();
        }
        return fakelib::produces();
    });
    if (!produced.hasValue())
    {
        return an::Status::failure(an::ErrorCode::InvalidData,
                                   an::StatusDetail{.message = "the frame could not be opened",
                                                    .operation = "ImageLoader::open",
                                                    .domain = an::ErrorDomain::Io},
                                   produced.status());
    }
    if (libraryStatus != 0)
    {
        return an::Status::failure(an::ErrorCode::InvalidData,
                                   an::StatusDetail{.message = "the frame could not be opened",
                                                    .operation = "ImageLoader::open",
                                                    .domain = an::ErrorDomain::Io},
                                   an::translateExternalCode(kBoundary, an::ErrorCode::ExternalFailure, libraryStatus,
                                                             "the file is truncated"));
    }
    return produced;
}

TEST(ExceptionBoundaryTest, AnAdapterReportsInTheEnginesVocabularyAndKeepsWhatItWasTold)
{
    const an::Result<int> opened = openThroughTheAdapter(false, 0);
    ASSERT_TRUE(opened.hasValue());
    EXPECT_EQ(*opened, 4);

    const an::Result<int> thrown = openThroughTheAdapter(true, 0);
    ASSERT_FALSE(thrown.hasValue());
    EXPECT_EQ(thrown.code(), an::ErrorCode::InvalidData);
    EXPECT_STREQ(thrown.status().operation(), "ImageLoader::open");
    ASSERT_TRUE(thrown.status().hasCause());
    EXPECT_EQ(thrown.status().rootCause().code(), an::ErrorCode::ExternalFailure);
    EXPECT_STREQ(thrown.status().rootCause().message(), "the library could not open the file");

    const an::Result<int> refused = openThroughTheAdapter(false, fakelib::kFileNotReadable);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidData);
    const an::Status root = refused.status().rootCause();
    EXPECT_EQ(root.code(), an::ErrorCode::ExternalFailure);
    const an::StatusContextField* reported = root.findContext(an::ContextKey::LibraryCode);
    ASSERT_NE(reported, nullptr);
    EXPECT_EQ(reported->number, fakelib::kFileNotReadable);
}

} // namespace
