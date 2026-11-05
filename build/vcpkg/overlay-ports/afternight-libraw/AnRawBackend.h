// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once
#include "libraw_types.h"
#include <stdint.h>

#ifdef __cplusplus
#define AN_RAW_NOEXCEPT noexcept
extern "C" {
#else
#define AN_RAW_NOEXCEPT
#endif

enum AnRawFamily
{
    AnRawUnknown = 0,
    AnRawCanonCr2,
    AnRawNikonNef,
    AnRawBayerDng,
    AnRawFujiRaf,
    AnRawSonyArw2
};
enum AnRawOutcome
{
    AnRawNative = 0,
    AnRawAccelerated,
    AnRawUnsupported,
    AnRawMalformed,
    AnRawWarnings,
    AnRawAllocationFailed,
    AnRawCancelled,
    AnRawGeometryMismatch,
    AnRawMemoryRefused,
    AnRawInvalidArgument,
    AnRawInternal
};
typedef int (*AnRawStop)(void* state, uint32_t phase, uint64_t completed);
typedef struct AnRawIdentification
{
    uint32_t size, version, family, bits, compression, samples, sampleFormat, predictor;
    uint32_t width, height, filters, rawCount, imageIndex, supported;
    uint32_t mode, curveEntries, tileWidth, tileHeight, leftMargin, topMargin, visibleWidth, visibleHeight;
    uint64_t encodedBytes, workspaceEstimate;
} AnRawIdentification;
typedef struct AnRawOptions
{
    uint32_t size, version, family, enabled;
    AnRawStop stop;
    void* state;
} AnRawOptions;
typedef struct AnRawResult
{
    uint32_t size, version, outcome, correctionRevision;
    uint64_t workspaceBytes, readMicroseconds, unpackMicroseconds, correctionMicroseconds;
} AnRawResult;

// decoder is a borrowed LibRaw instance from THIS package, never a stock LibRaw pointer.
DllDef int anLibRawIdentify(void* decoder, AnRawIdentification* result) AN_RAW_NOEXCEPT;
DllDef int anLibRawConfigure(void* decoder, const AnRawOptions* options) AN_RAW_NOEXCEPT;
DllDef int anLibRawResult(void* decoder, AnRawResult* result) AN_RAW_NOEXCEPT;
#ifdef __cplusplus
}
#endif
