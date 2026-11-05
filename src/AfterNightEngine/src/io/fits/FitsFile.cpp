// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - opening and closing FITS files, translating CFITSIO statuses, the sample
// type tables, and the version report for the linked CFITSIO.

#include "io/fits/FitsFile.h"

#include <AfterNightEngine/IO/CodecError.h>

#include "runtime/DependencyVersions.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace an
{
void FitsCloser::operator()(fitsfile* file) const noexcept
{
    if (file != nullptr)
    {
        int ignored = 0;
        fits_close_file(file, &ignored);
    }
}

std::string fitsPath(std::string_view path)
{
    std::string result(path);
#ifdef _WIN32
    std::replace(result.begin(), result.end(), '\\', '/');
#endif
    return result;
}

Status fitsFailure(int status, const char* operation, std::string_view path, bool writing)
{
    char message[FLEN_STATUS]{};
    fits_get_errstatus(status, message);
    // The library's message stack is shared state. The numeric code is sufficient and is never
    // obtained by draining another concurrent operation's messages.
    const auto code = status == MEMORY_ALLOCATION ? ErrorCode::AllocationFailed : ErrorCode::ExternalFailure;
    return Status::failure(code,
                           {.message = message,
                            .operation = operation,
                            .domain = ErrorDomain::Io,
                            .context = {StatusContextField::ofNumber(ContextKey::LibraryCode, status),
                                        StatusContextField::ofText(ContextKey::Path, std::string(path)),
                                        StatusContextField::ofCount(
                                            ContextKey::CodecReason,
                                            static_cast<std::uint64_t>(writing ? CodecFailure::ShortWrite
                                                                               : CodecFailure::MetadataUnreadable))}});
}

Result<FitsHandle> openFits(std::string_view path)
{
    if (path.size() >= FLEN_FILENAME - 1u)
        return Status::failure(ErrorCode::LimitExceeded);
    fitsfile* raw = nullptr;
    int status = 0;
    fits_open_diskfile(&raw, fitsPath(path).c_str(), READONLY, &status);
    FitsHandle result(raw);
    if (status != 0)
        return fitsFailure(status, "open FITS", path);
    return result;
}

int fitsDatatype(RawSampleFormat format) noexcept
{
    if (format.sampleClass == RawSampleClass::FloatingPoint)
        return format.sizeBytes == 4u ? TFLOAT : format.sizeBytes == 8u ? TDOUBLE : 0;
    if (format.sampleClass == RawSampleClass::UnsignedInteger)
    {
        switch (format.sizeBytes)
        {
        case 1u:
            return TBYTE;
        case 2u:
            return TUSHORT;
        case 4u:
            return TUINT;
        case 8u:
            return TULONGLONG;
        default:
            return 0;
        }
    }
    if (format.sampleClass == RawSampleClass::SignedInteger)
    {
        switch (format.sizeBytes)
        {
        case 1u:
            return TSBYTE;
        case 2u:
            return TSHORT;
        case 4u:
            return TINT;
        case 8u:
            return TLONGLONG;
        default:
            return 0;
        }
    }
    return 0;
}

int fitsImageType(RawSampleFormat format) noexcept
{
    switch (fitsDatatype(format))
    {
    case TFLOAT:
        return FLOAT_IMG;
    case TDOUBLE:
        return DOUBLE_IMG;
    case TBYTE:
        return BYTE_IMG;
    case TSBYTE:
        return SBYTE_IMG;
    case TUSHORT:
        return USHORT_IMG;
    case TSHORT:
        return SHORT_IMG;
    case TUINT:
        return ULONG_IMG;
    case TINT:
        return LONG_IMG;
    case TULONGLONG:
        return ULONGLONG_IMG;
    case TLONGLONG:
        return LONGLONG_IMG;
    default:
        return 0;
    }
}

namespace dependency_detail
{

void reportCfitsio(DependencyVersion& entry) noexcept
{
    entry.name = "cfitsio";
    // Windows links the packaged DLL. Linux links the packaged position-independent archive,
    // because the distribution ships the runtime without a development link - so the copy inside
    // this binary is fixed at build time there and replaceable here.
#if defined(_WIN32)
    entry.linkage = DependencyLinkage::SharedLibrary;
#else
    entry.linkage = DependencyLinkage::StaticArchive;
#endif
    setVersionTextFromParts(entry.compiled, CFITSIO_MAJOR, CFITSIO_MINOR, CFITSIO_MICRO);

    // CFITSIO's run-time version is a float rather than a string: the library packs the triple as
    // major + minor/100 + micro/10000, which its own header is what constrains the minor and micro
    // numbers to two digits each. Recovering the triple is therefore exact rather than a guess -
    // the encoded integer needs sixteen bits and a float carries twenty-four - and it is the one
    // arithmetic decode in this report.
    float packed = 0.0f;
    (void)ffvers(&packed);
    const auto encoded = static_cast<std::uint32_t>(std::lround(static_cast<double>(packed) * 10000.0));
    setVersionTextFromParts(entry.loaded, encoded / 10000u, (encoded / 100u) % 100u, encoded % 100u);
}

} // namespace dependency_detail

} // namespace an
