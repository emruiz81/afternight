// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Native loader probe avoids inheriting a Python interpreter's already-loaded dependency DLLs.
#include <iostream>
#include <string>
#if defined(_WIN32)
#include <Windows.h>
#else
#include <dlfcn.h>
#endif
int main(int argc, char** argv)
{
    if (argc != 2)
        return 2;
    const std::string suffix = std::string(argv[1]) == "debug" ? "_d" : "";
#if defined(_WIN32)
    for (const auto& base : {"AfterNightEngine"})
    {
        const auto name = std::string(base) + suffix + ".dll";
        if (!LoadLibraryA(name.c_str()))
        {
            std::cerr << name << ": " << GetLastError();
            return 1;
        }
    }
    const auto d = suffix.empty() ? "" : "d";
    const auto stock = LoadLibraryA((std::string("raw_r") + d + ".dll").c_str());
    const auto privateRaw = LoadLibraryA((std::string("afternight_libraw_r") + d + ".dll").c_str());
    if (!stock || !privateRaw)
        return 1;
    const auto a = GetProcAddress(stock, "libraw_version"), b = GetProcAddress(privateRaw, "libraw_version");
    if (!a || !b || a == b)
        return 1;
#else
    for (const auto& base : {"libAfterNightEngine"})
    {
        const auto name = std::string("./") + base + suffix + ".so";
        if (!dlopen(name.c_str(), RTLD_NOW | RTLD_GLOBAL))
        {
            std::cerr << dlerror();
            return 1;
        }
    }
#endif
    std::cout << "The engine loaded with independent RAW linkage\n";
    return 0;
}
