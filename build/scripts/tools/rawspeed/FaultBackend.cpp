// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Test-only C ABI replacement. Built into obj/.../fault; never installed or shipped.
// The qualification runner copies it into an isolated runtime closure to exercise engine retries.
#include <AnRawSpeed.h>
#include <cstdlib>

extern "C" {
uint32_t anRsVersion() noexcept
{
    return 1;
}
uint32_t anRsCorrectionRevision() noexcept
{
    return 1;
}
int anRsCreate(const AnRsOptions*, AnRsHandle* handle) noexcept
{
    if (!handle)
        return AnRsInvalidArgument;
    *handle = reinterpret_cast<void*>(1);
    return AnRsOk;
}
int anRsDecode(AnRsHandle, const void*, size_t, AnRsResult* result) noexcept
{
    if (!result)
        return AnRsInvalidArgument;
    *result = {};
    result->size = sizeof(*result);
    result->version = 1;
    const auto* value = std::getenv("AN_TEST_RAWSPEED_STATUS");
    result->status = value ? std::atoi(value) : AnRsInternal;
    return result->status;
}
void anRsDestroy(AnRsHandle) noexcept {}
}
