--[[
    AfterNight Main Project Build Configuration
    
    This script configures the build for the main AfterNight projects:
    - AfterNightEngine (shared library)
    - AfterNightCLI (console application on the consumer runtime and the engine)
    - AfterNightApp (Qt-based GUI application)
    
    Supported build systems:
    - Visual Studio 2026 (MSVC/Clang)
    - GNU Make (gmake) for Linux
    - CodeLite for Linux
]]

local common = require "afternight-common"
local targets = require "afternight-targets"
-- Included by default. Only an explicit 0 omits the private dependencies for rollback builds.
-- Normal runtime selection belongs to CodecReadRequest.rawDecoderPreference; no regeneration is needed.
local rawspeedEnabled = os.getenv("AFTERNIGHT_RAWSPEED") ~= "0"
-- The optional Python adapter is built by default and is never linked by the engine or by a
-- product target. Only an explicit 0 generates it without the Python runtime, which is the
-- configuration where it reports itself absent and refuses every call by name.
local pythonAdapterEnabled = os.getenv("AFTERNIGHT_PYTHON") ~= "0"
-- The optional G'MIC adapter follows the same rule, with its own switch: only an explicit 0
-- generates it without the library, which is the configuration where it reports itself absent.
local gmicAdapterEnabled = os.getenv("AFTERNIGHT_GMIC") ~= "0"
-- The OpenCL proof backend has the opposite default: it is generated only when AFTERNIGHT_OPENCL=1
-- asks for it by name, and a tree generated with nothing set contains no GPU project at all. It is a
-- non-shipping instrument for proving the device seam on a GPU, and a contributor who never installed
-- a GPU SDK builds everything else.
-- build/scripts/tools/check_gpu_backend_optionality.py reads this line in both workspace files.
local openClBackendEnabled = os.getenv("AFTERNIGHT_OPENCL") == "1"

-- Premake writes project files and never deletes one, so a tree regenerated without the opt-in would
-- keep the backend's project from an earlier generation that had it: a file describing a GPU target the
-- current solution does not build, which check_build_graph.py and the engine's build script both read.
-- A generation without the opt-in removes it.
local function removeOpenClBackendProjectFiles(name, subdirectory)
    if not (_ACTION and _ACTION:find("^vs")) then
        return
    end
    for _, tree in ipairs({ common.ProjectDirs.vs2026.msc, common.ProjectDirs.vs2026.clang }) do
        for _, suffix in ipairs({ ".vcxproj", ".vcxproj.filters", ".vcxproj.user" }) do
            os.remove(tree .. subdirectory .. "/" .. name .. suffix)
        end
    end
end

workspace "AfterNight"

    configurations(common.Configs.configurations)
    platforms(common.Configs.platforms)
    
    cppdialect(common.Configs.cppdialect)
    cdialect(common.Configs.cdialect)
    common.applyWarningsAsErrors()
    
    startproject "AfterNightApp"
    
    local qt = premake.extensions.qt
    filter "platforms:x64"
        architecture "x86_64"

    filter {"system:linux"}
        includedirs {
            "../../vcpkg/installed/x64-linux/include",
            "../../vcpkg/installed/x64-linux-release/include"
        }

    filter {"action:vs*"}
        includedirs {
            "../../vcpkg/installed/x64-windows/include",
            "../../vcpkg/installed/x64-windows-release/include"
        }

    filter {}
        
    -- Build output locations for different build systems
    filter {"action:codelite","system:linux"}
        location(common.ProjectDirs.codelite.linux)
        basedir(common.ProjectDirs.codelite.linux)
        toolset "clang"
    
    filter {"action:gmake","system:linux"}
        location(common.ProjectDirs.gmake.linux)
        basedir(common.ProjectDirs.gmake.linux)
        toolset "clang"

    filter "action:vs*"
        if _OPTIONS["cc"] == "msc" then
            location(common.ProjectDirs.vs2026.msc)
            basedir(common.ProjectDirs.vs2026.msc)
            toolset "msc"
        elseif _OPTIONS["cc"] == "clang" then
            location(common.ProjectDirs.vs2026.clang)
            basedir(common.ProjectDirs.vs2026.clang)
            toolset "clang"
        end
        
    filter {"system:windows"}
        defines { "WIN64" }
        
    filter {"system:windows","action:vs*"}
        systemversion "latest"

    -- AfterNightPlatform: the process services the application owns rather than an engine - the logger
    -- with its LOG_* macros, the logging configuration, the Crashpad crash handler - and the few text,
    -- path and memory helpers the product still needs.
    --
    -- It is a shared library so that the logger and the crash handler exist once per process, whichever
    -- module logs. It links neither the engine nor Qt, so the command-line tool and the runtime backend can
    -- load it, and the Lite host and the protocol library never do; build/scripts/tools/check_build_graph.py
    -- enforces all three. spdlog and fmt are header-only here, so Crashpad's client is its only link input.
    project "AfterNightPlatform"

        kind "SharedLib"
        language "C++"
        defines { "AFTERNIGHTPLATFORM_EXPORTS" }

        files {
            "../../src/AfterNightPlatform/**.h",
            "../../src/AfterNightPlatform/**.cpp"
        }

        vpaths {
            ["public"] = { "../../src/AfterNightPlatform/include/AfterNightPlatform/**" },
            ["private"] = { "../../src/AfterNightPlatform/src/**" }
        }

        -- One root: the published headers, written <AfterNightPlatform/Name.h>. The implementation
        -- includes nothing of its own by another path.
        includedirs { "../../src/AfterNightPlatform/include" }

        ------------- MSVC -------------
        filter {"action:vs*"}
            includedirs { "../../vcpkg/installed/x64-windows/include", "../../vcpkg/installed/x64-windows-release/include" }
            objdir ("../../obj/AfterNightPlatform/" .. common.WindowsOutputDir)
            disablewarnings { "4251" }
            multiprocessorcompile "On"
            buildoptions { "/utf-8", "/Zc:__cplusplus", "/FS" }

        filter {"action:vs*", "configurations:Debug"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            implibdir ("../../lib/" .. common.WindowsOutputDir)
            libdirs { "../../vcpkg/installed/x64-windows/debug/lib", "../../vcpkg/installed/x64-windows/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            links { "vcpkg_crashpad_client.lib", "vcpkg_crashpad_util.lib", "vcpkg_crashpad_base.lib", "vcpkg_crashpad_client_common.lib" }

        filter {"action:vs*", "configurations:Release"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            implibdir ("../../lib/" .. common.WindowsOutputDir)
            libdirs { "../../vcpkg/installed/x64-windows/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            links { "vcpkg_crashpad_client.lib", "vcpkg_crashpad_util.lib", "vcpkg_crashpad_base.lib", "vcpkg_crashpad_client_common.lib" }

        ------------- Clang (Linux) -------------
        -- Hidden visibility, so what AFTERNIGHTPLATFORM_API marks is all the library exports of its own.
        -- Crashpad's client is linked whole, as the engine that carried the handler before linked it, and
        -- whole it references zlib, which resolves to the same static vcpkg archive the engine takes.
        filter {"system:linux"}
            includedirs { "../../vcpkg/installed/x64-linux/include", "../../vcpkg/installed/x64-linux/include/crashpad", "../../vcpkg/installed/x64-linux-release/include", "../../vcpkg/installed/x64-linux-release/include/crashpad" }
            buildoptions { "-fPIC", "-fvisibility=hidden" }
            objdir "../../obj/AfterNightPlatform/clang"
            linkoptions { "-Wl,--whole-archive", "-lvcpkg_crashpad_client", "-lvcpkg_crashpad_util", "-lvcpkg_crashpad_base", "-lvcpkg_crashpad_client_common", "-Wl,--no-whole-archive", "-lz", "-Wl,-z,noexecstack", "-Wl,--no-warn-execstack" }

        filter {"system:linux", "configurations:Debug"}
            targetdir "../../bin/clang/debug"
            libdirs { "../../vcpkg/installed/x64-linux/debug/lib", "../../vcpkg/installed/x64-linux-release/lib" }

        filter {"system:linux", "configurations:Release"}
            targetdir "../../bin/clang/release"
            libdirs { "../../vcpkg/installed/x64-linux/lib", "../../vcpkg/installed/x64-linux-release/lib" }
            postbuildcommands {
                "objcopy --only-keep-debug \"%{cfg.buildtarget.relpath}\" \"%{cfg.buildtarget.relpath}.debug\"",
                "objcopy --strip-debug --strip-unneeded \"%{cfg.buildtarget.relpath}\"",
                "objcopy --add-gnu-debuglink=\"%{cfg.buildtarget.relpath}.debug\" \"%{cfg.buildtarget.relpath}\""
            }

        ------------- Common -------------
        filter {"system:linux", "configurations:Debug"}
            defines { "_DEBUG", "DEBUG", "linux", "__linux__" }
            symbols "On"
            targetname "AfterNightPlatform_d"

        filter {"system:linux", "configurations:Release"}
            defines { "NDEBUG", "linux", "__linux__" }
            symbols "On"
            optimize "Speed"
            targetname "AfterNightPlatform"

        filter {"system:windows", "configurations:Debug"}
            defines { "_DEBUG", "DEBUG", "WIN32" }
            symbols "On"
            targetname "AfterNightPlatform_d"

        filter {"system:windows", "configurations:Release"}
            defines { "NDEBUG", "WIN32" }
            symbols "On"
            optimize "Speed"
            targetname "AfterNightPlatform"

        filter {}

        -- The instrumentation this tree was generated for, if any: the consumer closures are instrumented with the
        -- engine they run on, so a sanitizer run over a consumer suite covers the libraries it exercises. Last in the
        -- project, because it overrides link-time code generation and incremental linking. Crashpad's client is linked
        -- from static libraries built without the sanitizer, and Microsoft's library refuses to link annotated and
        -- unannotated containers into one binary, so this library's standard containers go unannotated; its heap and
        -- stack are instrumented as every other library's are.
        common.applyEngineSanitizer({ unannotatedContainers = true })
        filter {}

    -- The AfterNightEngine shared library: the engine. It compiles no file out of its own tree, and
    -- build/scripts/tools/check_build_graph.py holds what links it and what it links.
    project "AfterNightEngine"

        kind "SharedLib"
        language "C++"

        ------------- Source code -------------
        files { "../../src/AfterNightEngine/**.h" , "../../src/AfterNightEngine/**.hpp" ,"../../src/AfterNightEngine/**.inl" , "../../src/AfterNightEngine/**.cpp" }

        -- No precompiled header, deliberately: one would put every header it holds into every unit,
        -- and the include graph check_include_layers.py reads from the include lines would not be the
        -- one the compiler sees.

        vectorextensions "AVX2"

        ------------- Virtual folders -------------
        -- One virtual folder per layer, so the generated solution shows the same physical
        -- structure the include rules are written against rather than a flat file list.
        vpaths {
            ["public/Algorithms"] = { "../../src/AfterNightEngine/include/AfterNightEngine/Algorithms/**" },
            ["public/Core"] = { "../../src/AfterNightEngine/include/AfterNightEngine/Core/**" },
            ["public/IO"] = { "../../src/AfterNightEngine/include/AfterNightEngine/IO/**" },
            ["public/Image"] = { "../../src/AfterNightEngine/include/AfterNightEngine/Image/**" },
            ["public/Interop"] = { "../../src/AfterNightEngine/include/AfterNightEngine/Interop/**" },
            ["public/Runtime"] = { "../../src/AfterNightEngine/include/AfterNightEngine/Runtime/**" },
            ["private/algorithms"] = { "../../src/AfterNightEngine/src/algorithms/**" },
            ["private/backends/cpu"] = { "../../src/AfterNightEngine/src/backends/cpu/**" },
            ["private/backends/gpu"] = { "../../src/AfterNightEngine/src/backends/gpu/**" },
            ["private/core"] = { "../../src/AfterNightEngine/src/core/**" },
            ["private/image"] = { "../../src/AfterNightEngine/src/image/**" },
            ["private/interop"] = { "../../src/AfterNightEngine/src/interop/**" },
            ["private/io"] = { "../../src/AfterNightEngine/src/io/**" },
            ["private/kernels/reference"] = { "../../src/AfterNightEngine/src/kernels/reference/**" },
            ["private/memory"] = { "../../src/AfterNightEngine/src/memory/**" },
            ["private/ported"] = { "../../src/AfterNightEngine/src/ported/**" },
            ["private/runtime"] = { "../../src/AfterNightEngine/src/runtime/**" }
        }

        -- Two include roots, and the difference between them is the difference between the
        -- published surface and the implementation. A header reached through the public root is
        -- written <AfterNightEngine/Area/Name.h> and is part of the contract; a header reached
        -- through the private root is written "layer/Name.h" and is not. Only this project gets the
        -- private root - the engine's test projects deliberately do not, so a test cannot reach past
        -- the published surface without the build graph saying so.
        includedirs { "../../src/AfterNightEngine/include", "../../src/AfterNightEngine/src" }

        ------------- MSVC -------------
        filter {"action:vs*"}
            objdir  ("../../obj/AfterNightEngine/" .. common.WindowsOutputDir)

            -- oneTBB, FFTW's single-precision transforms through src/interop/fftw, and GSL's
            -- Student-t distribution through src/interop/gsl. Each external header stays in its
            -- declared adapter or backend layer. build/scripts/tools/check_include_layers.py
            -- reads the include lines to enforce what an include directory cannot restrict.
            -- OpenFITS and OpenXISF, behind the FITS and XISF codecs, are each built from their published
            -- release into an install tree of their own (build/scripts/msvc/_build_openfits.bat and
            -- _build_openxisf.bat) and reached as <openfits/...> and <openxisf/...> from those trees, which
            -- are named first.
            includedirs { "../../openfits/build-msvc-install/include", "../../openxisf/build-msvc-install/include",
                          "../../vcpkg/installed/x64-windows/include" }
            if rawspeedEnabled then
                includedirs { "../../vcpkg/installed/x64-windows-raw/include" }
                defines { "AFTERNIGHT_HAS_RAWSPEED" }
            end

            -- No implicit fused multiply-add, on either compiler. A vector kernel and the scalar
            -- reference it is judged against must round in the same places, and contraction is the
            -- one thing that silently changes where a rounding happens: /arch:AVX2 makes the
            -- instruction available in every translation unit, so a compiler that contracts would
            -- fuse the oracle as readily as the kernel. Contraction is written into a kernel with
            -- MulAdd and into its reference with std::fma, and nowhere else.
            --
            -- Microsoft's compiler has no negative form of the switch: /fp:precise already does not
            -- contract, /fp:contract turns it on, and /fp:contract- is rejected as an unknown
            -- option. So the rule on that compiler is that neither /fp:contract nor /fp:fast is
            -- ever added here. clang-cl already defaults to contraction off in its Microsoft
            -- compatibility mode, and says so explicitly for the same reason its Linux build does.
            if _OPTIONS["cc"] == "msc" then
                buildoptions { "/utf-8", "/Zc:__cplusplus", "/FS", "/arch:AVX2" }
            elseif _OPTIONS["cc"] == "clang" then
                -- Visual Studio emits /fp:precise for the project. clang-cl translates that to
                -- -ffp-model=precise and calls the narrower contraction choice below an override;
                -- the override is intentional and is the numeric contract, so silence that one
                -- driver diagnostic while keeping every source warning fatal.
                buildoptions { "/arch:AVX2", "/clang:-ffp-contract=off", "-Wno-overriding-option" }
            end

        -- The debug library only exists in a full triplet, and that is the whole of why there
        -- is no debug job: continuous integration provisions x64-windows-release, which
        -- contains no tbb12_debug.lib to find. Debug stays a local configuration, built on a
        -- machine that installed the full triplet.
        filter {"action:vs*", "configurations:Debug"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            implibdir ("../../lib/" .. common.WindowsOutputDir)
            libdirs { "../../openfits/build-msvc-install/lib", "../../openxisf/build-msvc-install/lib",
                      "../../vcpkg/installed/x64-windows/debug/lib" }
            if rawspeedEnabled then
                libdirs { "../../vcpkg/installed/x64-windows-raw/debug/lib" }
            end
            links { "tbb12_debug.lib", "fftw3f.lib", "gsld.lib", "gslcblasd.lib", "openfitsd.lib",
                    "openxisfd.lib", rawspeedEnabled and "afternight_libraw_rd.lib" or "raw_rd.lib",
                    "tiffd.lib", "libpng16d.lib", "jpeg.lib", "exiv2.lib",
                    "avformat.lib", "avcodec.lib", "avutil.lib", "swscale.lib" }
            multiprocessorcompile "On"
            linktimeoptimization "Off"

        -- Both triplet roots, in that order, like every other project here. A development
        -- machine installs the full x64-windows triplet and finds the library in the first;
        -- continuous integration deliberately provisions x64-windows-release only, to halve
        -- the dependency build, and finds it in the second. Listing one root links on the
        -- machine that wrote it and fails with LNK1181 on the machine that did not, which is
        -- exactly what happened the first time this project had an external library to link.
        filter {"action:vs*", "configurations:Release"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            implibdir ("../../lib/" .. common.WindowsOutputDir)
            libdirs { "../../openfits/build-msvc-install/lib",
                      "../../openxisf/build-msvc-install/lib",
                      "../../vcpkg/installed/x64-windows/lib",
                      "../../vcpkg/installed/x64-windows-release/lib" }
            if rawspeedEnabled then
                libdirs { "../../vcpkg/installed/x64-windows-raw/lib" }
            end
            links { "tbb12.lib", "fftw3f.lib", "gsl.lib", "gslcblas.lib", "openfits.lib",
                    "openxisf.lib", rawspeedEnabled and "afternight_libraw_r.lib" or "raw_r.lib",
                    "tiff.lib", "libpng16.lib", "jpeg.lib", "exiv2.lib",
                    "avformat.lib", "avcodec.lib", "avutil.lib", "swscale.lib" }
            multiprocessorcompile "On"
            linktimeoptimization "On"

        ------------- Clang (Linux) -------------
        filter {"system:linux"}
            objdir  "../../obj/AfterNightEngine/clang"
            -- -ffp-contract=off for the reason given on the Microsoft side, and it is load-bearing
            -- here rather than declaratory: this compiler contracts within a statement by default,
            -- and the one translation unit below is compiled with -mfma, which is what makes the
            -- instruction reachable. Without this flag that file's scalar tail loops would fuse
            -- while the vector bodies above them, written as separate Mul and Add, would not - so a
            -- row's last few outputs would round differently from the rest of the row and from the
            -- reference. -mavx2 alone does not imply -mfma, which is why only that file is exposed.
            buildoptions { "-fPIC", "-mavx2", "-ffp-contract=off" }
            -- The export macro this engine's public headers carry is
            -- __attribute__((visibility("default"))) on this compiler, which says nothing unless
            -- the default is hidden. Without this flag every symbol with external linkage reaches
            -- the dynamic symbol table, so the binary interface is the whole implementation rather
            -- than the published surface, and it differs from the Microsoft build, where only a
            -- decorated declaration reaches the export table. With it the two agree, and
            -- build/scripts/tools/check_public_api.py --binaries reads the same answer on both.
            buildoptions { "-fvisibility=hidden" }
            -- oneTBB comes from the distribution here rather than from vcpkg, as it does for every
            -- other target that links it on Linux, so a process holds one copy of one library.
            --
            -- The vector abstraction is the exception in the other direction: it is taken from
            -- vcpkg on both platforms so that both compile the same port version, rather than from
            -- a distribution package whose version moves with the distribution. It is header-only
            -- for the way this engine uses it - per-target compilation and this engine's own
            -- dispatch table, never the library's own runtime dispatch - so there is nothing to
            -- link and nothing to deploy. Which headers may be reached through this root is read
            -- out of the include lines by build/scripts/tools/check_include_layers.py, exactly as
            -- it is on Windows; an include directory cannot restrict it.
            includedirs { "../../openfits/build-clang-install/include", "../../openxisf/build-clang-install/include",
                          "../../vcpkg/installed/x64-linux/include" }
            if rawspeedEnabled then
                includedirs { "../../vcpkg/installed/x64-linux-raw/include" }
                defines { "AFTERNIGHT_HAS_RAWSPEED" }
            end
            -- FFTW's single-precision library joins oneTBB here for the reason the MSVC comment
            -- gives, and comes from the distribution as oneTBB does. GSL's Student-t inverse
            -- likewise uses the installed library through its own interop adapter. The
            -- configuration filters below name vcpkg's library directory for each configuration;
            -- it holds no oneTBB, FFTW or GSL, so it cannot displace the shared copies above.
            --
            -- The three raster codecs take the distribution's libtiff and libpng. The metadata
            -- adapter instead takes vcpkg's position-independent static Exiv2 and its Expat, Brotli
            -- and inih closure: Ubuntu's Expat can lag fixes for hostile XMP input, and a hostile
            -- TIFF or JPEG reaches it. The static libraries are ordered immediately after Exiv2 and
            -- the exclusion below keeps every one out of this library's dynamic symbol table.
            -- The shared raster objects have their own libjpeg, and this engine links a static one
            -- for LibRaw; the same exclusion makes libtiff's calls reach the copy it was built
            -- against rather than the copy inside this engine, whose ABI is a different one.
            --
            -- The video source uses the FFmpeg release vcpkg's overlay pins, which carries the fixes for
            -- the demuxers and decoders a video file reaches (build/vcpkg/overlay-ports/ffmpeg). Linux's
            -- triplet builds it as position-independent static archives, so the engine carries that
            -- release rather than the distribution's shared copy. The exclusion below keeps its symbols
            -- private, and only src/io/sequences/ffmpeg reaches its headers.
            linkoptions { "-ltbb", "-lfftw3f", "-lgsl", "-lgslcblas", "-ltiff", "-lpng", "-lexiv2",
                          "-lINIReader", "-linih", "-lexpat", "-lbrotlidec", "-lbrotlicommon", "-lm", "-lz",
                          "-lavformat", "-lavcodec", "-lvpx", "-lm", "-lx264", "-lpthread", "-lx265",
                          "-lstdc++", "-lrt", "-ldl", "-lswresample", "-lswscale", "-lavutil", "-lm",
                          "-pthread", "-Wl,-Bsymbolic", "-Wl,--exclude-libs,ALL",
                          "-Wl,-rpath,'$$ORIGIN'" }

        -- OpenFITS and OpenXISF, behind the FITS and XISF codecs, are the shared libraries
        -- build/scripts/linux/_build_openfits.sh and _build_openxisf.sh build from their published
        -- releases, each carrying the zlib - and OpenXISF the pugixml, LZ4 and Zstandard - it was built
        -- against and exporting only its own API. Each is linked by path, so no other copy a -l could
        -- find is taken instead, and the engine records its soname. The engine's build copies them
        -- beside the engine (the post-build commands below): GNU ld finds the libraries a linked shared
        -- library needs through that library's runpath, which is $ORIGIN, so every target that links
        -- the engine finds them there, and so does a suite that runs.
        local openfitsLinuxLibdir = path.getabsolute(path.join(_MAIN_SCRIPT_DIR, "../../openfits/build-clang-install/lib"))
        local openxisfLinuxLibdir = path.getabsolute(path.join(_MAIN_SCRIPT_DIR, "../../openxisf/build-clang-install/lib"))
        filter {"system:linux", "configurations:Debug"}
            libdirs { "../../vcpkg/installed/x64-linux/debug/lib" }
            linkoptions { openfitsLinuxLibdir .. "/libopenfitsd.so", openxisfLinuxLibdir .. "/libopenxisfd.so" }
            postbuildcommands { "cp -fL \"" .. openfitsLinuxLibdir .. "/libopenfitsd.so.1.0\" \"%{cfg.buildtarget.directory}/\"",
                                "cp -fL \"" .. openxisfLinuxLibdir .. "/libopenxisfd.so.1.1\" \"%{cfg.buildtarget.directory}/\"" }
            -- x64-linux LibRaw is static: its decoder dependencies follow it on the link line.
            if rawspeedEnabled then
                linkoptions { "../../vcpkg/installed/x64-linux-raw/debug/lib/libafternight_libraw_rd.a",
                    "../../vcpkg/installed/x64-linux-raw/debug/lib/libafternight_rawspeedd.a",
                    "../../vcpkg/installed/x64-linux-raw/debug/lib/libpugixml.a",
                    "../../vcpkg/installed/x64-linux-raw/debug/lib/liblcms2.a",
                    "../../vcpkg/installed/x64-linux-raw/debug/lib/libjpeg.a",
                    "../../vcpkg/installed/x64-linux-raw/debug/lib/libz.a", "-lm" }
            else
                linkoptions { "-lraw_r", "-ljasperd", "-llcms2", "-lz", "-ljpeg", "-lm" }
            end

        filter {"system:linux", "configurations:Release"}
            libdirs { "../../vcpkg/installed/x64-linux/lib" }
            linkoptions { openfitsLinuxLibdir .. "/libopenfits.so", openxisfLinuxLibdir .. "/libopenxisf.so" }
            postbuildcommands { "cp -fL \"" .. openfitsLinuxLibdir .. "/libopenfits.so.1.0\" \"%{cfg.buildtarget.directory}/\"",
                                "cp -fL \"" .. openxisfLinuxLibdir .. "/libopenxisf.so.1.1\" \"%{cfg.buildtarget.directory}/\"" }
            if rawspeedEnabled then
                linkoptions { "../../vcpkg/installed/x64-linux-raw/lib/libafternight_libraw_r.a",
                    "../../vcpkg/installed/x64-linux-raw/lib/libafternight_rawspeed.a",
                    "../../vcpkg/installed/x64-linux-raw/lib/libpugixml.a",
                    "../../vcpkg/installed/x64-linux-raw/lib/liblcms2.a",
                    "../../vcpkg/installed/x64-linux-raw/lib/libjpeg.a",
                    "../../vcpkg/installed/x64-linux-raw/lib/libz.a", "-lm" }
            else
                linkoptions { "-lraw_r", "-ljasper", "-llcms2", "-lz", "-ljpeg", "-lm" }
            end

        -- LibRaw inline methods compiled in our RAW units must remain private too.
        filter {"system:linux", "files:**/io/raw/**.cpp"}
            buildoptions { "-fvisibility=hidden" }

        filter {"system:linux"}

        -- The SIMD translation units in this engine are compiled with instruction-set flags of
        -- their own, and the flags are a completion of the packaged floor rather than a raise
        -- above it.
        -- /arch:AVX2 implies AES and PCLMUL on MSVC and does not on Clang, and the vector
        -- abstraction's AVX2 target sits on a baseline that requires them - so without these it
        -- silently selects a 128-bit target and every kernel in the file is quietly narrower than
        -- the floor. The file asserts the target it ended up with, so a lost flag here is a
        -- compile error rather than a slow binary. Nothing above the floor is named on any command
        -- line: the AVX-512 bodies are per-target functions the abstraction emits inside this same
        -- object, which is what keeps a raised instruction set out of every other translation unit.
        filter {"system:linux", "files:**/SimdKernelTables.cpp"}
            buildoptions { "-mfma", "-mf16c", "-mbmi", "-mbmi2", "-mlzcnt", "-mmovbe", "-maes", "-mpclmul" }

        -- The same completion under clang-cl, where the floor is stated as /arch:AVX2 rather
        -- than -mavx2 and therefore already implies six of those eight. The two that remain are
        -- the ones this compiler does not take from the switch and MSVC does. Without them the
        -- abstraction has no usable baseline at all and stops the build, which is how the gap
        -- was found: it is a compile error rather than a narrower binary.
        filter {"action:vs*", "toolset:clang", "files:**/SimdKernelTables.cpp"}
            buildoptions { "-maes", "-mpclmul" }

        -- Every other translation unit the vector abstraction compiles once per packaged
        -- instruction set needs the same completion, whatever domain it belongs to. A unit left off
        -- this list still builds under MSVC, whose /arch:AVX2 implies the two flags clang does not,
        -- and stops the Linux build outright with no usable baseline - which is how PsfFitCpu.cpp
        -- was found missing from it.
        local perTargetVectorSources = {
            "**/CalibrationCpu.cpp",
            "**/ColorAdjustmentCpu.cpp",
            "**/DemosaicAmazeCpu.cpp",
            "**/DemosaicBilinearCpu.cpp",
            "**/DemosaicFastCpu.cpp",
            "**/DemosaicLuminanceAhdCpu.cpp",
            "**/DemosaicMarkesteijnOneCpu.cpp",
            "**/DemosaicMarkesteijnThreeCpu.cpp",
            "**/DemosaicRcdCpu.cpp",
            "**/DemosaicXTransFastCpu.cpp",
            "**/LocalNormalizationCpu.cpp",
            "**/NormalizationCpu.cpp",
            "**/PresentationCpu.cpp",
            "**/PsfFitCpu.cpp",
            "**/StackAccumulatorCpu.cpp",
            "**/StackMethodCpu.cpp",
            "**/StackRejectionCpu.cpp",
            "**/StackSourceCpu.cpp",
            "**/StretchCpu.cpp",
            "**/WarpCpu.cpp",
        }
        for _, source in ipairs(perTargetVectorSources) do
            filter {"system:linux", "files:" .. source}
                buildoptions { "-mfma", "-mf16c", "-mbmi", "-mbmi2", "-mlzcnt", "-mmovbe", "-maes", "-mpclmul" }

            filter {"action:vs*", "toolset:clang", "files:" .. source}
                buildoptions { "-maes", "-mpclmul" }
        end

        filter {"system:linux","action:codelite", "configurations:Debug"}
            targetdir "../../bin/clang/debug"

        filter {"system:linux","action:codelite", "configurations:Release"}
            targetdir "../../bin/clang/release"

        filter {"system:linux","action:gmake", "configurations:Debug"}
            targetdir "../../bin/clang/debug"

        filter {"system:linux","action:gmake", "configurations:Release"}
            targetdir "../../bin/clang/release"

        ------------- Common Linux-------------
        filter {"system:linux","configurations:Debug"}
            defines { "_DEBUG","DEBUG", "linux", "__linux__", "AFTERNIGHTENGINE_EXPORTS" }
            symbols "On"
            targetname "AfterNightEngine_d"

        filter {"system:linux","configurations:Release"}
            defines { "NDEBUG","linux", "__linux__", "AFTERNIGHTENGINE_EXPORTS" }
            symbols "On"
            optimize "Speed"
            targetname "AfterNightEngine"
            postbuildcommands {
                "objcopy --only-keep-debug \"%{cfg.buildtarget.relpath}\" \"%{cfg.buildtarget.relpath}.debug\"",
                "objcopy --strip-debug --strip-unneeded \"%{cfg.buildtarget.relpath}\"",
                "objcopy --add-gnu-debuglink=\"%{cfg.buildtarget.relpath}.debug\" \"%{cfg.buildtarget.relpath}\""
            }

        ------------- Common Windows-------------
        -- FFTW_DLL is that library's own switch for "this consumer links the DLL rather than
        -- containing it", and without it its header declares everything it exports as if it were in
        -- this binary. The functions survive that anyway, because a linker resolves a call through
        -- the import library's thunk; the exported *data* does not, and FFTW publishes its version
        -- as exported data and in no other way. So the engine's dependency report can name the FFTW
        -- it actually loaded only in a build that states this. It is a Windows-only spelling: the
        -- library's own header ignores the switch everywhere else.
        filter {"system:windows","configurations:Debug"}
            defines { "_DEBUG","DEBUG","AFTERNIGHTENGINE_EXPORTS", "WIN32", "FFTW_DLL" }
            symbols "On"
            targetname "AfterNightEngine_d"

        filter {"system:windows","configurations:Release"}
            defines { "NDEBUG","AFTERNIGHTENGINE_EXPORTS", "WIN32", "FFTW_DLL" }
            symbols "On"
            optimize "Speed"
            targetname "AfterNightEngine"

        -- The instrumentation this tree was generated for, if any. Last in the project because it
        -- overrides link-time code generation and incremental linking, which the address sanitizer
        -- cannot be combined with on Microsoft's toolchain.
        common.applyEngineSanitizer()

        filter {}


    -- The AfterNightEnginePythonAdapter shared library: the optional Python seam.
    --
    -- The engine links no Python runtime and no G'MIC library, and a consumer that wants either
    -- acquires the corresponding adapter explicitly. That is a build property before it is an API
    -- property, and this project is where it is stated:
    --
    --   - the adapter links the engine's published surface and the Python runtime. The engine links
    --     neither the adapter nor Python, so nothing that uses the engine acquires a Python
    --     dependency by linking it, and the engine's own build stays free of a Python toolchain;
    --   - nothing links this project except its own suite and the consumers that asked for G'MIC -
    --     the processes library, whose G'MIC processes run through it, and the suite that compiles
    --     those processes in isolation. It is optional in the sense a consumer cares about: absent
    --     unless something asked for it; and
    --   - the project exists in every generated tree whether or not the Python runtime is present,
    --     because the absent case is an ordinary outcome this engine reports rather than a
    --     configuration that cannot be built. AFTERNIGHT_PYTHON=0 regenerates it without the
    --     runtime, and it then reports itself absent and refuses every call by name.
    --
    -- build/scripts/tools/check_build_graph.py fails the graph when the first two stop holding, and
    -- build/scripts/tools/check_installer_payload.py fails the packagers when either artifact would
    -- reach an installer payload.
    project "AfterNightEnginePythonAdapter"

        kind "SharedLib"
        language "C++"

        dependson { "AfterNightEngine" }

        ------------- Source code -------------
        files { "../../src/AfterNightEnginePythonAdapter/**.h",
                "../../src/AfterNightEnginePythonAdapter/**.cpp" }

        vpaths {
            ["public"] = { "../../src/AfterNightEnginePythonAdapter/include/**" },
            ["private"] = { "../../src/AfterNightEnginePythonAdapter/src/**" }
        }

        -- Three include roots and no fourth. This adapter's own public and private headers, and the
        -- engine's *public* surface - not the engine's private src root, because an adapter outside
        -- the engine binary reaching an engine implementation header would be linking against
        -- something the engine does not export.
        includedirs { "../../src/AfterNightEnginePythonAdapter/include",
                      "../../src/AfterNightEnginePythonAdapter/src",
                      "../../src/AfterNightEngine/include" }

        ------------- MSVC -------------
        filter {"action:vs*"}
            objdir  ("../../obj/AfterNightEnginePythonAdapter/" .. common.WindowsOutputDir)
            if pythonAdapterEnabled then
                includedirs { "../../python/python/tools_msvc/include" }
                defines { "AFTERNIGHT_HAS_PYTHON" }
            end
            if _OPTIONS["cc"] == "msc" then
                buildoptions { "/utf-8", "/Zc:__cplusplus", "/FS" }
            elseif _OPTIONS["cc"] == "clang" then
                -- The engine's contraction policy, for the reason its own project states it: this
                -- adapter compiles the engine's public headers, and nothing here may round in a
                -- different place than the engine does.
                buildoptions { "/clang:-ffp-contract=off", "-Wno-overriding-option" }
            end

        filter {"action:vs*", "configurations:Debug"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            implibdir ("../../lib/" .. common.WindowsOutputDir)
            libdirs { ("../../lib/" .. common.WindowsOutputDir), "../../python/python/tools_msvc/libs" }
            links { "AfterNightEngine_d.lib" }
            if pythonAdapterEnabled then
                -- The release import library in both configurations, deliberately: the interpreter
                -- unit undefines _DEBUG around Python.h so a debug build of this adapter links the
                -- release runtime rather than requiring a debug build of CPython nobody ships.
                links { "python314.lib" }
            end
            multiprocessorcompile "On"

        filter {"action:vs*", "configurations:Release"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            implibdir ("../../lib/" .. common.WindowsOutputDir)
            libdirs { ("../../lib/" .. common.WindowsOutputDir), "../../python/python/tools_msvc/libs" }
            links { "AfterNightEngine.lib" }
            if pythonAdapterEnabled then
                links { "python314.lib" }
            end
            multiprocessorcompile "On"

        ------------- Clang (Linux) -------------
        filter {"system:linux"}
            objdir  "../../obj/AfterNightEnginePythonAdapter/clang"
            buildoptions { "-fPIC", "-ffp-contract=off" }
            -- The export macro this engine's public headers carry is
            -- __attribute__((visibility("default"))) on this compiler, which says nothing unless
            -- the default is hidden. Without this flag every symbol with external linkage reaches
            -- the dynamic symbol table, so the binary interface is the whole implementation rather
            -- than the published surface, and it differs from the Microsoft build, where only a
            -- decorated declaration reaches the export table. With it the two agree, and
            -- build/scripts/tools/check_public_api.py --binaries reads the same answer on both.
            buildoptions { "-fvisibility=hidden" }
            if pythonAdapterEnabled then
                includedirs { "../../python/python/tools_linux/include/python3.14" }
                defines { "AFTERNIGHT_HAS_PYTHON" }
            end

        filter {"system:linux", "configurations:Debug"}
            targetdir "../../bin/clang/debug"
            libdirs { "../../bin/clang/debug", "../../python/python/tools_linux/lib" }
            linkoptions { "-lAfterNightEngine_d", "-Wl,-rpath,'$$ORIGIN'" }
            if pythonAdapterEnabled then
                linkoptions { "-lpython3.14" }
            end

        filter {"system:linux", "configurations:Release"}
            targetdir "../../bin/clang/release"
            libdirs { "../../bin/clang/release", "../../python/python/tools_linux/lib" }
            linkoptions { "-lAfterNightEngine", "-Wl,-rpath,'$$ORIGIN'" }
            if pythonAdapterEnabled then
                linkoptions { "-lpython3.14" }
            end
            postbuildcommands {
                "objcopy --only-keep-debug \"%{cfg.buildtarget.relpath}\" \"%{cfg.buildtarget.relpath}.debug\"",
                "objcopy --strip-debug --strip-unneeded \"%{cfg.buildtarget.relpath}\"",
                "objcopy --add-gnu-debuglink=\"%{cfg.buildtarget.relpath}.debug\" \"%{cfg.buildtarget.relpath}\""
            }

        ------------- Common Linux-------------
        filter {"system:linux","configurations:Debug"}
            defines { "_DEBUG","DEBUG", "linux", "__linux__", "AFTERNIGHTENGINEPYTHONADAPTER_EXPORTS" }
            symbols "On"
            targetname "AfterNightEnginePythonAdapter_d"

        filter {"system:linux","configurations:Release"}
            defines { "NDEBUG","linux", "__linux__", "AFTERNIGHTENGINEPYTHONADAPTER_EXPORTS" }
            symbols "On"
            optimize "Speed"
            targetname "AfterNightEnginePythonAdapter"

        ------------- Common Windows-------------
        filter {"system:windows","configurations:Debug"}
            defines { "_DEBUG","DEBUG","AFTERNIGHTENGINEPYTHONADAPTER_EXPORTS", "WIN32" }
            symbols "On"
            targetname "AfterNightEnginePythonAdapter_d"

        filter {"system:windows","configurations:Release"}
            defines { "NDEBUG","AFTERNIGHTENGINEPYTHONADAPTER_EXPORTS", "WIN32" }
            symbols "On"
            optimize "Speed"
            targetname "AfterNightEnginePythonAdapter"

        -- The instrumentation this tree was generated for, if any. Last in the project because it
        -- overrides link-time code generation and incremental linking, which the address sanitizer
        -- cannot be combined with on Microsoft's toolchain.
        common.applyEngineSanitizer()

        filter {}


    -- The AfterNightEngineGmicAdapter shared library: the optional G'MIC seam.
    --
    -- The second of the two adapters, built to the same rule as the first: the engine links no
    -- G'MIC library, and a consumer that wants one acquires this adapter explicitly. What that
    -- rule is, in a build rather than in prose:
    --
    --   - the adapter links the engine's published surface and the G'MIC library. The engine links
    --     neither the adapter nor G'MIC, so nothing that uses the engine acquires a G'MIC
    --     dependency by linking it;
    --   - nothing links this project except its own suite. It is optional in the sense a consumer
    --     cares about: absent unless something asked for it; and
    --   - the project exists in every generated tree whether or not the G'MIC library is present,
    --     because the absent case is an ordinary outcome this engine reports rather than a
    --     configuration that cannot be built. AFTERNIGHT_GMIC=0 regenerates it without the
    --     library, and it then reports itself absent and refuses every call by name.
    --
    -- Only the library's C bridge is compiled against, which is why the include root is the
    -- 3rdparty source directory and not the interpreter's own header directory: `gmic_libc.h` is
    -- two structures and three functions, where `gmic.h` would pull CImg into this binary.
    --
    -- build/scripts/tools/check_build_graph.py fails the graph when the first two stop holding, and
    -- build/scripts/tools/check_installer_payload.py fails the packagers when either artifact would
    -- reach an installer payload.
    project "AfterNightEngineGmicAdapter"

        kind "SharedLib"
        language "C++"

        dependson { "AfterNightEngine" }

        ------------- Source code -------------
        files { "../../src/AfterNightEngineGmicAdapter/**.h",
                "../../src/AfterNightEngineGmicAdapter/**.cpp" }

        vpaths {
            ["public"] = { "../../src/AfterNightEngineGmicAdapter/include/**" },
            ["private"] = { "../../src/AfterNightEngineGmicAdapter/src/**" }
        }

        -- Three include roots and no fourth, for the reason the Python adapter states: this
        -- adapter's own public and private headers, and the engine's *public* surface - not the
        -- engine's private src root, because an adapter outside the engine binary reaching an
        -- engine implementation header would be linking against something the engine does not
        -- export.
        includedirs { "../../src/AfterNightEngineGmicAdapter/include",
                      "../../src/AfterNightEngineGmicAdapter/src",
                      "../../src/AfterNightEngine/include" }

        ------------- MSVC -------------
        filter {"action:vs*"}
            objdir  ("../../obj/AfterNightEngineGmicAdapter/" .. common.WindowsOutputDir)
            if gmicAdapterEnabled then
                includedirs { "../../3rdparty/src" }
                defines { "AFTERNIGHT_HAS_GMIC" }
            end
            if _OPTIONS["cc"] == "msc" then
                buildoptions { "/utf-8", "/Zc:__cplusplus", "/FS" }
            elseif _OPTIONS["cc"] == "clang" then
                -- The engine's contraction policy, for the reason its own project states it: this
                -- adapter compiles the engine's public headers, and nothing here may round in a
                -- different place than the engine does.
                buildoptions { "/clang:-ffp-contract=off", "-Wno-overriding-option" }
            end

        filter {"action:vs*", "configurations:Debug"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            implibdir ("../../lib/" .. common.WindowsOutputDir)
            libdirs { ("../../lib/" .. common.WindowsOutputDir), "../../3rdparty/lib/msvc/debug" }
            links { "AfterNightEngine_d.lib" }
            if gmicAdapterEnabled then
                links { "cgmicd.lib" }
            end
            multiprocessorcompile "On"

        filter {"action:vs*", "configurations:Release"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            implibdir ("../../lib/" .. common.WindowsOutputDir)
            libdirs { ("../../lib/" .. common.WindowsOutputDir), "../../3rdparty/lib/msvc/release" }
            links { "AfterNightEngine.lib" }
            if gmicAdapterEnabled then
                links { "cgmic.lib" }
            end
            multiprocessorcompile "On"

        ------------- Clang (Linux) -------------
        filter {"system:linux"}
            objdir  "../../obj/AfterNightEngineGmicAdapter/clang"
            buildoptions { "-fPIC", "-ffp-contract=off" }
            -- The export macro this engine's public headers carry is
            -- __attribute__((visibility("default"))) on this compiler, which says nothing unless
            -- the default is hidden. Without this flag every symbol with external linkage reaches
            -- the dynamic symbol table, so the binary interface is the whole implementation rather
            -- than the published surface, and it differs from the Microsoft build, where only a
            -- decorated declaration reaches the export table. With it the two agree, and
            -- build/scripts/tools/check_public_api.py --binaries reads the same answer on both.
            buildoptions { "-fvisibility=hidden" }
            if gmicAdapterEnabled then
                includedirs { "../../3rdparty/src" }
                defines { "AFTERNIGHT_HAS_GMIC" }
            end

        filter {"system:linux", "configurations:Debug"}
            targetdir "../../bin/clang/debug"
            libdirs { "../../bin/clang/debug", "../../3rdparty/bin/clang/debug" }
            linkoptions { "-lAfterNightEngine_d", "-Wl,-rpath,'$$ORIGIN'" }
            if gmicAdapterEnabled then
                linkoptions { "-lcgmicd" }
            end

        filter {"system:linux", "configurations:Release"}
            targetdir "../../bin/clang/release"
            libdirs { "../../bin/clang/release", "../../3rdparty/bin/clang/release" }
            linkoptions { "-lAfterNightEngine", "-Wl,-rpath,'$$ORIGIN'" }
            if gmicAdapterEnabled then
                linkoptions { "-lcgmic" }
            end
            postbuildcommands {
                "objcopy --only-keep-debug \"%{cfg.buildtarget.relpath}\" \"%{cfg.buildtarget.relpath}.debug\"",
                "objcopy --strip-debug --strip-unneeded \"%{cfg.buildtarget.relpath}\"",
                "objcopy --add-gnu-debuglink=\"%{cfg.buildtarget.relpath}.debug\" \"%{cfg.buildtarget.relpath}\""
            }

        ------------- Common Linux-------------
        filter {"system:linux","configurations:Debug"}
            defines { "_DEBUG","DEBUG", "linux", "__linux__", "AFTERNIGHTENGINEGMICADAPTER_EXPORTS" }
            symbols "On"
            targetname "AfterNightEngineGmicAdapter_d"

        filter {"system:linux","configurations:Release"}
            defines { "NDEBUG","linux", "__linux__", "AFTERNIGHTENGINEGMICADAPTER_EXPORTS" }
            symbols "On"
            optimize "Speed"
            targetname "AfterNightEngineGmicAdapter"

        ------------- Common Windows-------------
        filter {"system:windows","configurations:Debug"}
            defines { "_DEBUG","DEBUG","AFTERNIGHTENGINEGMICADAPTER_EXPORTS", "WIN32" }
            symbols "On"
            targetname "AfterNightEngineGmicAdapter_d"

        filter {"system:windows","configurations:Release"}
            defines { "NDEBUG","AFTERNIGHTENGINEGMICADAPTER_EXPORTS", "WIN32" }
            symbols "On"
            optimize "Speed"
            targetname "AfterNightEngineGmicAdapter"

        -- The instrumentation this tree was generated for, if any. Last in the project because it
        -- overrides link-time code generation and incremental linking, which the address sanitizer
        -- cannot be combined with on Microsoft's toolchain.
        common.applyEngineSanitizer()

        filter {}


    -- AfterNightConsumerRuntime: what a consumer of the engine owns that the engine does not - the
    -- session that owns one engine context and admits every operation run in it, the operation scope that
    -- owns one execution, the image asset that owns one image behind read and write leases, and the
    -- allowances for the consumer memory the engine does not count.
    --
    -- A shared library, so the application, the command-line tool, the full host and the dynamically loaded
    -- runtime backend share one definition of those types and one copy of their state. It links the
    -- engine's published surface and the platform library, and nothing else of AfterNight's: no Qt, no
    -- Python, no product library - and neither the engine nor the platform library links it.
    -- build/scripts/tools/check_build_graph.py enforces all of that.
    project "AfterNightConsumerRuntime"

        kind "SharedLib"
        language "C++"

        dependson { "AfterNightEngine", "AfterNightPlatform" }

        files {
            "../../src/AfterNightConsumerRuntime/**.h",
            "../../src/AfterNightConsumerRuntime/**.cpp"
        }

        vpaths {
            ["public"] = { "../../src/AfterNightConsumerRuntime/include/AfterNightConsumerRuntime/**" },
            ["private"] = { "../../src/AfterNightConsumerRuntime/src/**" }
        }

        -- Its published headers, written <AfterNightConsumerRuntime/Name.h>, and the published surfaces of
        -- the engine and the platform library. Its private headers sit beside the units that include them.
        includedirs { "../../src/AfterNightConsumerRuntime/include",
                      "../../src/AfterNightEngine/include",
                      "../../src/AfterNightPlatform/include" }

        ------------- MSVC -------------
        filter {"action:vs*"}
            -- The logger's spdlog and fmt, which the platform library's header names.
            includedirs { "../../vcpkg/installed/x64-windows/include", "../../vcpkg/installed/x64-windows-release/include" }
            objdir ("../../obj/AfterNightConsumerRuntime/" .. common.WindowsOutputDir)
            disablewarnings { "4251" }
            multiprocessorcompile "On"
            if _OPTIONS["cc"] == "msc" then
                buildoptions { "/utf-8", "/Zc:__cplusplus", "/FS" }
            elseif _OPTIONS["cc"] == "clang" then
                -- The engine's contraction policy: this library compiles the engine's public headers.
                buildoptions { "/clang:-ffp-contract=off", "-Wno-overriding-option" }
            end

        filter {"action:vs*", "configurations:Debug"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            implibdir ("../../lib/" .. common.WindowsOutputDir)
            libdirs { ("../../lib/" .. common.WindowsOutputDir) }
            links { "AfterNightEngine_d.lib", "AfterNightPlatform_d.lib" }

        filter {"action:vs*", "configurations:Release"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            implibdir ("../../lib/" .. common.WindowsOutputDir)
            libdirs { ("../../lib/" .. common.WindowsOutputDir) }
            links { "AfterNightEngine.lib", "AfterNightPlatform.lib" }

        ------------- Clang (Linux) -------------
        -- Hidden visibility, so what AFTERNIGHTCONSUMERRUNTIME_API marks is all the library exports of its own.
        filter {"system:linux"}
            includedirs { "../../vcpkg/installed/x64-linux/include", "../../vcpkg/installed/x64-linux-release/include" }
            buildoptions { "-fPIC", "-fvisibility=hidden", "-ffp-contract=off" }
            objdir "../../obj/AfterNightConsumerRuntime/clang"

        filter {"system:linux", "configurations:Debug"}
            targetdir "../../bin/clang/debug"
            libdirs { "../../bin/clang/debug" }
            linkoptions { "-lAfterNightEngine_d", "-lAfterNightPlatform_d", "-Wl,-rpath,'$$ORIGIN'", "-Wl,-z,noexecstack" }

        filter {"system:linux", "configurations:Release"}
            targetdir "../../bin/clang/release"
            libdirs { "../../bin/clang/release" }
            linkoptions { "-lAfterNightEngine", "-lAfterNightPlatform", "-Wl,-rpath,'$$ORIGIN'", "-Wl,-z,noexecstack" }
            postbuildcommands {
                "objcopy --only-keep-debug \"%{cfg.buildtarget.relpath}\" \"%{cfg.buildtarget.relpath}.debug\"",
                "objcopy --strip-debug --strip-unneeded \"%{cfg.buildtarget.relpath}\"",
                "objcopy --add-gnu-debuglink=\"%{cfg.buildtarget.relpath}.debug\" \"%{cfg.buildtarget.relpath}\""
            }

        ------------- Common -------------
        filter {"system:linux", "configurations:Debug"}
            defines { "_DEBUG", "DEBUG", "linux", "__linux__", "AFTERNIGHTCONSUMERRUNTIME_EXPORTS" }
            symbols "On"
            targetname "AfterNightConsumerRuntime_d"

        filter {"system:linux", "configurations:Release"}
            defines { "NDEBUG", "linux", "__linux__", "AFTERNIGHTCONSUMERRUNTIME_EXPORTS" }
            symbols "On"
            optimize "Speed"
            targetname "AfterNightConsumerRuntime"

        filter {"system:windows", "configurations:Debug"}
            defines { "_DEBUG", "DEBUG", "WIN32", "AFTERNIGHTCONSUMERRUNTIME_EXPORTS" }
            symbols "On"
            targetname "AfterNightConsumerRuntime_d"

        filter {"system:windows", "configurations:Release"}
            defines { "NDEBUG", "WIN32", "AFTERNIGHTCONSUMERRUNTIME_EXPORTS" }
            symbols "On"
            optimize "Speed"
            targetname "AfterNightConsumerRuntime"

        filter {}

        -- The instrumentation this tree was generated for, if any: the consumer closures are instrumented with the
        -- engine they run on, so a sanitizer run over a consumer suite covers the libraries it exercises. Last in the
        -- project, because it overrides link-time code generation and incremental linking.
        common.applyEngineSanitizer()
        filter {}


    -- The AfterNightEngineOpenClBackend shared library: the opt-in OpenCL proof backend.
    --
    -- A GPU behind the device seam the engine publishes (Algorithms/Kernel/DeviceBackend.h), built to
    -- prove that seam on an NVIDIA host and for nothing else. It is non-shipping and off by default, and
    -- not leaving a dependency behind is the reason for every line of it:
    --
    --   - it is generated only when AFTERNIGHT_OPENCL=1, and only for Windows. A tree generated with
    --     nothing set contains no GPU project, so a contributor with no GPU SDK builds, tests, deploys
    --     and packages everything else;
    --   - it links the engine's published surface, the engine links nothing of it, and nothing but its
    --     own suite links it. check_build_graph.py fails the graph otherwise, and fails any other target
    --     that reaches the OpenCL SDK;
    --   - the SDK is the Khronos headers and the ICD loader's import library, which
    --     install_opencl_backend_deps.bat installs into a vcpkg triplet of their own, so no default
    --     project's include or library directory reaches them; and
    --   - OpenCL.dll is delay-loaded. The backend loads the loader the graphics driver installed from
    --     the system directory before it calls into it, so on a machine without one it starts, reports
    --     that, and refuses to create a backend rather than failing to load - and the loader vcpkg built
    --     is never deployed.
    if openClBackendEnabled and os.istarget("windows") then
        local openClSdk = "../../vcpkg/installed/x64-windows-opencl"
        if not os.isfile(openClSdk .. "/include/CL/cl.h") then
            error("AFTERNIGHT_OPENCL=1 needs the OpenCL headers; run build/scripts/msvc/install_opencl_backend_deps.bat first")
        end

    project "AfterNightEngineOpenClBackend"

        kind "SharedLib"
        language "C++"

        dependson { "AfterNightEngine" }

        ------------- Source code -------------
        files { "../../src/AfterNightEngineOpenClBackend/**.h",
                "../../src/AfterNightEngineOpenClBackend/**.cpp" }

        vpaths {
            ["public"] = { "../../src/AfterNightEngineOpenClBackend/include/**" },
            ["private"] = { "../../src/AfterNightEngineOpenClBackend/src/**" }
        }

        -- Four include roots: this backend's public and private headers, the engine's *public* surface
        -- for the reason the adapters state, and the OpenCL headers. Only the private sources may
        -- include the last one, which check_include_layers.py enforces where an include directory
        -- cannot.
        includedirs { "../../src/AfterNightEngineOpenClBackend/include",
                      "../../src/AfterNightEngineOpenClBackend/src",
                      "../../src/AfterNightEngine/include",
                      openClSdk .. "/include" }

        ------------- MSVC -------------
        filter {"action:vs*"}
            objdir  ("../../obj/AfterNightEngineOpenClBackend/" .. common.WindowsOutputDir)
            -- One import library for both configurations: the loader has one binary interface, and the
            -- debug and release import libraries both name OpenCL.dll.
            libdirs { ("../../lib/" .. common.WindowsOutputDir), openClSdk .. "/lib" }
            links { "OpenCL.lib", "delayimp.lib" }
            linkoptions { "/DELAYLOAD:OpenCL.dll" }
            if _OPTIONS["cc"] == "msc" then
                buildoptions { "/utf-8", "/Zc:__cplusplus", "/FS" }
            elseif _OPTIONS["cc"] == "clang" then
                -- The engine's contraction policy, for the reason its own project states it.
                buildoptions { "/clang:-ffp-contract=off", "-Wno-overriding-option" }
            end

        filter {"action:vs*", "configurations:Debug"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            implibdir ("../../lib/" .. common.WindowsOutputDir)
            links { "AfterNightEngine_d.lib" }
            multiprocessorcompile "On"

        filter {"action:vs*", "configurations:Release"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            implibdir ("../../lib/" .. common.WindowsOutputDir)
            links { "AfterNightEngine.lib" }
            multiprocessorcompile "On"

        ------------- Common Windows-------------
        filter {"system:windows","configurations:Debug"}
            defines { "_DEBUG","DEBUG","AFTERNIGHTENGINEOPENCLBACKEND_EXPORTS", "WIN32" }
            symbols "On"
            targetname "AfterNightEngineOpenClBackend_d"

        filter {"system:windows","configurations:Release"}
            defines { "NDEBUG","AFTERNIGHTENGINEOPENCLBACKEND_EXPORTS", "WIN32" }
            symbols "On"
            optimize "Speed"
            targetname "AfterNightEngineOpenClBackend"

        filter {}
    elseif openClBackendEnabled then
        print("AFTERNIGHT_OPENCL=1 generates nothing for this target: the OpenCL proof backend is Windows-only")
    else
        removeOpenClBackendProjectFiles("AfterNightEngineOpenClBackend", "")
    end


    -- The CLI interface
    --
    -- AfterNightCLI: one image read, stretched for display and written, over the consumer runtime and the engine.
    -- It links the runtime, the engine and the platform library and nothing else of AfterNight's - no Qt, no Python, no
    -- G'MIC and no crash-handler client it never calls - which check_build_graph.py holds it to.
    project "AfterNightCLI"

        kind "ConsoleApp"
        language "C++"
        -- Declared long-path aware on Windows, as every executable of the product is.
        common.applyApplicationManifest()

        dependson { "AfterNightConsumerRuntime", "AfterNightEngine", "AfterNightPlatform" }

        ------------- Source code -------------
        files { "../../src/AfterNightCLI/**.h", "../../src/AfterNightCLI/**.cpp" }

        vpaths {
            ["cli"] = { "../../src/AfterNightCLI/*.*" }
               }

        -- The published surfaces of the runtime, the engine and the platform library, and nothing else first-party.
        includedirs { "../../src/AfterNightConsumerRuntime/include",
                      "../../src/AfterNightEngine/include",
                      "../../src/AfterNightPlatform/include" }

        ------------- MSVC -------------
        filter {"action:vs*"}
            -- The logger's spdlog and fmt, which the platform library's header names.
            includedirs { "../../vcpkg/installed/x64-windows/include", "../../vcpkg/installed/x64-windows-release/include" }
            disablewarnings { "4251" }
            if _OPTIONS["cc"] == "msc" then
                buildoptions { "/utf-8", "/Zc:__cplusplus", "/FS" }
            elseif _OPTIONS["cc"] == "clang" then
                -- The engine's contraction policy: the tool compiles the engine's public headers.
                buildoptions { "/clang:-ffp-contract=off", "-Wno-overriding-option" }
            end

        filter {"action:vs*", "configurations:Debug"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            debugdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            libdirs { ("../../lib/" .. common.WindowsOutputDir) }
            objdir  ("../../obj/AfterNightCLI/" .. common.WindowsOutputDir)
            links { "AfterNightConsumerRuntime_d.lib", "AfterNightEngine_d.lib", "AfterNightPlatform_d.lib" }

        filter {"action:vs*", "configurations:Release"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            debugdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            libdirs { ("../../lib/" .. common.WindowsOutputDir) }
            objdir  ("../../obj/AfterNightCLI/" .. common.WindowsOutputDir)
            links { "AfterNightConsumerRuntime.lib", "AfterNightEngine.lib", "AfterNightPlatform.lib" }

        ------------- Clang (Linux) -------------
        filter {"system:linux"}
            includedirs { "../../vcpkg/installed/x64-linux/include", "../../vcpkg/installed/x64-linux-release/include" }
            buildoptions { "-ffp-contract=off" }
            objdir  "../../obj/AfterNightCLI/clang"

        filter {"system:linux", "configurations:Debug"}
            targetdir "../../bin/clang/debug"
            debugdir "../../bin/clang/debug"
            libdirs { "../../bin/clang/debug" }
            linkoptions { "-lAfterNightConsumerRuntime_d", "-lAfterNightEngine_d", "-lAfterNightPlatform_d", "-Wl,-z,noexecstack", "-Wl,-rpath,'$$ORIGIN'" }

        filter {"system:linux", "configurations:Release"}
            targetdir "../../bin/clang/release"
            debugdir "../../bin/clang/release"
            libdirs { "../../bin/clang/release" }
            linkoptions { "-lAfterNightConsumerRuntime", "-lAfterNightEngine", "-lAfterNightPlatform", "-Wl,-z,noexecstack", "-Wl,-rpath,'$$ORIGIN'" }
            postbuildcommands {
                "objcopy --only-keep-debug \"%{cfg.buildtarget.relpath}\" \"%{cfg.buildtarget.relpath}.debug\"",
                "objcopy --strip-debug --strip-unneeded \"%{cfg.buildtarget.relpath}\"",
                "objcopy --add-gnu-debuglink=\"%{cfg.buildtarget.relpath}.debug\" \"%{cfg.buildtarget.relpath}\""
            }

        ------------- Common -------------
        filter {"system:linux","configurations:Debug"}
            defines { "_DEBUG", "DEBUG", "linux", "__linux__" }
            symbols "On"
            targetname "AfterNightCLI_d"

        filter {"system:linux","configurations:Release"}
            defines { "NDEBUG","linux", "__linux__" }
            symbols "On"
            optimize "Speed"
            targetname "AfterNightCLI"

        filter {"system:windows","configurations:Debug"}
            defines { "_DEBUG","DEBUG", "WIN32" }
            symbols "On"
            targetname "AfterNightCLI_d"

        filter {"system:windows","configurations:Release"}
            defines { "NDEBUG", "WIN32" }
            symbols "On"
            optimize "Speed"
            targetname "AfterNightCLI"

        filter {}

        -- The instrumentation this tree was generated for, if any: the consumer closures are instrumented with the
        -- engine they run on, so a sanitizer run over a consumer suite covers the libraries it exercises. Last in the
        -- project, because it overrides link-time code generation and incremental linking.
        common.applyEngineSanitizer()
        filter {}

    -- AfterNightExtensionProtocol: Engine-free protocol serialization/framing
    -- shared by the app, full extension host, lite extension host, and tests.
    project "AfterNightExtensionProtocol"

        qt.enable(6)
        qtuseexternalinclude ( true )

        filter {"system:windows"}
            qtmodules { "core", "network" }

        filter {"system:linux"}
            qtmodules { "core", "network" }

        filter {}

        qtprefix "Qt6"

        kind "SharedLib"
        language "C++"
        defines { "AFTERNIGHTEXTENSIONPROTOCOL_EXPORTS" }

        files {
            "../../src/AfterNightExtensionProtocol/**.h",
            "../../src/AfterNightExtensionProtocol/**.cpp"
        }

        includedirs {
            "../../src/AfterNightExtensionProtocol",
            "../../3rdparty/src"
        }

        vpaths {
            ["protocol"] = { "../../src/AfterNightExtensionProtocol/**.*" }
        }

        filter {"action:vs*"}
            buildoptions { "/Zc:__cplusplus", "/utf-8", "/FS" }
            disablewarnings { "4251" }
            objdir ("../../obj/AfterNightExtensionProtocol/" .. common.WindowsOutputDir)
            qtincludepath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/include")
            qtlibpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib")
            qtbinpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/bin")
            qtgenerateddir ("../../obj/AfterNightExtensionProtocol/" .. common.WindowsOutputDir)

        filter {"system:linux"}
            buildoptions { "-fPIC" }
            objdir "../../obj/AfterNightExtensionProtocol/clang"
            qtincludepath("../../Qt6/" .. common.QT_VERSION .. "/gcc_64/include")
            qtlibpath("../../Qt6/" .. common.QT_VERSION .. "/gcc_64/lib")
            qtbinpath("../../Qt6/" .. common.QT_VERSION .. "/gcc_64/libexec")
            qtgenerateddir "../../obj/AfterNightExtensionProtocol/clang"

        filter {"action:vs*", "configurations:Debug"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            implibdir ("../../lib/" .. common.WindowsOutputDir)
            targetname "AfterNightExtensionProtocol_d"
            symbols "On"
            qtsuffix "d"

        filter {"action:vs*", "configurations:Release"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            implibdir ("../../lib/" .. common.WindowsOutputDir)
            targetname "AfterNightExtensionProtocol"
            symbols "On"
            optimize "Speed"

        filter {"system:linux", "configurations:Debug"}
            targetdir "../../bin/clang/debug"
            targetname "AfterNightExtensionProtocol_d"
            defines { "_DEBUG", "DEBUG", "linux", "__linux__" }
            symbols "On"

        filter {"system:linux", "configurations:Release"}
            targetdir "../../bin/clang/release"
            targetname "AfterNightExtensionProtocol"
            defines { "NDEBUG", "linux", "__linux__" }
            symbols "On"
            optimize "Speed"
            postbuildcommands {
                "objcopy --only-keep-debug \"%{cfg.buildtarget.relpath}\" \"%{cfg.buildtarget.relpath}.debug\"",
                "objcopy --strip-debug --strip-unneeded \"%{cfg.buildtarget.relpath}\"",
                "objcopy --add-gnu-debuglink=\"%{cfg.buildtarget.relpath}.debug\" \"%{cfg.buildtarget.relpath}\""
            }

        filter {}

        -- The instrumentation this tree was generated for, if any: the consumer closures are instrumented with the
        -- engine they run on, so a sanitizer run over a consumer suite covers the libraries it exercises. Last in the
        -- project, because it overrides link-time code generation and incremental linking.
        common.applyEngineSanitizer()
        filter {}

    -- AfterNightControls: Shared library containing the process control framework,
    -- reusable UI widgets, and app services under src/AfterNightControls.
    -- Concrete process shells and implementations live in AfterNightProcesses.
    -- The executable shell (main.cpp, AfterNightApp, and AfterNightAppContext)
    -- stays in src/AfterNightApp.
    project "AfterNightControls"

        qt.enable(6)
        qtuseexternalinclude ( true )

        filter {"system:windows"}
            qtmodules { "core", "gui", "widgets", "svg", "sql", "network" }

        filter {"system:linux"}
            qtmodules { "core", "gui", "widgets", "svg", "sql", "network" }

        filter {"action:vs*", "configurations:Debug"}
            links { "Qt6Concurrentd" }

        filter {"action:vs*", "configurations:Release"}
            links { "Qt6Concurrent" }

        filter {"system:linux"}
            links { "Qt6Concurrent" }

        filter {}

        qtprefix "Qt6"

        kind "SharedLib"
        language "C++"
        defines { "AFTERNIGHTCONTROLS_EXPORTS" }

        -- The library is built on the consumer runtime and the engine: every unit under its directory, the session
        -- bridge among them, includes the engine's published headers. The registry states the class; a record that
        -- says otherwise is refused here.
        if targets.engineClass("AfterNightControls") ~= "engine" then
            error("AfterNightControls compiles against the engine; consumer_targets.txt must give it class engine")
        end

        dependson { "AfterNightConsumerRuntime", "AfterNightEngine", "AfterNightPlatform", "AfterNightExtensionProtocol" }

        vectorextensions "AVX2"

        ------------- Source code -------------
        -- The published surfaces of the runtime, the engine and the platform library, the library's own tree, and the
        -- docking framework every dockable window derives from.
        filter { "system:linux" }
            includedirs { "../../src/AfterNightConsumerRuntime/include", "../../src/AfterNightEngine/include", "../../src/AfterNightPlatform/include", "../../src/AfterNightControls", "../../3rdparty/src", "../../kddockwidgets/build-clang-install/include/kddockwidgets-qt6", "../../vcpkg/installed/x64-linux/include", "../../vcpkg/installed/x64-linux-release/include" }
            -- The engine's contraction policy: the library compiles the engine's public headers.
            buildoptions { "-ffp-contract=off" }

        filter {"action:vs*"}
            includedirs { "../../src/AfterNightConsumerRuntime/include", "../../src/AfterNightEngine/include", "../../src/AfterNightPlatform/include", "../../src/AfterNightControls", "../../3rdparty/src", "../../kddockwidgets/build-msvc-install/include/kddockwidgets-qt6", "../../vcpkg/installed/x64-windows/include", "../../vcpkg/installed/x64-windows-release/include" }
            buildoptions { "/Zc:__cplusplus", "/utf-8", "/FS" }

        filter {}
            files {
                "../../src/AfterNightControls/**.h",
                "../../src/AfterNightControls/**.hpp",
                "../../src/AfterNightControls/**.inl",
                "../../src/AfterNightControls/**.ui",
                "../../src/AfterNightControls/**.qrc",
                "../../src/AfterNightControls/**.cpp",
                "../../src/AfterNightControls/data/**",
                "../../src/AfterNightControls/icons/**"
            }

        filter {}

        ------------- Virtual folders -------------
        vpaths {
                ["base"] = {
                    "../../src/AfterNightControls/AfterNight.qrc",
                    "../../src/AfterNightControls/AfterNightControlsExport.h",
                    "../../src/AfterNightControls/SexagesimalFormatter.h",
                    "../../src/AfterNightControls/common.h",
                    "../../src/AfterNightControls/stdafx.*"
                },
                ["app"] = { "../../src/AfterNightControls/app/**.*" },
                ["components"] = { "../../src/AfterNightControls/components/**.*" },
                ["controls"] = { "../../src/AfterNightControls/controls/*.*" },
                ["controls/drawing"] = { "../../src/AfterNightControls/controls/drawing/**.*" },
                ["controls/settings"] = { "../../src/AfterNightControls/controls/settings/**.*" },
                ["data"] = { "../../src/AfterNightControls/data/**" },
                ["dialogs"] = { "../../src/AfterNightControls/dialogs/**.*" },
                ["session"] = { "../../src/AfterNightControls/session/**.*" },
                ["icons"] = { "../../src/AfterNightControls/icons/**" },
                ["theme"] = { "../../src/AfterNightControls/theme/**.*" },
                ["widgets"] = { "../../src/AfterNightControls/widgets/**.*" }
               }

        pchheader "stdafx.hpp"
        pchsource "../../src/AfterNightControls/stdafx.cpp"

        -- MSVC: force-include the PCH header via /FI so that generated files
        -- (qrc_*.cpp, moc_*.cpp) satisfy the /Yu requirement automatically.
        filter {"action:vs*"}
            forceincludes { "stdafx.hpp" }
            multiprocessorcompile "On"
            disablewarnings { "4251" }
            objdir ("../../obj/AfterNightControls/" .. common.WindowsOutputDir)
        filter {}

        ------------- MSVC Qt Configuration -------------
        filter {"action:vs*", "configurations:Debug"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            libdirs { "../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib", ("../../lib/" .. common.WindowsOutputDir), "../../3rdparty/lib/msvc/debug", "../../kddockwidgets/build-msvc-install/lib", "../../vcpkg/installed/x64-windows/debug/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            links { "AfterNightConsumerRuntime_d.lib", "AfterNightEngine_d.lib", "AfterNightPlatform_d.lib", "AfterNightExtensionProtocol_d.lib", "kddockwidgets-qt6d.lib", "tbb12_debug.lib", "zstd.lib", "libsodium.lib" }
            qtincludepath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/include")
            qtlibpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib")
            qtbinpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/bin")
            qtgenerateddir ("../../obj/AfterNightControls/" .. common.WindowsOutputDir)
            qtsuffix "d"

        filter {"action:vs*", "configurations:Release"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            libdirs { "../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib", ("../../lib/" .. common.WindowsOutputDir), "../../3rdparty/lib/msvc/release", "../../kddockwidgets/build-msvc-install/lib", "../../vcpkg/installed/x64-windows/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            links { "AfterNightConsumerRuntime.lib", "AfterNightEngine.lib", "AfterNightPlatform.lib", "AfterNightExtensionProtocol.lib", "kddockwidgets-qt6.lib", "tbb12.lib", "zstd.lib", "libsodium.lib" }
            qtincludepath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/include")
            qtlibpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib")
            qtbinpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/bin")
            qtgenerateddir ("../../obj/AfterNightControls/" .. common.WindowsOutputDir)

        ------------- Clang (Linux) Qt Configuration -------------
        filter {"system:linux"}
            buildoptions { "-fPIC" }
            objdir "../../obj/AfterNightControls/clang"
            qtincludepath("../../Qt6/" .. common.QT_VERSION .. "/gcc_64/include")
            qtlibpath("../../Qt6/" .. common.QT_VERSION .. "/gcc_64/lib")
            qtbinpath("../../Qt6/" .. common.QT_VERSION .. "/gcc_64/libexec")
            qtgenerateddir "../../obj/AfterNightControls/clang"

        filter {"system:linux","configurations:Debug"}
            defines { "_DEBUG","DEBUG","linux", "__linux__", "KDDW_FRONTEND_QT", "KDDW_FRONTEND_QTWIDGETS" }
            symbols "On"
            targetname "AfterNightControls_d"
            targetdir "../../bin/clang/debug"
            libdirs { "../../bin/clang/debug", "../../3rdparty/bin/clang/debug", "../../kddockwidgets/build-clang-install/lib", "../../vcpkg/installed/x64-linux/debug/lib", "../../vcpkg/installed/x64-linux-release/lib" }
            linkoptions { "-lAfterNightConsumerRuntime_d", "-lAfterNightEngine_d", "-lAfterNightPlatform_d", "-lAfterNightExtensionProtocol_d", "-lkddockwidgets-qt6", "-ltbb", "-lzstd", "-lsodium", "-Wl,-rpath,'$$ORIGIN'" }

        filter {"system:linux","configurations:Release"}
            defines { "NDEBUG","linux", "__linux__", "KDDW_FRONTEND_QT", "KDDW_FRONTEND_QTWIDGETS" }
            symbols "On"
            optimize "Speed"
            targetname "AfterNightControls"
            targetdir "../../bin/clang/release"
            libdirs { "../../bin/clang/release", "../../3rdparty/bin/clang/release", "../../kddockwidgets/build-clang-install/lib", "../../vcpkg/installed/x64-linux/lib", "../../vcpkg/installed/x64-linux-release/lib" }
            linkoptions { "-lAfterNightConsumerRuntime", "-lAfterNightEngine", "-lAfterNightPlatform", "-lAfterNightExtensionProtocol", "-lkddockwidgets-qt6", "-ltbb", "-lzstd", "-lsodium", "-Wl,-rpath,'$$ORIGIN'" }
            postbuildcommands {
                "objcopy --only-keep-debug \"%{cfg.buildtarget.relpath}\" \"%{cfg.buildtarget.relpath}.debug\"",
                "objcopy --strip-debug --strip-unneeded \"%{cfg.buildtarget.relpath}\"",
                "objcopy --add-gnu-debuglink=\"%{cfg.buildtarget.relpath}.debug\" \"%{cfg.buildtarget.relpath}\""
            }

        filter {"system:windows","configurations:Debug"}
            defines { "_DEBUG","DEBUG","WIN32", "KDDW_FRONTEND_QT", "KDDW_FRONTEND_QTWIDGETS" }
            symbols "On"
            targetname "AfterNightControls_d"

        filter {"system:windows","action:vs*","configurations:Debug"}
            implibdir ("../../lib/" .. common.WindowsOutputDir)

        filter {"system:windows","configurations:Release"}
            defines { "NDEBUG","WIN32", "KDDW_FRONTEND_QT", "KDDW_FRONTEND_QTWIDGETS" }
            symbols "On"
            optimize "Speed"
            targetname "AfterNightControls"

        filter {"system:windows","action:vs*","configurations:Release"}
            implibdir ("../../lib/" .. common.WindowsOutputDir)

        -- The instrumentation this tree was generated for, if any: the consumer closures are instrumented with the
        -- engine they run on, so a sanitizer run over a consumer suite covers the libraries it exercises. Last in the
        -- project, because it overrides link-time code generation and incremental linking.
        common.applyEngineSanitizer()
        filter {}

    -- AfterNightProcesses: Shared library containing built-in process shells,
    -- process launch/registration glue, and process-specific implementation
    -- sources under src/AfterNightProcesses.
    project "AfterNightProcesses"

        qt.enable(6)
        qtuseexternalinclude ( true )

        filter {"system:windows"}
            qtmodules { "core", "gui", "widgets", "svg", "sql", "network" }

        filter {"system:linux"}
            qtmodules { "core", "gui", "widgets", "svg", "sql", "network" }

        filter {"action:vs*", "configurations:Debug"}
            links { "Qt6Concurrentd" }

        filter {"action:vs*", "configurations:Release"}
            links { "Qt6Concurrent" }

        filter {"system:linux"}
            links { "Qt6Concurrent" }

        filter {}

        qtprefix "Qt6"

        kind "SharedLib"
        language "C++"
        defines { "AFTERNIGHTPROCESSES_EXPORTS" }

        -- The library is built on the Controls library, the consumer runtime and the engine: every unit under its
        -- directory includes the engine's published headers. Its G'MIC processes run through the G'MIC adapter, which
        -- refuses every call by name in a build without the library it adapts. The registry states the class; a
        -- record that says otherwise is refused here.
        if targets.engineClass("AfterNightProcesses") ~= "engine" then
            error("AfterNightProcesses compiles against the engine; consumer_targets.txt must give it class engine")
        end

        dependson { "AfterNightControls", "AfterNightConsumerRuntime", "AfterNightEngine", "AfterNightEngineGmicAdapter", "AfterNightPlatform", "AfterNightExtensionProtocol" }

        vectorextensions "AVX2"

        ------------- Source code -------------
        filter { "system:linux" }
            includedirs { "../../src/AfterNightConsumerRuntime/include", "../../src/AfterNightEngine/include", "../../src/AfterNightEngineGmicAdapter/include", "../../src/AfterNightPlatform/include", "../../src/AfterNightProcesses", "../../src/AfterNightControls", "../../3rdparty/src", "../../kddockwidgets/build-clang-install/include/kddockwidgets-qt6", "../../vcpkg/installed/x64-linux/include", "../../vcpkg/installed/x64-linux-release/include" }
            -- The engine's contraction policy: the library compiles the engine's public headers.
            buildoptions { "-ffp-contract=off" }

        filter {"action:vs*"}
            includedirs { "../../src/AfterNightConsumerRuntime/include", "../../src/AfterNightEngine/include", "../../src/AfterNightEngineGmicAdapter/include", "../../src/AfterNightPlatform/include", "../../src/AfterNightProcesses", "../../src/AfterNightControls", "../../3rdparty/src", "../../kddockwidgets/build-msvc-install/include/kddockwidgets-qt6", "../../vcpkg/installed/x64-windows/include", "../../vcpkg/installed/x64-windows-release/include" }
            buildoptions { "/Zc:__cplusplus", "/utf-8", "/FS", "/bigobj" }

        filter {}
            files {
                "../../src/AfterNightProcesses/**.h",
                "../../src/AfterNightProcesses/**.hpp",
                "../../src/AfterNightProcesses/**.inl",
                "../../src/AfterNightProcesses/**.ui",
                "../../src/AfterNightProcesses/**.qrc",
                "../../src/AfterNightProcesses/**.cpp"
            }

        filter {}

        ------------- Virtual folders -------------
        vpaths {
                ["base"] = {
                    "../../src/AfterNightProcesses/AfterNightProcessesExport.h",
                    "../../src/AfterNightProcesses/stdafx.*"
                },
                ["app"] = { "../../src/AfterNightProcesses/app/**.*" },
                ["processes"] = { "../../src/AfterNightProcesses/processes/*.*" },
                ["processes/core"] = { "../../src/AfterNightProcesses/processes/core/**.*" },
                ["processes/ImageIntegration"] = { "../../src/AfterNightProcesses/processes/ImageIntegration/**.*" },
                ["processes/PhotometricColorCalibration"] = { "../../src/AfterNightProcesses/processes/PhotometricColorCalibration/**.*" },
                ["processes/SpectrophotometricColorCalibration"] = { "../../src/AfterNightProcesses/processes/SpectrophotometricColorCalibration/**.*" }
               }

        pchheader "stdafx.hpp"
        pchsource "../../src/AfterNightProcesses/stdafx.cpp"

        filter {"action:vs*"}
            forceincludes { "stdafx.hpp" }
            multiprocessorcompile "On"
            disablewarnings { "4251" }
            objdir ("../../obj/AfterNightProcesses/" .. common.WindowsOutputDir)
        filter {}

        ------------- MSVC Qt Configuration -------------
        filter {"action:vs*", "configurations:Debug"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            implibdir ("../../lib/" .. common.WindowsOutputDir)
            libdirs { "../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib", ("../../lib/" .. common.WindowsOutputDir), "../../3rdparty/lib/msvc/debug", "../../kddockwidgets/build-msvc-install/lib", "../../vcpkg/installed/x64-windows/debug/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            links { "AfterNightControls_d.lib", "AfterNightConsumerRuntime_d.lib", "AfterNightEngine_d.lib", "AfterNightEngineGmicAdapter_d.lib", "AfterNightPlatform_d.lib", "AfterNightExtensionProtocol_d.lib", "kddockwidgets-qt6d.lib", "tbb12_debug.lib", "zstd.lib", "libsodium.lib" }
            qtincludepath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/include")
            qtlibpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib")
            qtbinpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/bin")
            qtgenerateddir ("../../obj/AfterNightProcesses/" .. common.WindowsOutputDir)
            qtsuffix "d"

        filter {"action:vs*", "configurations:Release"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            implibdir ("../../lib/" .. common.WindowsOutputDir)
            libdirs { "../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib", ("../../lib/" .. common.WindowsOutputDir), "../../3rdparty/lib/msvc/release", "../../kddockwidgets/build-msvc-install/lib", "../../vcpkg/installed/x64-windows/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            links { "AfterNightControls.lib", "AfterNightConsumerRuntime.lib", "AfterNightEngine.lib", "AfterNightEngineGmicAdapter.lib", "AfterNightPlatform.lib", "AfterNightExtensionProtocol.lib", "kddockwidgets-qt6.lib", "tbb12.lib", "zstd.lib", "libsodium.lib" }
            qtincludepath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/include")
            qtlibpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib")
            qtbinpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/bin")
            qtgenerateddir ("../../obj/AfterNightProcesses/" .. common.WindowsOutputDir)

        ------------- Clang (Linux) Qt Configuration -------------
        filter {"system:linux"}
            buildoptions { "-fPIC" }
            objdir "../../obj/AfterNightProcesses/clang"
            qtincludepath("../../Qt6/" .. common.QT_VERSION .. "/gcc_64/include")
            qtlibpath("../../Qt6/" .. common.QT_VERSION .. "/gcc_64/lib")
            qtbinpath("../../Qt6/" .. common.QT_VERSION .. "/gcc_64/libexec")
            qtgenerateddir "../../obj/AfterNightProcesses/clang"

        filter {"system:linux","configurations:Debug"}
            defines { "_DEBUG","DEBUG","linux", "__linux__", "KDDW_FRONTEND_QT", "KDDW_FRONTEND_QTWIDGETS" }
            symbols "On"
            targetname "AfterNightProcesses_d"
            targetdir "../../bin/clang/debug"
            libdirs { "../../bin/clang/debug", "../../3rdparty/bin/clang/debug", "../../kddockwidgets/build-clang-install/lib", "../../vcpkg/installed/x64-linux/debug/lib", "../../vcpkg/installed/x64-linux-release/lib" }
            linkoptions { "-lAfterNightControls_d", "-lAfterNightConsumerRuntime_d", "-lAfterNightEngine_d", "-lAfterNightEngineGmicAdapter_d", "-lAfterNightPlatform_d", "-lAfterNightExtensionProtocol_d", "-lkddockwidgets-qt6", "-ltbb", "-lzstd", "-lsodium", "-Wl,-rpath,'$$ORIGIN'" }

        filter {"system:linux","configurations:Release"}
            defines { "NDEBUG","linux", "__linux__", "KDDW_FRONTEND_QT", "KDDW_FRONTEND_QTWIDGETS" }
            symbols "On"
            optimize "Speed"
            targetname "AfterNightProcesses"
            targetdir "../../bin/clang/release"
            libdirs { "../../bin/clang/release", "../../3rdparty/bin/clang/release", "../../kddockwidgets/build-clang-install/lib", "../../vcpkg/installed/x64-linux/lib", "../../vcpkg/installed/x64-linux-release/lib" }
            linkoptions { "-lAfterNightControls", "-lAfterNightConsumerRuntime", "-lAfterNightEngine", "-lAfterNightEngineGmicAdapter", "-lAfterNightPlatform", "-lAfterNightExtensionProtocol", "-lkddockwidgets-qt6", "-ltbb", "-lzstd", "-lsodium", "-Wl,-rpath,'$$ORIGIN'" }
            postbuildcommands {
                "objcopy --only-keep-debug \"%{cfg.buildtarget.relpath}\" \"%{cfg.buildtarget.relpath}.debug\"",
                "objcopy --strip-debug --strip-unneeded \"%{cfg.buildtarget.relpath}\"",
                "objcopy --add-gnu-debuglink=\"%{cfg.buildtarget.relpath}.debug\" \"%{cfg.buildtarget.relpath}\""
            }

        filter {"system:windows","configurations:Debug"}
            defines { "_DEBUG","DEBUG","WIN32", "KDDW_FRONTEND_QT", "KDDW_FRONTEND_QTWIDGETS" }
            symbols "On"
            targetname "AfterNightProcesses_d"

        filter {"system:windows","action:vs*","configurations:Debug"}
            implibdir ("../../lib/" .. common.WindowsOutputDir)

        filter {"system:windows","configurations:Release"}
            defines { "NDEBUG","WIN32", "KDDW_FRONTEND_QT", "KDDW_FRONTEND_QTWIDGETS" }
            symbols "On"
            optimize "Speed"
            targetname "AfterNightProcesses"

        filter {"system:windows","action:vs*","configurations:Release"}
            implibdir ("../../lib/" .. common.WindowsOutputDir)

        -- The instrumentation this tree was generated for, if any: the consumer closures are instrumented with the
        -- engine they run on, so a sanitizer run over a consumer suite covers the libraries it exercises. Last in the
        -- project, because it overrides link-time code generation and incremental linking.
        common.applyEngineSanitizer()
        filter {}

    -- The GUI interface
    project "AfterNightApp"

        qt.enable(6)
        qtuseexternalinclude ( true )

        filter {"system:windows"}
            qtmodules { "core", "gui", "widgets", "entrypoint", "svg", "sql", "network" }

        filter {"system:linux"}
            qtmodules { "core", "gui", "widgets", "svg", "sql", "network" }

        filter {"action:vs*", "configurations:Debug"}
            links { "Qt6Concurrentd" }

        filter {"action:vs*", "configurations:Release"}
            links { "Qt6Concurrent" }

        filter {"system:linux"}
            links { "Qt6Concurrent" }

        filter {}

        qtprefix "Qt6"

        kind "WindowedApp"
        language "C++"
        -- Declared long-path aware on Windows, as every executable of the product is.
        common.applyApplicationManifest()

        -- The application is built on the processes and Controls libraries, the consumer runtime and the engine: its
        -- window, context and session include the engine's published headers. It owns the process's one engine
        -- session. The registry states the class; a record that says otherwise is refused here.
        if targets.engineClass("AfterNightApp") ~= "engine" then
            error("AfterNightApp compiles against the engine; consumer_targets.txt must give it class engine")
        end

        dependson { "AfterNightProcesses", "AfterNightControls", "AfterNightConsumerRuntime", "AfterNightEngine", "AfterNightPlatform", "AfterNightExtensionProtocol" }

        ------------- Source code (app shell only — controls/processes are in shared libraries) -------------
        filter { "system:linux" }
            includedirs { "../../src/AfterNightConsumerRuntime/include", "../../src/AfterNightEngine/include", "../../src/AfterNightPlatform/include", "../../src/AfterNightApp", "../../src/AfterNightProcesses", "../../src/AfterNightProcesses/app", "../../src/AfterNightControls", "../../src/AfterNightControls/app", "../../src/AfterNightExtensionProtocol", "../../3rdparty/src", "../../kddockwidgets/build-clang-install/include/kddockwidgets-qt6", "../../vcpkg/installed/x64-linux/include", "../../vcpkg/installed/x64-linux-release/include" }
            -- The engine's contraction policy: the application compiles the engine's public headers.
            buildoptions { "-ffp-contract=off" }

        filter {"action:vs*"}
            includedirs { "../../src/AfterNightConsumerRuntime/include", "../../src/AfterNightEngine/include", "../../src/AfterNightPlatform/include", "../../src/AfterNightApp", "../../src/AfterNightProcesses", "../../src/AfterNightProcesses/app", "../../src/AfterNightControls", "../../src/AfterNightControls/app", "../../src/AfterNightExtensionProtocol", "../../3rdparty/src", "../../kddockwidgets/build-msvc-install/include/kddockwidgets-qt6", "../../vcpkg/installed/x64-windows/include", "../../vcpkg/installed/x64-windows-release/include" }
            buildoptions { "/Zc:__cplusplus", "/utf-8", "/FS" }

        filter { "system:windows" }
            files {
                "../../src/AfterNightApp/main.cpp",
                "../../src/AfterNightApp/AfterNightApp.ico",
                "../../src/AfterNightApp/AfterNightAppResources.rc",
                "../../src/AfterNightApp/app/AfterNightApp.h",
                "../../src/AfterNightApp/app/AfterNightApp.cpp",
                "../../src/AfterNightApp/app/AfterNightAppContext.h",
                "../../src/AfterNightApp/app/AfterNightAppContext.cpp",
                "../../src/AfterNightApp/app/ApplicationSession.h",
                "../../src/AfterNightApp/app/ApplicationSession.cpp"
            }

        filter { "system:linux" }
            files {
                "../../src/AfterNightApp/main.cpp",
                "../../src/AfterNightApp/app/AfterNightApp.h",
                "../../src/AfterNightApp/app/AfterNightApp.cpp",
                "../../src/AfterNightApp/app/AfterNightAppContext.h",
                "../../src/AfterNightApp/app/AfterNightAppContext.cpp",
                "../../src/AfterNightApp/app/ApplicationSession.h",
                "../../src/AfterNightApp/app/ApplicationSession.cpp"
            }

        filter {}

        ------------- Virtual folders (app shell only) -------------
        vpaths {
                ["app"] = { "../../src/AfterNightApp/app/AfterNightApp.*", "../../src/AfterNightApp/app/AfterNightAppContext.*", "../../src/AfterNightApp/app/ApplicationSession.*" },
                ["base"] = { "../../src/AfterNightApp/main.cpp", "../../src/AfterNightApp/AfterNightAppResources.rc", "../../src/AfterNightApp/AfterNightApp.ico" }
               }

        vectorextensions "AVX2"

        ------------- MSVC -------------

        filter {"action:vs*"}
            disablewarnings { "4251" }
            multiprocessorcompile "On"
            objdir  ("../../obj/AfterNightApp/" .. common.WindowsOutputDir)

        ------------- MSVC Qt Configuration -------------
        filter {"action:vs*", "configurations:Debug"}
            links { "AfterNightProcesses_d.lib", "AfterNightControls_d.lib", "AfterNightConsumerRuntime_d.lib", "AfterNightEngine_d.lib", "AfterNightPlatform_d.lib", "AfterNightExtensionProtocol_d.lib", "kddockwidgets-qt6d.lib", "tbb12_debug.lib" }
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            debugdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            libdirs { "../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib", ("../../lib/" .. common.WindowsOutputDir), "../../3rdparty/lib/msvc/debug", "../../kddockwidgets/build-msvc-install/lib", "../../vcpkg/installed/x64-windows/debug/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            linkoptions { "/STACK:4194304"}
            qtincludepath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/include")
            qtlibpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib")
            qtbinpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/bin")
            qtgenerateddir ("../../obj/AfterNightApp/" .. common.WindowsOutputDir)
            qtsuffix "d"
            postbuildcommands { "call \"$(VCInstallDir)Auxiliary\\Build\\vcvars64.bat\"", "..\\..\\Qt6\\" .. common.QT_VERSION .. "\\msvc2022_64\\bin\\windeployqt $(TargetPath) --no-translations" }

        filter {"action:vs*", "configurations:Release"}
            links { "AfterNightProcesses.lib", "AfterNightControls.lib", "AfterNightConsumerRuntime.lib", "AfterNightEngine.lib", "AfterNightPlatform.lib", "AfterNightExtensionProtocol.lib", "kddockwidgets-qt6.lib", "tbb12.lib" }
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            debugdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            libdirs { "../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib", ("../../lib/" .. common.WindowsOutputDir), "../../3rdparty/lib/msvc/release", "../../kddockwidgets/build-msvc-install/lib", "../../vcpkg/installed/x64-windows/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            linkoptions { "/STACK:4194304"}

            qtincludepath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/include")
            qtlibpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib")
            qtbinpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/bin")
            qtgenerateddir ("../../obj/AfterNightApp/" .. common.WindowsOutputDir)
            postbuildcommands { "call \"$(VCInstallDir)Auxiliary\\Build\\vcvars64.bat\"", "..\\..\\Qt6\\" .. common.QT_VERSION .. "\\msvc2022_64\\bin\\windeployqt $(TargetPath) --no-translations" }

        ------------- Clang (Linux) Qt Configuration -------------
        filter {"system:linux"}
            buildoptions { "-fPIC" }
            objdir  "../../obj/AfterNightApp/clang"
            qtincludepath("../../Qt6/" .. common.QT_VERSION .. "/gcc_64/include")
            qtlibpath("../../Qt6/" .. common.QT_VERSION .. "/gcc_64/lib")
            qtbinpath("../../Qt6/" .. common.QT_VERSION .. "/gcc_64/libexec")
            qtgenerateddir "../../obj/AfterNightApp/clang"

        filter {"system:linux","action:codelite"}
            debugenvs {
                "LD_LIBRARY_PATH=./:$(OutputDirectory)/lib:$LD_LIBRARY_PATH",
                "QT_PLUGIN_PATH=$(OutputDirectory)/plugins"
            }

        filter {}

        filter {"system:linux","configurations:Debug"}
            defines { "_DEBUG","DEBUG","linux", "__linux__", "KDDW_FRONTEND_QT", "KDDW_FRONTEND_QTWIDGETS" }
            symbols "On"
            targetname "AfterNight_d"
            targetdir "../../bin/clang/debug"
            debugdir "../../bin/clang/debug"
            libdirs { "../../bin/clang/debug", "../../3rdparty/bin/clang/debug", "../../kddockwidgets/build-clang-install/lib", "../../vcpkg/installed/x64-linux/debug/lib", "../../vcpkg/installed/x64-linux-release/lib" }
            linkoptions  { "-lAfterNightProcesses_d", "-lAfterNightControls_d", "-lAfterNightConsumerRuntime_d", "-lAfterNightEngine_d", "-lAfterNightPlatform_d", "-lAfterNightExtensionProtocol_d", "-lkddockwidgets-qt6", "-ltbb", "-Wl,-z,noexecstack", "-Wl,-rpath,'$$ORIGIN'" }

        filter {"system:linux","configurations:Release"}
            defines { "NDEBUG", "linux", "__linux__", "KDDW_FRONTEND_QT", "KDDW_FRONTEND_QTWIDGETS" }
            symbols "On"
            optimize "Speed"
            targetname "AfterNight"
            targetdir "../../bin/clang/release"
            debugdir "../../bin/clang/release"
            libdirs { "../../bin/clang/release", "../../3rdparty/bin/clang/release", "../../kddockwidgets/build-clang-install/lib", "../../vcpkg/installed/x64-linux/lib", "../../vcpkg/installed/x64-linux-release/lib" }
            linkoptions  { "-lAfterNightProcesses", "-lAfterNightControls", "-lAfterNightConsumerRuntime", "-lAfterNightEngine", "-lAfterNightPlatform", "-lAfterNightExtensionProtocol", "-lkddockwidgets-qt6", "-ltbb", "-Wl,-z,noexecstack", "-Wl,-rpath,'$$ORIGIN'" }
            postbuildcommands {
                "objcopy --only-keep-debug \"%{cfg.buildtarget.relpath}\" \"%{cfg.buildtarget.relpath}.debug\"",
                "objcopy --strip-debug --strip-unneeded \"%{cfg.buildtarget.relpath}\"",
                "objcopy --add-gnu-debuglink=\"%{cfg.buildtarget.relpath}.debug\" \"%{cfg.buildtarget.relpath}\""
            }

        filter {"system:windows","configurations:Debug"}
            defines { "_DEBUG","DEBUG","WIN32", "KDDW_FRONTEND_QT", "KDDW_FRONTEND_QTWIDGETS" }
            symbols "On"
            targetname "AfterNight_d"

        filter {"system:windows","configurations:Release"}
            defines { "NDEBUG","WIN32", "KDDW_FRONTEND_QT", "KDDW_FRONTEND_QTWIDGETS" }
            symbols "On"
            optimize "Speed"
            targetname "AfterNight"

        -- The instrumentation this tree was generated for, if any: the consumer closures are instrumented with the
        -- engine they run on, so a sanitizer run over a consumer suite covers the libraries it exercises. Last in the
        -- project, because it overrides link-time code generation and incremental linking.
        common.applyEngineSanitizer()
        filter {}

    -- AfterNightStylePlugin: Qt Style Plugin that wraps AnProxyStyle + AnDesignTokens
    -- from libAfterNightControls as a dynamically loadable QStyleFactory plugin.
    -- Deployed to bin/<toolchain>/<config>/styles/ so Qt auto-discovers it via
    -- QStyleFactory::create("afternight").  Both AfterNightApp and
    -- AfterNightExtensionHost can use it for consistent theming.
    project "AfterNightStylePlugin"

        qt.enable(6)
        qtuseexternalinclude ( true )

        filter {"system:windows"}
            qtmodules { "core", "gui", "widgets" }

        filter {"system:linux"}
            qtmodules { "core", "gui", "widgets" }

        filter {}

        qtprefix "Qt6"

        kind "SharedLib"
        language "C++"

        dependson { "AfterNightControls" }

        vectorextensions "AVX2"

        ------------- Source code -------------
        files {
            "../../src/AfterNightStylePlugin/**.h",
            "../../src/AfterNightStylePlugin/**.cpp",
            "../../src/AfterNightStylePlugin/**.json"
        }

        filter { "system:linux" }
            includedirs {
                "../../src/AfterNightControls",
                "../../src/AfterNightStylePlugin",
                "../../3rdparty/src",
                "../../kddockwidgets/build-clang-install/include/kddockwidgets-qt6"
            }
            buildoptions { "-fPIC", "-fvisibility=hidden" }

        filter {"action:vs*"}
            includedirs {
                "../../src/AfterNightControls",
                "../../src/AfterNightStylePlugin",
                "../../3rdparty/src",
                "../../kddockwidgets/build-msvc-install/include/kddockwidgets-qt6"
            }
            buildoptions { "/Zc:__cplusplus", "/utf-8", "/FS" }
            multiprocessorcompile "On"
            disablewarnings { "4251" }
            objdir ("../../obj/AfterNightStylePlugin/" .. common.WindowsOutputDir)

        filter {}

        vpaths {
            ["plugin"] = { "../../src/AfterNightStylePlugin/**.*" }
        }

        ------------- MSVC Qt Configuration -------------
        filter {"action:vs*", "configurations:Debug"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/debug/styles")
            libdirs { "../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib", ("../../lib/" .. common.WindowsOutputDir), "../../3rdparty/lib/msvc/debug", "../../kddockwidgets/build-msvc-install/lib" }
            links { "AfterNightControls_d.lib" }
            qtincludepath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/include")
            qtlibpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib")
            qtbinpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/bin")
            qtgenerateddir ("../../obj/AfterNightStylePlugin/" .. common.WindowsOutputDir)
            qtsuffix "d"

        filter {"action:vs*", "configurations:Release"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/release/styles")
            libdirs { "../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib", ("../../lib/" .. common.WindowsOutputDir), "../../3rdparty/lib/msvc/release", "../../kddockwidgets/build-msvc-install/lib" }
            links { "AfterNightControls.lib" }
            qtincludepath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/include")
            qtlibpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib")
            qtbinpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/bin")
            qtgenerateddir ("../../obj/AfterNightStylePlugin/" .. common.WindowsOutputDir)

        ------------- Clang (Linux) Qt Configuration -------------
        filter {"system:linux"}
            objdir "../../obj/AfterNightStylePlugin/clang"
            qtincludepath("../../Qt6/" .. common.QT_VERSION .. "/gcc_64/include")
            qtlibpath("../../Qt6/" .. common.QT_VERSION .. "/gcc_64/lib")
            qtbinpath("../../Qt6/" .. common.QT_VERSION .. "/gcc_64/libexec")
            qtgenerateddir "../../obj/AfterNightStylePlugin/clang"

        filter {"system:linux","configurations:Debug"}
            defines { "_DEBUG","DEBUG","linux", "__linux__", "KDDW_FRONTEND_QT", "KDDW_FRONTEND_QTWIDGETS" }
            symbols "On"
            targetname "qafternight_d"
            targetdir "../../bin/clang/debug/styles"
            libdirs { "../../bin/clang/debug", "../../3rdparty/bin/clang/debug", "../../kddockwidgets/build-clang-install/lib", "../../vcpkg/installed/x64-linux/debug/lib", "../../vcpkg/installed/x64-linux-release/lib" }
            linkoptions { "-lAfterNightControls_d", "-lkddockwidgets-qt6", "-ltbb" }

        filter {"system:linux","configurations:Release"}
            defines { "NDEBUG","linux", "__linux__", "KDDW_FRONTEND_QT", "KDDW_FRONTEND_QTWIDGETS" }
            symbols "On"
            optimize "Speed"
            targetname "qafternight"
            targetdir "../../bin/clang/release/styles"
            libdirs { "../../bin/clang/release", "../../3rdparty/bin/clang/release", "../../kddockwidgets/build-clang-install/lib", "../../vcpkg/installed/x64-linux/lib", "../../vcpkg/installed/x64-linux-release/lib" }
            linkoptions { "-lAfterNightControls", "-lkddockwidgets-qt6", "-ltbb" }
            postbuildcommands {
                "objcopy --only-keep-debug \"%{cfg.buildtarget.relpath}\" \"%{cfg.buildtarget.relpath}.debug\"",
                "objcopy --strip-debug --strip-unneeded \"%{cfg.buildtarget.relpath}\"",
                "objcopy --add-gnu-debuglink=\"%{cfg.buildtarget.relpath}.debug\" \"%{cfg.buildtarget.relpath}\""
            }

        filter {"system:windows","configurations:Debug"}
            defines { "_DEBUG","DEBUG","WIN32", "KDDW_FRONTEND_QT", "KDDW_FRONTEND_QTWIDGETS" }
            symbols "On"
            targetname "qafternight_d"

        filter {"system:windows","configurations:Release"}
            defines { "NDEBUG","WIN32", "KDDW_FRONTEND_QT", "KDDW_FRONTEND_QTWIDGETS" }
            symbols "On"
            optimize "Speed"
            targetname "qafternight"

        -- The instrumentation this tree was generated for, if any: the consumer closures are instrumented with the
        -- engine they run on, so a sanitizer run over a consumer suite covers the libraries it exercises. Last in the
        -- project, because it overrides link-time code generation and incremental linking.
        common.applyEngineSanitizer()
        filter {}

    -- Extension subsystem targets are supported on Linux Clang and Windows MSVC.

    -- AfterNightExtensionHostLite: Engine-free protocol-only extension host.
    project "AfterNightExtensionHostLite"

        qt.enable(6)
        qtuseexternalinclude ( true )
        qtmodules { "core", "network" }
        qtprefix "Qt6"

        kind "ConsoleApp"
        language "C++"
        -- Declared long-path aware on Windows, as every executable of the product is.
        common.applyApplicationManifest()
        dependson { "AfterNightExtensionProtocol" }

        files {
            "../../src/AfterNightExtensionHostLite/**.h",
            "../../src/AfterNightExtensionHostLite/**.cpp"
        }

        includedirs {
            "../../src/AfterNightExtensionProtocol"
        }

        vpaths {
            ["lite-host"] = { "../../src/AfterNightExtensionHostLite/**.*" }
        }

        filter {"action:vs*"}
            buildoptions { "/Zc:__cplusplus", "/utf-8", "/FS" }
            includedirs {
                "../../src/AfterNightExtensionProtocol",
                "../../python/python/tools_msvc/include"
            }

        filter {"action:vs*", "configurations:Debug"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            debugdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            libdirs { "../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib", ("../../lib/" .. common.WindowsOutputDir), "../../python/python/tools_msvc/libs" }
            objdir  ("../../obj/AfterNightExtensionHostLite/" .. common.WindowsOutputDir)
            qtincludepath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/include")
            qtlibpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib")
            qtbinpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/bin")
            qtgenerateddir ("../../obj/AfterNightExtensionHostLite/" .. common.WindowsOutputDir)
            qtsuffix "d"

        filter {"action:vs*", "configurations:Release"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            debugdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            libdirs { "../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib", ("../../lib/" .. common.WindowsOutputDir), "../../python/python/tools_msvc/libs" }
            objdir  ("../../obj/AfterNightExtensionHostLite/" .. common.WindowsOutputDir)
            qtincludepath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/include")
            qtlibpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib")
            qtbinpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/bin")
            qtgenerateddir ("../../obj/AfterNightExtensionHostLite/" .. common.WindowsOutputDir)

        filter {"action:vs*","configurations:Debug"}
            defines { "_DEBUG","DEBUG", "WIN32" }
            symbols "On"
            links { "AfterNightExtensionProtocol_d.lib", "python314.lib" }
            targetname "AfterNightExtensionHostLite_d"

        filter {"action:vs*","configurations:Release"}
            defines { "NDEBUG", "WIN32" }
            symbols "On"
            links { "AfterNightExtensionProtocol.lib", "python314.lib" }
            optimize "Speed"
            targetname "AfterNightExtensionHostLite"

        filter {"system:linux"}
            includedirs {
                "../../src/AfterNightExtensionProtocol",
                "../../python/python/tools_linux/include/python3.14"
            }
            buildoptions { "-fPIC" }
            objdir  "../../obj/AfterNightExtensionHostLite/clang"
            qtincludepath("../../Qt6/" .. common.QT_VERSION .. "/gcc_64/include")
            qtlibpath("../../Qt6/" .. common.QT_VERSION .. "/gcc_64/lib")
            qtbinpath("../../Qt6/" .. common.QT_VERSION .. "/gcc_64/libexec")
            qtgenerateddir "../../obj/AfterNightExtensionHostLite/clang"

        filter {"system:linux", "configurations:Debug"}
            targetdir "../../bin/clang/debug"
            debugdir "../../bin/clang/debug"
            libdirs {
                "../../bin/clang/debug",
                "../../Qt6/" .. common.QT_VERSION .. "/gcc_64/lib",
                "../../python/python/tools_linux/lib"
            }
            defines { "_DEBUG", "DEBUG", "linux", "__linux__" }
            linkoptions  { "-lAfterNightExtensionProtocol_d", "-lpython3.14", "-Wl,-rpath,'$$ORIGIN':'$$ORIGIN/lib'" }
            symbols "On"
            targetname "AfterNightExtensionHostLite_d"

        filter {"system:linux", "configurations:Release"}
            targetdir "../../bin/clang/release"
            debugdir "../../bin/clang/release"
            libdirs {
                "../../bin/clang/release",
                "../../Qt6/" .. common.QT_VERSION .. "/gcc_64/lib",
                "../../python/python/tools_linux/lib"
            }
            defines { "NDEBUG", "linux", "__linux__" }
            linkoptions  { "-lAfterNightExtensionProtocol", "-lpython3.14", "-Wl,-rpath,'$$ORIGIN':'$$ORIGIN/lib'" }
            symbols "On"
            optimize "Speed"
            targetname "AfterNightExtensionHostLite"
            postbuildcommands {
                "objcopy --only-keep-debug \"%{cfg.buildtarget.relpath}\" \"%{cfg.buildtarget.relpath}.debug\"",
                "objcopy --strip-debug --strip-unneeded \"%{cfg.buildtarget.relpath}\"",
                "objcopy --add-gnu-debuglink=\"%{cfg.buildtarget.relpath}.debug\" \"%{cfg.buildtarget.relpath}\""
            }

        filter {}

        -- The instrumentation this tree was generated for, if any: the consumer closures are instrumented with the
        -- engine they run on, so a sanitizer run over a consumer suite covers the libraries it exercises. Last in the
        -- project, because it overrides link-time code generation and incremental linking.
        common.applyEngineSanitizer()
        filter {}

    -- AfterNightExtensionHost: Isolated-process extension host executable for a `runtime` extension. It opens no
    -- engine session: it loads the runtime backend module by name - never links it - and borrows the session the
    -- module owns through the backend's session capsule once its interpreter starts, which the host's native windows
    -- and views run in. It links the processes and Controls libraries, the consumer runtime, the engine and the
    -- interpreter.
    project "AfterNightExtensionHost"

        qt.enable(6)
        qtuseexternalinclude ( true )

        filter {"system:windows"}
            qtmodules { "core", "gui", "widgets", "entrypoint", "svg", "sql", "network" }

        filter {"system:linux"}
            qtmodules { "core", "gui", "widgets", "svg", "sql", "network" }

        filter {"action:vs*", "configurations:Debug"}
            links { "Qt6Concurrentd" }

        filter {"action:vs*", "configurations:Release"}
            links { "Qt6Concurrent" }

        filter {"system:linux"}
            links { "Qt6Concurrent" }

        filter {}

        qtprefix "Qt6"

        kind "ConsoleApp"
        language "C++"
        -- Declared long-path aware on Windows, as every executable of the product is.
        common.applyApplicationManifest()

        -- The host is built on the processes and Controls libraries, the consumer runtime and the engine.
        -- It reads the runtime backend's session capsule through the backend's published record, a header it includes
        -- from the backend's directory; the backend itself is loaded by name. The registry states the class; a record
        -- that says otherwise is refused here.
        if targets.engineClass("AfterNightExtensionHost") ~= "engine" then
            error("AfterNightExtensionHost compiles against the engine; consumer_targets.txt must give it class engine")
        end

        dependson { "AfterNightProcesses", "AfterNightControls", "AfterNightConsumerRuntime", "AfterNightEngine", "AfterNightPlatform", "AfterNightExtensionProtocol" }

        vectorextensions "AVX2"

        filter { "system:linux" }
            includedirs {
                "../../src/AfterNightConsumerRuntime/include",
                "../../src/AfterNightEngine/include",
                "../../src/AfterNightPlatform/include",
                "../../src/AfterNightProcesses",
                "../../src/AfterNightControls",
                "../../src/AfterNightExtensionProtocol",
                "../../src/AfterNightExtensionHost",
                "../../src/AfterNightExtensionRuntimeBackend",
                "../../3rdparty/src",
                "../../kddockwidgets/build-clang-install/include/kddockwidgets-qt6",
                "../../python/python/tools_linux/include/python3.14",
                "../../vcpkg/installed/x64-linux/include",
                "../../vcpkg/installed/x64-linux-release/include"
            }
            -- The engine's contraction policy: the host compiles the engine's public headers.
            buildoptions { "-ffp-contract=off" }

        filter {"action:vs*"}
            includedirs {
                "../../src/AfterNightConsumerRuntime/include",
                "../../src/AfterNightEngine/include",
                "../../src/AfterNightPlatform/include",
                "../../src/AfterNightProcesses",
                "../../src/AfterNightControls",
                "../../src/AfterNightExtensionProtocol",
                "../../src/AfterNightExtensionHost",
                "../../src/AfterNightExtensionRuntimeBackend",
                "../../3rdparty/src",
                "../../kddockwidgets/build-msvc-install/include/kddockwidgets-qt6",
                "../../python/python/tools_msvc/include",
                "../../vcpkg/installed/x64-windows/include",
                "../../vcpkg/installed/x64-windows-release/include"
            }
            buildoptions { "/Zc:__cplusplus", "/utf-8", "/FS" }

        filter { "action:vs*" }
            files {
                "../../src/AfterNightExtensionHost/**.h",
                "../../src/AfterNightExtensionHost/**.cpp"
            }

        filter { "system:linux" }
            files {
                "../../src/AfterNightExtensionHost/**.h",
                "../../src/AfterNightExtensionHost/**.cpp"
            }

        filter {}

        vpaths {
            ["host"] = { "../../src/AfterNightExtensionHost/**.*" }
        }

        filter {"action:vs*"}
            if _OPTIONS["cc"] == "msc" then
                buildoptions { "/utf-8", "/Zc:__cplusplus", "/FS", "/bigobj" }
            elseif _OPTIONS["cc"] == "clang" then
                buildoptions { }
            end

        filter {"action:vs*", "configurations:Debug"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            debugdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            libdirs { "../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib", ("../../lib/" .. common.WindowsOutputDir), "../../3rdparty/lib/msvc/debug", "../../kddockwidgets/build-msvc-install/lib", "../../python/python/tools_msvc/libs", "../../vcpkg/installed/x64-windows/debug/lib", "../../vcpkg/installed/x64-windows/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            linkoptions { "/STACK:4194304"}
            objdir  ("../../obj/AfterNightExtensionHost/" .. common.WindowsOutputDir)
            links { "kddockwidgets-qt6d.lib", "tbb12_debug.lib" }
            disablewarnings { "4251" }
            qtincludepath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/include")
            qtlibpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib")
            qtbinpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/bin")
            qtgenerateddir ("../../obj/AfterNightExtensionHost/" .. common.WindowsOutputDir)
            qtsuffix "d"

        filter {"action:vs*", "configurations:Release"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            debugdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            libdirs { "../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib", ("../../lib/" .. common.WindowsOutputDir), "../../3rdparty/lib/msvc/release", "../../kddockwidgets/build-msvc-install/lib", "../../python/python/tools_msvc/libs", "../../vcpkg/installed/x64-windows/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            linkoptions { "/STACK:4194304"}
            objdir  ("../../obj/AfterNightExtensionHost/" .. common.WindowsOutputDir)
            links { "kddockwidgets-qt6.lib", "tbb12.lib" }
            disablewarnings { "4251" }
            qtincludepath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/include")
            qtlibpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib")
            qtbinpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/bin")
            qtgenerateddir ("../../obj/AfterNightExtensionHost/" .. common.WindowsOutputDir)

        filter {"system:linux"}
            buildoptions { "-fPIC" }
            objdir  "../../obj/AfterNightExtensionHost/clang"
            qtincludepath("../../Qt6/" .. common.QT_VERSION .. "/gcc_64/include")
            qtlibpath("../../Qt6/" .. common.QT_VERSION .. "/gcc_64/lib")
            qtbinpath("../../Qt6/" .. common.QT_VERSION .. "/gcc_64/libexec")
            qtgenerateddir "../../obj/AfterNightExtensionHost/clang"

        filter {"system:linux", "configurations:Debug"}
            targetdir "../../bin/clang/debug"
            debugdir "../../bin/clang/debug"

        filter {"system:linux", "configurations:Release"}
            targetdir "../../bin/clang/release"
            debugdir "../../bin/clang/release"
            postbuildcommands {
                "objcopy --only-keep-debug \"%{cfg.buildtarget.relpath}\" \"%{cfg.buildtarget.relpath}.debug\"",
                "objcopy --strip-debug --strip-unneeded \"%{cfg.buildtarget.relpath}\"",
                "objcopy --add-gnu-debuglink=\"%{cfg.buildtarget.relpath}.debug\" \"%{cfg.buildtarget.relpath}\""
            }

        filter {"system:linux","configurations:Debug"}
            libdirs {
                "../../bin/clang/debug",
                "../../3rdparty/bin/clang/debug",
                "../../python/python/tools_linux/lib",
                "../../kddockwidgets/build-clang-install/lib",
                "../../vcpkg/installed/x64-linux/debug/lib",
                "../../vcpkg/installed/x64-linux-release/lib"
            }
            defines { "_DEBUG", "DEBUG", "linux", "__linux__", "KDDW_FRONTEND_QT", "KDDW_FRONTEND_QTWIDGETS" }
            linkoptions  { "-lAfterNightProcesses_d", "-lAfterNightControls_d", "-lAfterNightConsumerRuntime_d", "-lAfterNightEngine_d", "-lAfterNightPlatform_d", "-lAfterNightExtensionProtocol_d", "-lkddockwidgets-qt6", "-ltbb", "-lpython3.14", "-Wl,-z,noexecstack", "-Wl,-rpath,'$$ORIGIN':'$$ORIGIN/lib'" }
            symbols "On"
            targetname "AfterNightExtensionHost_d"

        filter {"system:linux","configurations:Release"}
            libdirs {
                "../../bin/clang/release",
                "../../3rdparty/bin/clang/release",
                "../../python/python/tools_linux/lib",
                "../../kddockwidgets/build-clang-install/lib",
                "../../vcpkg/installed/x64-linux/lib",
                "../../vcpkg/installed/x64-linux-release/lib"
            }
            defines { "NDEBUG","linux", "__linux__", "KDDW_FRONTEND_QT", "KDDW_FRONTEND_QTWIDGETS" }
            linkoptions  { "-lAfterNightProcesses", "-lAfterNightControls", "-lAfterNightConsumerRuntime", "-lAfterNightEngine", "-lAfterNightPlatform", "-lAfterNightExtensionProtocol", "-lkddockwidgets-qt6", "-ltbb", "-lpython3.14", "-Wl,-z,noexecstack", "-Wl,-rpath,'$$ORIGIN':'$$ORIGIN/lib'" }
            symbols "On"
            optimize "Speed"
            targetname "AfterNightExtensionHost"

        filter {"action:vs*","configurations:Debug"}
            defines { "_DEBUG","DEBUG", "WIN32", "KDDW_FRONTEND_QT", "KDDW_FRONTEND_QTWIDGETS" }
            symbols "On"
            links { "AfterNightProcesses_d.lib", "AfterNightControls_d.lib", "AfterNightConsumerRuntime_d.lib", "AfterNightEngine_d.lib", "AfterNightPlatform_d.lib", "AfterNightExtensionProtocol_d.lib", "kddockwidgets-qt6d.lib", "tbb12_debug.lib" }
            targetname "AfterNightExtensionHost_d"

        filter {"action:vs*","configurations:Release"}
            defines { "NDEBUG", "WIN32", "KDDW_FRONTEND_QT", "KDDW_FRONTEND_QTWIDGETS" }
            symbols "On"
            links { "AfterNightProcesses.lib", "AfterNightControls.lib", "AfterNightConsumerRuntime.lib", "AfterNightEngine.lib", "AfterNightPlatform.lib", "AfterNightExtensionProtocol.lib", "kddockwidgets-qt6.lib", "tbb12.lib" }
            optimize "Speed"
            targetname "AfterNightExtensionHost"

        -- The instrumentation this tree was generated for, if any: the consumer closures are instrumented with the
        -- engine they run on, so a sanitizer run over a consumer suite covers the libraries it exercises. Last in the
        -- project, because it overrides link-time code generation and incremental linking.
        common.applyEngineSanitizer()
        filter {}

    -- Runtime-loadable module for the `runtime` SDK backend: the five C exports the full host loads it by, the
    -- embedded `_afternight_runtime` Python module and the engine session both share. Loaded at runtime via dlopen
    -- (Linux) / LoadLibrary (Windows) - NOT linked at build time. It links the consumer runtime, the engine, the
    -- platform library and the interpreter, and no Qt and no product library.
    project "AfterNightExtensionRuntimeBackend"

        kind "SharedLib"
        language "C++"

        dependson { "AfterNightConsumerRuntime", "AfterNightEngine", "AfterNightPlatform" }

        vectorextensions "AVX2"

        files {
            "../../src/AfterNightExtensionRuntimeBackend/**.h",
            "../../src/AfterNightExtensionRuntimeBackend/**.cpp"
        }

        vpaths {
            ["backend"] = { "../../src/AfterNightExtensionRuntimeBackend/**.*" }
        }

        -- The published surfaces of the runtime, the engine and the platform library, and nothing else first-party.
        includedirs {
            "../../src/AfterNightExtensionRuntimeBackend",
            "../../src/AfterNightConsumerRuntime/include",
            "../../src/AfterNightEngine/include",
            "../../src/AfterNightPlatform/include"
        }

        filter { "system:linux" }
            includedirs {
                "../../vcpkg/installed/x64-linux/include", "../../vcpkg/installed/x64-linux-release/include",
                "../../python/python/tools_linux/include/python3.14"
            }
            -- The engine's contraction policy: the backend compiles the engine's public headers.
            buildoptions { "-fPIC", "-fvisibility=hidden", "-ffp-contract=off" }
            -- A module the host loads by name meets an unresolved symbol only when it is loaded, so the link refuses
            -- one: a symbol a library it links does not export fails here rather than in the host. A sanitizer
            -- generation leaves its runtime's symbols to the executable that loads the module, as every instrumented
            -- shared library does, so there the refusal would refuse the instrumentation itself.
            linkoptions { "-Wl,-rpath,'$$ORIGIN'" }
            if common.Sanitizer == nil then
                linkoptions { "-Wl,-z,defs" }
            end

        filter {"action:vs*"}
            includedirs {
                "../../vcpkg/installed/x64-windows/include", "../../vcpkg/installed/x64-windows-release/include",
                "../../python/python/tools_msvc/include"
            }
            disablewarnings { "4251" }
            if _OPTIONS["cc"] == "msc" then
                buildoptions { "/Zc:__cplusplus", "/utf-8", "/FS" }
            elseif _OPTIONS["cc"] == "clang" then
                buildoptions { "/clang:-ffp-contract=off", "-Wno-overriding-option" }
            end

        -- Linux
        filter {"system:linux"}
            objdir  "../../obj/AfterNightExtensionRuntimeBackend/clang"
            targetprefix "lib"

        filter {"system:linux", "configurations:Debug"}
            targetdir "../../bin/clang/debug"
            libdirs { "../../bin/clang/debug", "../../python/python/tools_linux/lib" }
            defines { "_DEBUG", "DEBUG", "linux", "__linux__" }
            linkoptions { "-lAfterNightConsumerRuntime_d", "-lAfterNightEngine_d", "-lAfterNightPlatform_d", "-lpython3.14", "-Wl,-z,noexecstack" }
            symbols "On"
            targetname "an_extension_runtime_backend_d"

        filter {"system:linux", "configurations:Release"}
            targetdir "../../bin/clang/release"
            libdirs { "../../bin/clang/release", "../../python/python/tools_linux/lib" }
            defines { "NDEBUG", "linux", "__linux__" }
            linkoptions { "-lAfterNightConsumerRuntime", "-lAfterNightEngine", "-lAfterNightPlatform", "-lpython3.14", "-Wl,-z,noexecstack" }
            symbols "On"
            optimize "Speed"
            targetname "an_extension_runtime_backend"
            postbuildcommands {
                "objcopy --only-keep-debug \"%{cfg.buildtarget.relpath}\" \"%{cfg.buildtarget.relpath}.debug\"",
                "objcopy --strip-debug --strip-unneeded \"%{cfg.buildtarget.relpath}\"",
                "objcopy --add-gnu-debuglink=\"%{cfg.buildtarget.relpath}.debug\" \"%{cfg.buildtarget.relpath}\""
            }

        -- MSVC
        filter {"action:vs*", "configurations:Debug"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            libdirs { ("../../lib/" .. common.WindowsOutputDir), "../../python/python/tools_msvc/libs" }
            objdir  ("../../obj/AfterNightExtensionRuntimeBackend/" .. common.WindowsOutputDir)
            links { "AfterNightConsumerRuntime_d.lib", "AfterNightEngine_d.lib", "AfterNightPlatform_d.lib", "python314.lib" }
            targetname "an_extension_runtime_backend_d"

        filter {"action:vs*", "configurations:Release"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            libdirs { ("../../lib/" .. common.WindowsOutputDir), "../../python/python/tools_msvc/libs" }
            objdir  ("../../obj/AfterNightExtensionRuntimeBackend/" .. common.WindowsOutputDir)
            links { "AfterNightConsumerRuntime.lib", "AfterNightEngine.lib", "AfterNightPlatform.lib", "python314.lib" }
            targetname "an_extension_runtime_backend"

        filter {"system:windows","configurations:Debug"}
            defines { "_DEBUG", "DEBUG", "WIN32" }
            symbols "On"

        filter {"system:windows","configurations:Release"}
            defines { "NDEBUG", "WIN32" }
            symbols "On"
            optimize "Speed"

        -- The instrumentation this tree was generated for, if any: the consumer closures are instrumented with the
        -- engine they run on, so a sanitizer run over a consumer suite covers the libraries it exercises. Last in the
        -- project, because it overrides link-time code generation and incremental linking.
        common.applyEngineSanitizer()
        filter {}
