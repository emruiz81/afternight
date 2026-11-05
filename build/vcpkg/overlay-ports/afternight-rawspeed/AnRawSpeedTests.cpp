// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Compiles the real boundary with the same RawSpeed objects as the shipped library.
// Allocation injection is confined to this executable; no production control or global is added.
#include <cstdlib>
#include <iostream>
#include <new>
#include <filesystem>
#include <fstream>
#include <iterator>
#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#endif
static unsigned allocationCountdown = 0;
static unsigned allocationCount = 0;
void* operator new(std::size_t bytes)
{
    ++allocationCount;
    if (allocationCountdown && --allocationCountdown == 0)
        throw std::bad_alloc();
    if (auto* value = std::malloc(bytes ? bytes : 1))
        return value;
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept
{
    std::free(value);
}
void operator delete(void* value, std::size_t) noexcept
{
    std::free(value);
}
// Nothrow allocations are released by ordinary delete. Replace every unaligned pair so
// sanitizers cannot allocate through their operator new and deallocate through our free.
void* operator new[](std::size_t bytes)
{
    return ::operator new(bytes);
}
void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new(bytes);
    }
    catch (...)
    {
        return nullptr;
    }
}
void* operator new[](std::size_t bytes, const std::nothrow_t& tag) noexcept
{
    return ::operator new(bytes, tag);
}
void operator delete[](void* value) noexcept
{
    std::free(value);
}
void operator delete[](void* value, std::size_t) noexcept
{
    std::free(value);
}
void operator delete(void* value, const std::nothrow_t&) noexcept
{
    std::free(value);
}
void operator delete[](void* value, const std::nothrow_t&) noexcept
{
    std::free(value);
}
#include "AnRawSpeed.cpp"

static void require(bool value, const char* name)
{
    if (!value)
        throw std::runtime_error(name);
}

static void testAllocationForms()
{
    for (const bool array : {false, true})
    {
        for (unsigned form = 0; form < 3; ++form)
        {
            allocationCount = 0;
            auto* value = array ? ::operator new[](32, std::nothrow) : ::operator new(32, std::nothrow);
            require(value && allocationCount == 1, "nothrow allocation bypassed injection");
            if (array)
            {
                if (form == 0)
                    ::operator delete[](value);
                else if (form == 1)
                    ::operator delete[](value, std::size_t(32));
                else
                    ::operator delete[](value, std::nothrow);
            }
            else
            {
                if (form == 0)
                    ::operator delete(value);
                else if (form == 1)
                    ::operator delete(value, std::size_t(32));
                else
                    ::operator delete(value, std::nothrow);
            }
        }
        allocationCountdown = 1;
        auto* value = array ? ::operator new[](32, std::nothrow) : ::operator new(32, std::nothrow);
        const auto remaining = allocationCountdown;
        allocationCountdown = 0;
        require(!value && remaining == 0, "nothrow allocation did not return injected failure");
    }
}

static void testFile(int argc, char** argv)
{
    require(argc == 7 || argc == 8, "file test needs path, family, width, height, filters and optional X-Trans digits");
    // Read outside the injected boundary; only real decoder allocations are failed.
    std::ifstream stream(std::filesystem::u8path(argv[2]), std::ios::binary);
    require(bool(stream), "Cannot open allocation-test fixture");
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(stream), {}};
    AnRsOptions options{};
    options.size = sizeof(options);
    options.version = 1;
    options.family = std::stoul(argv[3]);
    options.width = std::stoul(argv[4]);
    options.height = std::stoul(argv[5]);
    options.filters = std::stoul(argv[6]);
    options.correction = options.family == AnRsNikonNef   ? AnRsNikonIntegerCurve
                         : options.family == AnRsSonyArw2 ? AnRsSonyIntegerCurve
                                                          : AnRsUnmodified;
    if (argc == 8)
    {
        require(std::strlen(argv[7]) == 36, "Expected 36 X-Trans entries");
        for (unsigned i = 0; i < 36; ++i)
            options.xtrans[i] = argv[7][i] - '0';
    }
    AnRsHandle handle = nullptr;
    require(anRsCreate(&options, &handle) == AnRsOk, "Cannot initialize fixture boundary");
    struct Cleanup
    {
        AnRsHandle handle;
        ~Cleanup()
        {
            anRsDestroy(handle);
        }
    } cleanup{handle};
    AnRsResult result{};
    result.size = sizeof(result);
    result.version = 1;
    allocationCount = 0;
    const int status = anRsDecode(handle, bytes.data(), bytes.size(), &result);
    const auto total = allocationCount;
    require(status == AnRsOk && result.pixels, "Baseline fixture decode failed");
    // Standard-library one-time setup can remove a few final allocations on later calls.
    // Sample throughout the stable decoder work, and require each injected refusal to occur.
    for (const unsigned failAt : {1u, 2u, 16u, total / 4, total / 2, total * 3 / 4, total * 7 / 8})
    {
        anRsDestroy(handle);
        cleanup.handle = nullptr;
        require(anRsCreate(&options, &handle) == AnRsOk, "Cannot reinitialize allocation boundary");
        cleanup.handle = handle;
        allocationCount = 0;
        allocationCountdown = failAt;
        const auto failed = anRsDecode(handle, bytes.data(), bytes.size(), &result);
        const auto remaining = allocationCountdown;
        allocationCountdown = 0;
        if (failed != AnRsAllocationFailed)
            std::cerr << "Allocation " << failAt << '/' << total << " returned status " << failed << ", remaining "
                      << remaining << ", total " << allocationCount << '\n';
        require(failed == AnRsAllocationFailed && !result.pixels, "Real decoder OOM was not a terminal typed failure");
    }
    std::cout << "Real decoder allocation injection passed at seven positions of " << total << " allocations\n";
}

int main(int argc, char** argv)
{
#if defined(_WIN32) && defined(_DEBUG)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    try
    {
        testAllocationForms();
        if (argc > 1)
        {
            testFile(argc, argv);
            return 0;
        }
        AnRsOptions options{};
        options.size = sizeof(options);
        options.version = 1;
        options.family = AnRsCanonCr2;
        options.width = 65;
        options.height = 49;
        AnRsHandle handle = reinterpret_cast<void*>(1);
        require(anRsCreate(nullptr, &handle) == AnRsInvalidArgument && !handle, "null options reset handle");
        const auto xmlAllocate = pugi::get_memory_allocation_function();
        const auto xmlFree = pugi::get_memory_deallocation_function();
        pugi::set_memory_management_functions([](size_t) -> void* { return nullptr; }, xmlFree);
        const int xmlStatus = anRsCreate(&options, &handle);
        pugi::set_memory_management_functions(xmlAllocate, xmlFree);
        require(xmlStatus == AnRsAllocationFailed && !handle, "XML allocation failure remains typed");
        for (unsigned allocation = 1; allocation <= 16; ++allocation)
        {
            allocationCountdown = allocation;
            const int status = anRsCreate(&options, &handle);
            allocationCountdown = 0;
            require(status == AnRsAllocationFailed && !handle, "allocation failure remains typed across C ABI");
        }
        options.stop = [](void*, uint32_t, uint64_t) -> int { return 1; };
        require(anRsCreate(&options, &handle) == AnRsCancelled && !handle, "initial cancellation");
        options.stop = [](void*, uint32_t, uint64_t) -> int { throw 7; };
        require(anRsCreate(&options, &handle) == AnRsInternal && !handle, "callback exception cannot cross C ABI");
        options.stop = nullptr;
        require(anRsCreate(&options, &handle) == AnRsOk && handle, "boundary initialization");
        std::array<unsigned char, 32> malformed{};
        AnRsResult result{};
        result.size = sizeof(result);
        result.version = 1;
        result.pixels = reinterpret_cast<const uint16_t*>(1);
        result.version = 99;
        require(anRsDecode(handle, nullptr, 0, &result) == AnRsInvalidArgument && !result.pixels,
                "version mismatch clears a correctly sized result");
        result.size = sizeof(uint32_t);
        result.pixels = reinterpret_cast<const uint16_t*>(1);
        require(anRsDecode(handle, nullptr, 0, &result) == AnRsInvalidArgument && result.pixels,
                "wrong-sized result is not overwritten");
        result.size = sizeof(result);
        require(anRsDecode(handle, nullptr, 0, &result) == AnRsInvalidArgument && !result.pixels,
                "partial result reset");
        require(anRsDecode(handle, malformed.data(), malformed.size(), &result) == AnRsMalformed && !result.pixels,
                "malformed bytes");
        allocationCountdown = 1;
        const int allocationStatus = anRsDecode(handle, malformed.data(), malformed.size(), &result);
        allocationCountdown = 0;
        require(allocationStatus == AnRsAllocationFailed && !result.pixels, "decoder allocation failure");
        anRsDestroy(handle);
        anRsDestroy(nullptr);

        for (unsigned phase = 0; phase < 4; ++phase)
        {
            rawspeed::ColorFilterArray cfa;
            cfa.setCFA({2, 2}, rawspeed::CFA_RED, rawspeed::CFA_GREEN, rawspeed::CFA_GREEN, rawspeed::CFA_BLUE);
            cfa.shiftLeft(phase & 1);
            cfa.shiftDown(phase >> 1);
            options.filters = cfa.getDcrawFilter();
            require(matchesCfa(cfa, options), "every Bayer phase and distinct green slots");
            options.filters ^= 3;
            require(!matchesCfa(cfa, options), "wrong CFA refused");
        }
        // Exercise every 16-bit code through the real non-dithered lookup with odd-width tails.
        // The curve has a black plateau, a gain change, a breakpoint and a saturated endpoint.
        auto raw = rawspeed::RawImage::create(rawspeed::TYPE_USHORT16);
        raw->dim = {257, 256};
        raw->createData();
        std::vector<uint16_t> curve(65536);
        for (unsigned i = 0; i < curve.size(); ++i)
            curve[i] = i < 32 ? 0 : i < 4096 ? (i - 32) * 2 : (std::min)(65535u, i + 4096u);
        raw->setTable(curve, false);
        for (unsigned y = 0; y < 256; ++y)
        {
            auto* row = reinterpret_cast<uint16_t*>(raw->getDataUncropped(0, y));
            for (unsigned x = 0; x < 257; ++x)
                row[x] = uint16_t(y * 257 + x);
        }
        raw->sixteenBitLookup();
        for (unsigned y = 0; y < 256; ++y)
        {
            const auto* row = reinterpret_cast<const uint16_t*>(raw->getDataUncropped(0, y));
            for (unsigned x = 0; x < 257; ++x)
                require(row[x] == curve[uint16_t(y * 257 + x)], "exact lookup endpoints/breakpoints/tails");
        }
        unsigned checkpoints = 0;
        raw->anStopState = &checkpoints;
        raw->anStop = [](void* value) { return ++*static_cast<unsigned*>(value) == 8 ? 1 : 0; };
        // RawSpeed's worker captures row exceptions as errors. Both are observed by the wrapper.
        raw->sixteenBitLookup();
        require(checkpoints == 8 && !raw->getErrors().empty(), "mid-correction cancellation");
        auto refused = rawspeed::RawImage::create(rawspeed::TYPE_USHORT16);
        refused->dim = {1024, 1024};
        refused->anExpectedWidth = 65;
        refused->anExpectedHeight = 49;
        bool geometryRefused = false;
        try
        {
            refused->createData();
        }
        catch (const rawspeed::AnGeometryMismatch&)
        {
            geometryRefused = true;
        }
        require(geometryRefused, "geometry refused before allocation");
        std::cout << "RawSpeed C boundary, allocation, CFA, exact curve and cancellation tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
    }
    catch (...)
    {
        std::cerr << "Unexpected test exception\n";
    }
    return 1;
}
