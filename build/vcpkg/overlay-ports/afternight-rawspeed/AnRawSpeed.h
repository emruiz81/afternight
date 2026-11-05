// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// AfterNight's private C ABI. No C++ object or allocation crosses compiler boundaries.
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && !defined(AN_RAWSPEED_STATIC)
#if defined(AN_RAWSPEED_BUILD)
#define AN_RS_API __declspec(dllexport)
#else
#define AN_RS_API __declspec(dllimport)
#endif
#else
#define AN_RS_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
#define AN_RS_NOEXCEPT noexcept
extern "C" {
#else
#define AN_RS_NOEXCEPT
#endif

enum AnRsStatus
{
    AnRsOk = 0,
    AnRsInvalidArgument,
    AnRsUnsupported,
    AnRsMalformed,
    AnRsWarnings,
    AnRsAllocationFailed,
    AnRsCancelled,
    AnRsGeometryMismatch,
    AnRsInternal
};
enum AnRsCorrection
{
    AnRsUnmodified = 0,
    AnRsNikonIntegerCurve,
    AnRsSonyIntegerCurve
};
enum AnRsFamily
{
    AnRsUnknown = 0,
    AnRsCanonCr2,
    AnRsNikonNef,
    AnRsBayerDng,
    AnRsFujiRaf,
    AnRsSonyArw2
};
enum AnRsPhase
{
    AnRsInitialize = 0,
    AnRsRead,
    AnRsUnpack,
    AnRsCorrect,
    AnRsPublish
};

// Returns nonzero to stop. Borrowed state lives until the handle is destroyed. Must not throw.
typedef int (*AnRsStop)(void* state, uint32_t phase, uint64_t completed);
typedef void* AnRsHandle;

typedef struct AnRsOptions
{
    uint32_t size;
    uint32_t version;
    uint32_t family;
    uint32_t correction;
    uint32_t width;
    uint32_t height;
    uint32_t filters;
    uint32_t reserved;
    uint8_t xtrans[36];
    AnRsStop stop;
    void* state;
} AnRsOptions;

typedef struct AnRsResult
{
    uint32_t size;
    uint32_t version;
    uint32_t status;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t filters;
    uint32_t components;
    const uint16_t* pixels;
    uint64_t workspaceBytes;
    uint64_t unpackMicroseconds;
    uint64_t correctionMicroseconds;
} AnRsResult;

// Version and correction revision are independent of the upstream source revision.
AN_RS_API uint32_t anRsVersion(void) AN_RS_NOEXCEPT;
AN_RS_API uint32_t anRsCorrectionRevision(void) AN_RS_NOEXCEPT;
AN_RS_API int anRsCreate(const AnRsOptions* options, AnRsHandle* handle) AN_RS_NOEXCEPT;
// Encoded input is borrowed until destroy; successful pixels are owned by the handle.
// Result must carry size/version; failures clear pixel/geometry fields and decoder state.
// A null/wrong-sized result is never written; only its supplied size may be read.
AN_RS_API int anRsDecode(AnRsHandle handle, const void* encoded, size_t bytes, AnRsResult* result) AN_RS_NOEXCEPT;
AN_RS_API void anRsDestroy(AnRsHandle handle) AN_RS_NOEXCEPT;

#ifdef __cplusplus
}
#endif
