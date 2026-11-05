// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the FFTW calls behind the real transform plan, and FFTW's version report.
//
// The one translation unit of the engine that includes `fftw3.h`, and it uses the single-precision
// library alone: every plan is an `fftwf_` two-dimensional real plan. Planning uses
// `FFTW_ESTIMATE`, so it never overwrites the arrays it is measured over, and an inverse plan adds
// `FFTW_DESTROY_INPUT`, which is what the library offers for a multi-dimensional half-spectrum.
// Creation and destruction hold the one planner lock below; execution holds nothing, through the
// new-array interface, over buffers whose alignment is checked against the class the plan recorded.
//
// The recorded class is the largest power of two dividing the planning array's address, capped at
// `kFftwDefaultAlignmentBytes`, so a later buffer is held to that alignment and not to a larger one
// the planning buffer happened to have.

#include "interop/fftw/FftwRealTransform.h"

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/ErrorCode.h>

#include "runtime/DependencyVersions.h"

#include <fftw3.h>

#include <mutex>
#include <utility>

namespace an::fftw_detail
{
namespace
{

/// The lock serializing the library's planner.
///
/// FFTW's plan creation and destruction read and write process-wide wisdom, so two threads planning
/// unrelated transforms race. This mutex is the engine's serialization of that library-owned global
/// state - it guards nothing of the engine's own, which is why it does not live in an engine
/// context: two contexts still share one FFTW planner, because the library gives them no choice.
///
/// @return the one planner lock of this process.
[[nodiscard]] std::mutex& plannerLock()
{
    static std::mutex lock;
    return lock;
}

/// One failure from this adapter.
///
/// @param code which failure.
/// @param operation the entry point the caller is inside.
/// @param message what a person reads.
/// @return the failure, named as the interop layer's.
[[nodiscard]] Status fftwFailure(ErrorCode code, const char* operation, const char* message)
{
    return Status::failure(code,
                           StatusDetail{.message = message, .operation = operation, .domain = ErrorDomain::Interop});
}

} // namespace

FftwRealTransformPlan::FftwRealTransformPlan(FftwRealTransformPlan&& other) noexcept
    : m_plan(std::exchange(other.m_plan, nullptr)), m_direction(other.m_direction),
      m_alignmentBytes(std::exchange(other.m_alignmentBytes, 1u)), m_split(std::exchange(other.m_split, false))
{
}

FftwRealTransformPlan& FftwRealTransformPlan::operator=(FftwRealTransformPlan&& other) noexcept
{
    if (this != &other)
    {
        if (m_plan != nullptr)
        {
            const std::lock_guard<std::mutex> held{plannerLock()};
            fftwf_destroy_plan(static_cast<fftwf_plan>(m_plan));
        }
        m_plan = std::exchange(other.m_plan, nullptr);
        m_direction = other.m_direction;
        m_alignmentBytes = std::exchange(other.m_alignmentBytes, 1u);
        m_split = std::exchange(other.m_split, false);
    }
    return *this;
}

FftwRealTransformPlan::~FftwRealTransformPlan()
{
    if (m_plan != nullptr)
    {
        const std::lock_guard<std::mutex> held{plannerLock()};
        fftwf_destroy_plan(static_cast<fftwf_plan>(m_plan));
    }
}

Result<FftwRealTransformPlan> FftwRealTransformPlan::create(FftwTransformDirection direction,
                                                            const FftwArrayDescription& array, const char* operation)
{
    if (array.data == nullptr || array.precision != FftwPrecision::Single || array.kind != FftwArrayKind::Real ||
        array.n0 <= 0 || array.n1 <= 0)
    {
        return fftwFailure(ErrorCode::InvalidArgument, operation,
                           "an in-place real transform plan needs a described single-precision real plane");
    }
    const std::uint64_t paddedRow = fftwRealRowReals(static_cast<std::uint32_t>(array.n1), FftwPlacement::InPlace);
    if (static_cast<std::uint64_t>(array.rowStrideReals) != paddedRow)
    {
        return fftwFailure(ErrorCode::InvalidArgument, operation,
                           "an in-place real transform plan needs exactly the in-place padded row stride");
    }
    // The largest power of two dividing the base address is the plan's alignment class. Measuring
    // it from the pointer rather than taking a stated number is what lets `execute` promise that a
    // matching buffer really is interchangeable with the planned one.
    const auto address = reinterpret_cast<std::uintptr_t>(array.data);
    const std::uint64_t alignment = address == 0u ? 1u : (address & (~address + 1u));

    fftwf_plan plan = nullptr;
    {
        const std::lock_guard<std::mutex> held{plannerLock()};
        if (direction == FftwTransformDirection::Forward)
        {
            plan = fftwf_plan_dft_r2c_2d(array.n0, array.n1, static_cast<float*>(array.data),
                                         static_cast<fftwf_complex*>(array.data), FFTW_ESTIMATE);
        }
        else
        {
            plan = fftwf_plan_dft_c2r_2d(array.n0, array.n1, static_cast<fftwf_complex*>(array.data),
                                         static_cast<float*>(array.data), FFTW_ESTIMATE | FFTW_DESTROY_INPUT);
        }
    }
    if (plan == nullptr)
    {
        return Status::failure(ErrorCode::ExternalFailure,
                               StatusDetail{.message = "the transform library declined to build a plan over this "
                                                       "geometry",
                                            .operation = operation,
                                            .domain = ErrorDomain::Interop,
                                            .context = {StatusContextField::ofText(ContextKey::Library, "fftw3f")}});
    }
    return FftwRealTransformPlan{plan, direction,
                                 alignment > kFftwDefaultAlignmentBytes ? kFftwDefaultAlignmentBytes : alignment};
}

Result<FftwRealTransformPlan> FftwRealTransformPlan::createSplit(FftwTransformDirection direction,
                                                                 const FftwArrayDescription& real,
                                                                 const FftwArrayDescription& spectrum,
                                                                 const char* operation)
{
    if (real.data == nullptr || spectrum.data == nullptr || real.precision != FftwPrecision::Single ||
        spectrum.precision != FftwPrecision::Single || real.kind != FftwArrayKind::Real ||
        spectrum.kind != FftwArrayKind::InterleavedComplex || real.n0 <= 0 || real.n1 <= 0 || spectrum.n0 != real.n0 ||
        spectrum.n1 != real.n1 / 2 + 1 || real.rowStrideReals != real.n1 || spectrum.rowStrideReals != 2 * spectrum.n1)
    {
        return fftwFailure(ErrorCode::InvalidArgument, operation,
                           "a split real transform needs a tightly packed real plane and its matching "
                           "single-precision half-spectrum");
    }

    const auto realAddress = reinterpret_cast<std::uintptr_t>(real.data);
    const auto spectrumAddress = reinterpret_cast<std::uintptr_t>(spectrum.data);
    const std::uint64_t realAlignment = realAddress == 0u ? 1u : (realAddress & (~realAddress + 1u));
    const std::uint64_t spectrumAlignment = spectrumAddress == 0u ? 1u : (spectrumAddress & (~spectrumAddress + 1u));
    const std::uint64_t alignment = realAlignment < spectrumAlignment ? realAlignment : spectrumAlignment;

    fftwf_plan plan = nullptr;
    {
        const std::lock_guard<std::mutex> held{plannerLock()};
        if (direction == FftwTransformDirection::Forward)
        {
            plan = fftwf_plan_dft_r2c_2d(real.n0, real.n1, static_cast<float*>(real.data),
                                         static_cast<fftwf_complex*>(spectrum.data), FFTW_ESTIMATE);
        }
        else
        {
            plan = fftwf_plan_dft_c2r_2d(real.n0, real.n1, static_cast<fftwf_complex*>(spectrum.data),
                                         static_cast<float*>(real.data), FFTW_ESTIMATE | FFTW_DESTROY_INPUT);
        }
    }
    if (plan == nullptr)
    {
        return Status::failure(ErrorCode::ExternalFailure,
                               StatusDetail{.message = "the transform library declined to build a plan over this "
                                                       "geometry",
                                            .operation = operation,
                                            .domain = ErrorDomain::Interop,
                                            .context = {StatusContextField::ofText(ContextKey::Library, "fftw3f")}});
    }
    return FftwRealTransformPlan{plan, direction,
                                 alignment > kFftwDefaultAlignmentBytes ? kFftwDefaultAlignmentBytes : alignment, true};
}

Status FftwRealTransformPlan::executeSplit(void* real, void* spectrum, const char* operation) const
{
    if (m_plan == nullptr || !m_split)
    {
        return fftwFailure(ErrorCode::InvalidState, operation, "this transform plan is not a split plan");
    }
    if (real == nullptr || spectrum == nullptr || !isPointerAligned(real, m_alignmentBytes) ||
        !isPointerAligned(spectrum, m_alignmentBytes))
    {
        return fftwFailure(ErrorCode::InvalidArgument, operation,
                           "a transform buffer must exist and match the alignment its plan was measured at");
    }
    if (m_direction == FftwTransformDirection::Forward)
    {
        fftwf_execute_dft_r2c(static_cast<fftwf_plan>(m_plan), static_cast<float*>(real),
                              static_cast<fftwf_complex*>(spectrum));
    }
    else
    {
        fftwf_execute_dft_c2r(static_cast<fftwf_plan>(m_plan), static_cast<fftwf_complex*>(spectrum),
                              static_cast<float*>(real));
    }
    return Status::success();
}

Status FftwRealTransformPlan::execute(void* base, const char* operation) const
{
    if (m_plan == nullptr || m_split)
    {
        return fftwFailure(ErrorCode::InvalidState, operation, "this transform plan holds no in-place plan");
    }
    if (base == nullptr || !isPointerAligned(base, m_alignmentBytes))
    {
        return fftwFailure(ErrorCode::InvalidArgument, operation,
                           "a transform buffer must exist and match the alignment its plan was measured at");
    }
    if (m_direction == FftwTransformDirection::Forward)
    {
        fftwf_execute_dft_r2c(static_cast<fftwf_plan>(m_plan), static_cast<float*>(base),
                              static_cast<fftwf_complex*>(base));
    }
    else
    {
        fftwf_execute_dft_c2r(static_cast<fftwf_plan>(m_plan), static_cast<fftwf_complex*>(base),
                              static_cast<float*>(base));
    }
    return Status::success();
}

} // namespace an::fftw_detail

namespace an::dependency_detail
{

void reportFftw(DependencyVersion& entry) noexcept
{
    entry.name = "fftw3";
    entry.linkage = DependencyLinkage::SharedLibrary;

    // No compiled version: FFTW publishes none. Its headers declare the transforms and nothing about
    // the release they came from, so the only version there is is the one the loaded library carries,
    // and it carries it as an exported string - `fftw-3.3.11-sse2-avx3` and so on - which the bounded
    // scan reads the number out of. Reaching that string is why this engine's Windows build declares
    // `FFTW_DLL`: it is the library's own switch for "this consumer links the DLL", and without it
    // the header declares the exported data as if it were in this binary.
    setVersionTextFromBanner(entry.loaded, fftwf_version, nullptr);
}

} // namespace an::dependency_detail
