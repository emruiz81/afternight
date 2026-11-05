// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The runtime report, and the refusals decided before a device exists.
//
// Most of these cases need no GPU: a host without OpenCL is an ordinary answer to what the report asks,
// and a device index past the last one is refused before a context is created. On a host that has one,
// the first case prints what the loader found - the device, its OpenCL version and its driver - which is
// the line a validation record quotes for the hardware a proof ran on.

#include "OpenClTestSupport.h"

#include <AfterNightEngineOpenClBackend/OpenClBackend.h>

#include <AfterNightEngine/Algorithms/Kernel/Device.h>
#include <AfterNightEngine/Algorithms/Kernel/KernelContract.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

namespace
{

[[nodiscard]] std::string lowered(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return text;
}

TEST(OpenClRuntimeReportTest, TheReportDescribesEveryGpuDeviceTheOperatingSystemsLoaderReaches)
{
    const an::OpenClRuntimeReport report = an::openClRuntimeReport();
    std::cout << "OpenCL runtime: " << an::openClRuntimeStateName(report.state) << ", loader "
              << (report.loaderPath.empty() ? std::string("none") : report.loaderPath) << '\n';
    switch (report.state)
    {
    case an::OpenClRuntimeState::LoaderNotInstalled:
        EXPECT_TRUE(report.loaderPath.empty());
        EXPECT_TRUE(report.devices.empty());
        break;
    case an::OpenClRuntimeState::NoGpuDevice:
        EXPECT_FALSE(report.loaderPath.empty());
        EXPECT_TRUE(report.devices.empty());
        break;
    case an::OpenClRuntimeState::GpuDevicesFound:
        EXPECT_FALSE(report.devices.empty());
        // The loader is the one the operating system's directory holds, never one deployed beside the
        // executable - the backend asks for that directory by name.
        EXPECT_NE(lowered(report.loaderPath).find("system32"), std::string::npos) << report.loaderPath;
        EXPECT_TRUE(lowered(report.loaderPath).ends_with("opencl.dll")) << report.loaderPath;
        break;
    }

    for (const an::OpenClGpuDevice& device : report.devices)
    {
        std::cout << "OpenCL GPU: " << device.deviceName << " | " << device.deviceVersion << " | driver "
                  << device.driverVersion << " | platform " << device.platformName << " (" << device.platformVersion
                  << ") | " << device.globalMemoryBytes << " bytes, largest buffer " << device.maximumAllocationBytes
                  << " | double precision " << (device.doublePrecision ? "yes" : "no") << " | unified memory "
                  << (device.hostUnifiedMemory ? "yes" : "no") << '\n';
        EXPECT_FALSE(device.deviceName.empty());
        EXPECT_FALSE(device.platformName.empty());
        EXPECT_TRUE(device.deviceVersion.starts_with("OpenCL ")) << device.deviceVersion;
        EXPECT_FALSE(device.driverVersion.empty());
        EXPECT_GT(device.versionMajor, 0u) << device.deviceVersion;
        EXPECT_GT(device.globalMemoryBytes, 0u);
        EXPECT_GT(device.maximumAllocationBytes, 0u);
        EXPECT_LE(device.maximumAllocationBytes, device.globalMemoryBytes);
        EXPECT_EQ(device.meetsApiFloor,
                  device.versionMajor > 1u || (device.versionMajor == 1u && device.versionMinor >= 2u));
    }
    if (an::test::openClRequired())
    {
        EXPECT_EQ(report.state, an::OpenClRuntimeState::GpuDevicesFound) << "AFTERNIGHT_TEST_REQUIRE_OPENCL=1";
    }

    // Asking again finds the same loader and the same devices.
    const an::OpenClRuntimeReport again = an::openClRuntimeReport();
    EXPECT_EQ(again.state, report.state);
    EXPECT_EQ(again.loaderPath, report.loaderPath);
    EXPECT_EQ(again.devices.size(), report.devices.size());
}

TEST(OpenClRuntimeReportTest, StatesHaveStableNames)
{
    EXPECT_STREQ(an::openClRuntimeStateName(an::OpenClRuntimeState::LoaderNotInstalled), "LoaderNotInstalled");
    EXPECT_STREQ(an::openClRuntimeStateName(an::OpenClRuntimeState::NoGpuDevice), "NoGpuDevice");
    EXPECT_STREQ(an::openClRuntimeStateName(an::OpenClRuntimeState::GpuDevicesFound), "GpuDevicesFound");
    EXPECT_STREQ(an::openClRuntimeStateName(static_cast<an::OpenClRuntimeState>(200u)), "UnknownOpenClRuntimeState");
}

TEST(OpenClRuntimeReportTest, ADeviceIndexPastTheLastIsRefusedWithTheCount)
{
    const an::OpenClRuntimeReport report = an::openClRuntimeReport();
    const auto requested = static_cast<std::uint32_t>(report.devices.size());
    const an::Result<std::shared_ptr<an::DeviceBackend>> made =
        an::createOpenClBackend(an::OpenClBackendOptions{.deviceIndex = requested});
    ASSERT_FALSE(made.hasValue());
    const an::StatusContextField* const library = made.status().findContext(an::ContextKey::Library);
    ASSERT_NE(library, nullptr);
    EXPECT_EQ(library->text, "OpenCL");
    if (report.state != an::OpenClRuntimeState::GpuDevicesFound)
    {
        EXPECT_EQ(made.status().code(), an::ErrorCode::Unsupported) << made.status().message();
        return;
    }
    EXPECT_EQ(made.status().code(), an::ErrorCode::OutOfRange) << made.status().message();
    const an::StatusContextField* const index = made.status().findContext(an::ContextKey::Requested);
    const an::StatusContextField* const count = made.status().findContext(an::ContextKey::Available);
    ASSERT_NE(index, nullptr);
    ASSERT_NE(count, nullptr);
    EXPECT_EQ(index->count, requested);
    EXPECT_EQ(count->count, report.devices.size());
}

TEST(OpenClRuntimeReportTest, AMemoryLimitIsTheDevicesOwnMemoryOrLessAndTheDeviceStatesIt)
{
    AN_OPENCL_DEVICE_OR_SKIP(subject);

    const an::Result<std::shared_ptr<an::DeviceBackend>> refused = an::createOpenClBackend(an::OpenClBackendOptions{
        .deviceIndex = subject.index, .memoryLimitBytes = subject.description.globalMemoryBytes + 1u});
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument) << refused.status().message();
    const an::StatusContextField* const limit = refused.status().findContext(an::ContextKey::Limit);
    ASSERT_NE(limit, nullptr);
    EXPECT_EQ(limit->count, subject.description.globalMemoryBytes);

    constexpr std::uint64_t kLimit = 256u * 1024u * 1024u;
    an::Result<std::shared_ptr<an::DeviceBackend>> limited =
        an::createOpenClBackend(an::OpenClBackendOptions{.deviceIndex = subject.index, .memoryLimitBytes = kLimit});
    ASSERT_TRUE(limited.hasValue()) << limited.status().message();
    const an::Device device = an::test::makeDevice(std::move(*limited));
    ASSERT_NE(device.capabilities(), nullptr);
    EXPECT_EQ(device.capabilities()->memoryLimitBytes, kLimit);
}

TEST(OpenClRuntimeReportTest, TheAffineContractIsTheReferencesNarrowedToWhatTheKernelHonours)
{
    const an::Result<an::KernelContract> contract = an::openClAffineContract();
    ASSERT_TRUE(contract.hasValue()) << contract.status().message();
    EXPECT_EQ(contract->id.operation, an::KernelOperation::PointwiseAffine);
    EXPECT_EQ(contract->capabilities.validity, an::KernelValidityCapability::AllValidOnly);
    EXPECT_EQ(contract->capabilities.cancellation, an::KernelCancellationPolicy::None);
    EXPECT_EQ(contract->capabilities.strides, an::KernelStridePolicy::RowStrided);
    EXPECT_EQ(contract->capabilities.accuracy.metric, an::KernelAccuracyMetric::Exact);
    EXPECT_TRUE(an::kernelContractSupports(*contract, an::kernelPlanFor(*contract)));

    an::KernelPlanKey masked = an::kernelPlanFor(*contract);
    masked.validity = an::ValidityRule::FiniteOnly;
    EXPECT_FALSE(an::kernelContractSupports(*contract, masked));
}

} // namespace
