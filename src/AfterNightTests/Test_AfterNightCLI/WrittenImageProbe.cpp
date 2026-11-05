// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "WrittenImageProbe.h"

#include "OneShotRun.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/Image/RawFrame.h>

#include <gtest/gtest.h>

#include <string>

namespace an::cli::tests
{
namespace
{

std::string utf8Path(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

} // namespace

std::optional<WrittenImage> probeWritten(const std::filesystem::path& path)
{
    an::consumer::EngineSessionSettings settings;
    settings.maxConcurrency = 2u;
    settings.codecRegistry = [] { return an::cli::createToolCodecRegistry(); };
    an::Result<an::consumer::EngineSession> session = an::consumer::EngineSession::open(settings);
    if (!session.hasValue())
    {
        ADD_FAILURE() << session.status().message();
        return std::nullopt;
    }
    std::optional<WrittenImage> written;
    {
        an::Result<an::consumer::OperationScope> scope = session->tryAdmit(an::consumer::OperationRequest{});
        if (!scope.hasValue())
        {
            ADD_FAILURE() << scope.status().message();
            return std::nullopt;
        }
        const std::string spelled = utf8Path(path);
        const an::Result<an::CodecImageInfo> info =
            an::probeImage(scope->engine(), scope->execution(), an::CodecReadRequest{.path = spelled});
        if (!info.hasValue())
        {
            ADD_FAILURE() << "probing " << spelled << ": " << info.status().message();
            return std::nullopt;
        }
        const std::optional<an::SampleType> resident = info->descriptor.residentSampleType();
        if (!resident.has_value())
        {
            ADD_FAILURE() << spelled << " holds samples outside the eight";
            return std::nullopt;
        }
        written = WrittenImage{*resident, info->descriptor.extent().width(), info->descriptor.extent().height(),
                               info->descriptor.extent().channelCount()};
    }
    EXPECT_TRUE(session->shutdown().ok());
    return written;
}

} // namespace an::cli::tests
