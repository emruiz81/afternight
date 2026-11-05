// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The consumer fault scenarios' session over the injecting resource and their accepted codes.

#include "Test_ConsumerFaults/ConsumerFaultSupport.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/IO/TiffCodec.h>

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string>

namespace an::tests::consumerfaults
{
namespace
{

/// The settings every consumer scenario's session is opened with, but the resource.
[[nodiscard]] an::consumer::EngineSessionSettings sessionSettings(std::uint32_t width)
{
    an::consumer::EngineSessionSettings settings;
    settings.maxConcurrency = width;
    settings.codecRegistry = []() -> an::Result<std::shared_ptr<const an::CodecRegistry>> {
        an::Result<std::shared_ptr<const an::ImageCodec>> fits = an::createFitsCodec();
        if (!fits.hasValue())
        {
            return fits.status();
        }
        an::Result<std::shared_ptr<const an::ImageCodec>> tiff = an::createTiffCodec();
        if (!tiff.hasValue())
        {
            return tiff.status();
        }
        const std::array<std::shared_ptr<const an::ImageCodec>, 2u> codecs{*fits, *tiff};
        return an::CodecRegistry::create(codecs);
    };
    return settings;
}

} // namespace

an::Result<an::consumer::EngineSession> openSession(an::faults::RunContext& run, const std::uint32_t width)
{
    an::consumer::EngineSessionSettings settings = sessionSettings(width);
    settings.memoryResource = &run.memory;
    return an::consumer::EngineSession::open(settings);
}

an::Result<an::consumer::EngineSession> openSetupSession()
{
    return an::consumer::EngineSession::open(sessionSettings(2u));
}

void acceptRefusals(an::faults::Sweep& sweep)
{
    sweep.accept(an::ErrorCode::AllocationFailed);
    sweep.accept(an::ErrorCode::MemoryBudgetExceeded);
}

void acceptStops(an::faults::Sweep& sweep)
{
    sweep.accept(an::ErrorCode::Cancelled);
}

QString qtPath(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

} // namespace an::tests::consumerfaults
