--[[
    AfterNight 3rd Party Libraries Build Configuration
    
    This script configures the build for 3rd party libraries used by AfterNight:
    - cgmic (G'MIC C++ library)
    
    Supported build systems:
    - Visual Studio 2026 (MSVC/Clang)
    - GNU Make (gmake) for Linux
    - CodeLite for Linux
]]

local common = require "afternight-common"

workspace "AfterNight-3rdparty"

    configurations(common.Configs.configurations)
    platforms(common.Configs.platforms)
    
    cppdialect(common.Configs.cppdialect)
    cdialect(common.Configs.cdialect)
    
    startproject "cgmic"
    
    local qt = premake.extensions.qt
    
    filter "platforms:x64"
        architecture "x86_64"
        
    -- Build output locations for different build systems (3rdparty subdirectories)
    filter {"action:codelite","system:linux"}
        location(common.ProjectDirs.codelite.linux .. "/3rdparty")
        basedir(common.ProjectDirs.codelite.linux .. "/3rdparty")
        toolset "clang"
    
    filter {"action:gmake","system:linux"}
        location(common.ProjectDirs.gmake.linux .. "/3rdparty")
        basedir(common.ProjectDirs.gmake.linux .. "/3rdparty")
        toolset "clang"
        
    filter "action:vs*"
        if _OPTIONS["cc"] == "msc" then
            location(common.ProjectDirs.vs2026.msc .. "/3rdparty")
            basedir(common.ProjectDirs.vs2026.msc .. "/3rdparty")
            toolset "msc"
        elseif _OPTIONS["cc"] == "clang" then
            location(common.ProjectDirs.vs2026.clang .. "/3rdparty")
            basedir(common.ProjectDirs.vs2026.clang .. "/3rdparty")
            toolset "clang"
        end
    
    filter {"system:windows"}
        defines { "WIN64" }
        
    filter {"system:windows","action:vs*"}
        systemversion "latest"
    
    -- The definition of the libcgmic shared library
    project "cgmic"
        
        kind "SharedLib"
        language "C++"
        
        ------------- Source code -------------
        files { "../../3rdparty/src/gmic/CImg.h" , "../../3rdparty/src/gmic/gmic.h" , "../../3rdparty/src/gmic/gmic_libc.h", "../../3rdparty/src/gmic/gmic_libc.cpp", "../../3rdparty/src/gmic/gmic.cpp" }
        
        vectorextensions "AVX2"
       
        ------------- MSVC -------------
        filter {"action:vs*"}
            includedirs { "../../3rdparty/src/gmic","../../vcpkg/installed/x64-windows/include","../../vcpkg/installed/x64-windows-release/include" }
            objdir  "../../3rdparty/obj/libcgmic/msvc" 
            disablewarnings { "4251" }
            
            if _OPTIONS["cc"] == "msc" then
                buildoptions { "/utf-8", "/Zc:__cplusplus", "/bigobj" }
            elseif _OPTIONS["cc"] == "clang" then
                buildoptions { "-Wno-unknown-pragmas" }
            end
            
        -- The third-party static libraries are built once and consumed by both Windows toolchains. They
        -- are archives with the Microsoft ABI, which clang-cl matches, so a second copy would be a long
        -- build for no difference. That is why this output tree is not named after a toolchain the way
        -- the engine's is.
        filter {"action:vs*", "configurations:Debug"}
            targetdir "../../3rdparty/bin/msvc/debug"
            implibdir "../../3rdparty/lib/msvc/debug"
            libdirs { "../../deps/lib/msvc/common", "../../vcpkg/installed/x64-windows/debug/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            links { "tiffd","jpeg","libpng16d","fftw3","zd" }
            multiprocessorcompile "Off"
        filter {"action:vs*", "configurations:Release"}
            targetdir "../../3rdparty/bin/msvc/release"
            implibdir "../../3rdparty/lib/msvc/release"
            libdirs { "../../deps/lib/msvc/common", "../../vcpkg/installed/x64-windows/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            links { "tiff","jpeg","libpng16","fftw3","z" }
            multiprocessorcompile "Off"
        
        ------------- Clang (Linux) -------------
        filter {"system:linux"}
            includedirs { "../../3rdparty/src/gmic" }
            buildoptions { "-fPIC",  "-Wno-unknown-pragmas" }
            objdir  "../../3rdparty/obj/libcgmic/clang" 
        
        ------------- GCC Codelite -------------
        filter {"system:linux","action:codelite", "configurations:Debug"}
            targetdir "../../3rdparty/bin/clang/debug"
            implibdir "../../3rdparty/lib/clang/debug"
            linkoptions  {"-ltiff;-ljpeg;-lpng16;-lz;-lfftw3;-lfftw3_threads;"}
        
        filter {"system:linux","action:codelite", "configurations:Release"}
            targetdir "../../3rdparty/bin/clang/release"
            implibdir "../../3rdparty/lib/clang/release"
            linkoptions  {"-ltiff;-ljpeg;-lpng16;-lz;-lfftw3;-lfftw3_threads;"}
        
        ------------- GCC gmake -------------
        filter {"system:linux","action:gmake", "configurations:Debug"}
            targetdir "../../3rdparty/bin/clang/debug"
            implibdir "../../3rdparty/lib/clang/debug"
            linkoptions  {"-ltiff -ljpeg -lpng16 -lz -lfftw3 -lfftw3_threads;"}
            
        filter {"system:linux","action:gmake", "configurations:Release"}
            targetdir "../../3rdparty/bin/clang/release"
            implibdir "../../3rdparty/lib/clang/release"
            linkoptions  {"-ltiff -ljpeg -lpng16 -lz -lfftw3 -lfftw3_threads;"}
            
        ------------- Common Linux-------------
        filter {"system:linux","configurations:Debug"}
            defines { "_DEBUG","DEBUG", "FFTW_DLL", "linux", "__linux__", "gmic_core", "gmic_is_parallel","gmic_community", "cimg_use_abort", "cimg_use_png", "cimg_use_jpeg", "cimg_use_tiff", "cimg_use_fftw3", "cimg_use_zlib" }
            symbols "On"
            targetname "cgmicd"

        filter {"system:linux","configurations:Release"}
            defines { "NDEBUG","FFTW_DLL", "linux","gmic_core", "__linux__", "gmic_is_parallel","gmic_community", "cimg_use_abort", "cimg_use_png", "cimg_use_jpeg", "cimg_use_tiff", "cimg_use_fftw3", "cimg_use_zlib" }
            symbols "Off"
            optimize "Speed"
            targetname "cgmic"
        
        ------------- Common Windows-------------
        filter {"system:windows","configurations:Debug"}
            defines { "_DEBUG","DEBUG", "FFTW_DLL", "WIN32", "_USRDLL", "_CRT_SECURE_NO_WARNINGS", "gmic_core", "gmic_is_parallel","gmic_community", "cimg_use_abort", "cimg_use_png", "cimg_use_jpeg", "cimg_use_tiff", "cimg_use_fftw3", "cimg_use_zlib" }
            symbols "On"
            targetname "cgmicd"

        filter {"system:windows","configurations:Release"}
            defines { "NDEBUG","FFTW_DLL", "_USRDLL", "WIN32", "_CRT_SECURE_NO_WARNINGS","gmic_core", "gmic_is_parallel","gmic_community", "cimg_use_abort", "cimg_use_png", "cimg_use_jpeg", "cimg_use_tiff", "cimg_use_fftw3", "cimg_use_zlib" }
            symbols "Off"
            optimize "Off"
            targetname "cgmic"
