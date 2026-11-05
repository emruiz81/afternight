--[[
   AfterNight Common Configuration
   
   This module provides shared constants, paths, and utility functions used across
   both the main project and 3rd party build scripts for AfterNight.
   
   Key Features:
   - Centralized Qt version management (single point to update Qt version)
   - Platform-specific path configurations for different build systems
   - Common build options and link options to reduce duplication
   - Helper functions for path resolution
   
   Usage:
       local common = require "afternight-common"
       
       -- Use Qt version constant
       qtincludepath("../../Qt6/" .. common.QT_VERSION .. "/" .. common.QT_MSVC_ARCH .. "/include")
       
       -- Use path configurations
       qtincludepath(common.Paths.QT_MSVC() .. "include")
       
       -- Use common build options
       buildoptions(common.BuildOptions.GCC_ENGINE)
   
   Supported Build Systems:
   - Visual Studio 2026 (MSVC/Clang)
   - GNU Make (gmake) for Linux
   - CodeLite for Linux

   Path Strategy:
   - Visual Studio on Windows: Uses the repository-managed Qt installation
   - Linux: Standard system paths and Qt installation paths
]]

-- Version constants
local AfterNightCommon = {}

-- Qt version - update this single location to change Qt version globally
AfterNightCommon.QT_VERSION = "6.10.3"
-- Qt 6.10.x Windows desktop package is currently named msvc2022_64 (also used for VS2026)
AfterNightCommon.QT_MSVC_ARCH = "msvc2022_64"

-- Path configurations for supported build systems
AfterNightCommon.Paths = {
    -- Qt paths for different platforms
    QT_MSVC = function() 
        return "../../Qt6/" .. AfterNightCommon.QT_VERSION .. "/" .. AfterNightCommon.QT_MSVC_ARCH .. "/"
    end,
   
   QT_GCC = function()
       return "../../Qt6/" .. AfterNightCommon.QT_VERSION .. "/gcc_64/"
   end
}

-- Common build configurations
AfterNightCommon.Configs = {
   configurations = { "Release", "Debug" },
   platforms = { "x64" },
   cppdialect = "C++20",
   cdialect = "C17"
}

-- Project directories for different actions
AfterNightCommon.ProjectDirs = {
   codelite = {
       linux = "../codelite-linux"
   },
   gmake = {
       linux = "../make-linux"
   },
   vs2026 = {
       msc = "../vs2026-msvc",
       clang = "../vs2026-clang"
   }
}

-- Helper functions
function AfterNightCommon.getQtPaths(action, system)
   if action and action:find("^vs") then
       local base = AfterNightCommon.Paths.QT_MSVC()
       return {
           include = base .. "include",
           lib = base .. "lib", 
           bin = base .. "bin"
       }
   elseif system == "linux" then
       local base = AfterNightCommon.Paths.QT_GCC()
       return {
           include = base .. "include",
           lib = base .. "lib",
           bin = base .. "libexec"
       }
   end
   
   return nil
end

-- Common build options for different compilers
AfterNightCommon.BuildOptions = {
   GCC_COMMON = { "-fPIC" },
   GCC_ENGINE = { "-fPIC", "-mavx2", "-Wno-unknown-pragmas", "-Wno-deprecated-enum-enum-conversion", "-Wno-deprecated-declarations", "-Wno-unused-result" }
}

function AfterNightCommon.applyWarningsAsErrors()
   filter { "action:vs*" }
      fatalwarnings { "All" }
      externalwarnings "Off"

   filter { "system:linux" }
      buildoptions { "-Werror" }

   filter {}
end

-- The application manifest a product executable carries on Windows, which the toolset merges into the one it embeds:
-- it declares the process long-path aware, so a path past the old 260-character limit reaches the file system wherever
-- Windows has long paths turned on. Without it a write into a deep directory fails at the pending file it creates.
function AfterNightCommon.applyApplicationManifest()
   filter { "action:vs*" }
      files { "../../src/AfterNightPlatform/resources/AfterNight.manifest" }
   filter {}
end

-- The sanitizer generation, selected by AFTERNIGHT_SANITIZE, and applied to the engine's targets - the
-- engine, its optional adapters and the suites that link them - and to the consumer closures: the
-- platform library, the consumer runtime, the protocol, Controls and processes libraries, the
-- application, both hosts, the runtime backend, the command-line tool and every suite and fuzz target
-- built from an explicit list.
--
-- It exists because a sanitizer run has to be reproducible as a whole - which targets were
-- instrumented, with what, and against which run of which tests - and a recipe typed into a shell
-- as CXXFLAGS is none of those things. It is also the only way the campaign runner can check that
-- what it ran was instrumented at all: an uninstrumented binary passes a sanitizer run silently.
--
--   AFTERNIGHT_SANITIZE=address  the address sanitizer, and on Clang the undefined-behaviour
--                                sanitizer with it. Microsoft's compiler has no UBSan, so that
--                                half of the matrix is a Linux row.
--   AFTERNIGHT_SANITIZE=thread   ThreadSanitizer, which is a Clang arrangement only.
--   AFTERNIGHT_FUZZ=1            on Clang, the libFuzzer generation: coverage counters with the address
--                                and undefined-behaviour sanitizers in every library and suite, so the
--                                fuzz targets, which link libFuzzer itself, are guided by the code they
--                                hand an input to and not only by their own. A target instrumented in
--                                front of an ordinary library keeps a corpus of one byte and finds a
--                                defect there only when it crashes. Microsoft's toolchain has no
--                                libFuzzer, so there the switch leaves the tree as it was.
--
-- A value that is none of these stops the generator rather than producing a tree that looks
-- instrumented and is not, and so does asking for the libFuzzer generation and a sanitizer together.
-- Instrumented objects go under obj/sanitize-<name>/ so that switching a tree back to an ordinary
-- generation cannot leave one binary holding objects from two instrumentations.
AfterNightCommon.Sanitizer = (function()
   local requested = os.getenv("AFTERNIGHT_SANITIZE")
   local fuzz = os.getenv("AFTERNIGHT_FUZZ") == "1" and not os.istarget("windows")
   if fuzz then
      if requested ~= nil and requested ~= "" then
         error("AFTERNIGHT_FUZZ=1 and AFTERNIGHT_SANITIZE=" .. requested .. " name two generations; the libFuzzer generation carries the address sanitizer already, so ask for one")
      end
      return "fuzzer"
   end
   if requested == nil or requested == "" then
      return nil
   end
   if requested ~= "address" and requested ~= "thread" then
      error("AFTERNIGHT_SANITIZE=" .. requested .. " names no sanitizer this tree can be generated for; use address or thread")
   end
   return requested
end)()

-- Applies the selected instrumentation to the project currently being defined. Called last in a
-- project, because these settings deliberately override what the configuration filters above them
-- chose: link-time code generation and incremental linking are both incompatible with the address
-- sanitizer on Microsoft's toolchain.
--
-- `options.unannotatedContainers` leaves the standard library's containers unannotated in a project
-- that links a static library built without the sanitizer: Microsoft's library records whether a
-- unit annotates them and refuses to link the two kinds into one binary. Only container overflow
-- inside capacity goes unseen there; the heap and the stack are instrumented as ever. The project
-- that asks for it says why, beside the call.
function AfterNightCommon.applyEngineSanitizer(options)
   local sanitizer = AfterNightCommon.Sanitizer
   options = options or {}
   if sanitizer == nil then
      return
   end

   if sanitizer == "thread" and os.istarget("windows") then
      error("AFTERNIGHT_SANITIZE=thread has no Microsoft toolchain to build it; ThreadSanitizer is a Clang arrangement")
   end

   local objectRoot = "../../obj/sanitize-" .. sanitizer .. "/%{prj.name}/"

   filter { "action:vs*" }
      objdir(objectRoot .. AfterNightCommon.WindowsOutputDir)
      buildoptions { "/fsanitize=address" }
      editandcontinue "Off"
      linktimeoptimization "Off"
      if options.unannotatedContainers then
         defines { "_DISABLE_STL_ANNOTATION" }
      end

   -- Incremental linking and the address sanitizer are incompatible, and the librarian does not
   -- take the option at all - a static library that passed it would link with a warning saying so.
   filter { "action:vs*", "kind:not StaticLib" }
      linkoptions { "/INCREMENTAL:NO" }

   -- A static library is the one artifact where an instrumentation goes stale silently. Its objects
   -- are separated by the directory above, but the library itself would share one path with the
   -- ordinary build, and a librarian that finds the library newer than the objects it is given keeps
   -- it - so a tree switched back to an ordinary generation would go on linking the instrumented copy
   -- until something refused it. An instrumented static library is therefore a differently named file.
   filter { "kind:StaticLib" }
      targetsuffix("-sanitize-" .. sanitizer)

   filter { "system:linux" }
      objdir(objectRoot .. "clang")
      -- -fno-sanitize=vptr because the undefined-behaviour sanitizer's virtual-pointer check needs
      -- run-time type information from every library in the process, which the external ones this
      -- engine links do not all carry. -Wno-pass-failed because instrumentation prevents the
      -- vectorization hints in the geometry resampler from being honoured, and -Werror would
      -- otherwise fail the build on a codegen note rather than on a correctness signal.
      if sanitizer == "address" then
         buildoptions { "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize=vptr", "-Wno-pass-failed" }
         linkoptions { "-fsanitize=address,undefined" }
      elseif sanitizer == "fuzzer" then
         -- fuzzer-no-link puts libFuzzer's coverage counters in without its `main`, which only a fuzz
         -- target links; the counters of every library a process loads report to the one fuzzer in it.
         buildoptions { "-fsanitize=fuzzer-no-link,address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize=vptr", "-Wno-pass-failed" }
         linkoptions { "-fsanitize=address,undefined" }
      else
         buildoptions { "-fsanitize=thread", "-fno-omit-frame-pointer", "-Wno-pass-failed" }
         linkoptions { "-fsanitize=thread" }
      end

   filter {}
end

-- The Windows output tree is named after the toolchain that produced it rather than after the
-- platform. These definitions generate two Windows trees - the shipping MSVC one and the
-- clang-cl evidence tree - and one output tree between them would mean the second replacing the
-- first's binaries and objects in place. The object directory would be shared too, so a partial
-- build would leave one binary holding objects from two compilers, and because clang-cl matches
-- MSVC's name mangling that mixture links rather than failing. Two trees also let both builds
-- exist at once, which is what a comparison between them needs.
local windowsClang = (_OPTIONS["cc"] == "clang") and _ACTION ~= nil and _ACTION:find("^vs") ~= nil
AfterNightCommon.WindowsOutputDir = windowsClang and "clang-cl" or "msvc"

-- GNU Make links a binary again only when something it links is newer than it, so a changed link option alone - a run
-- path, a library directory, a sanitizer's runtime - leaves the binary linked the old way, and a package made from that
-- tree ships it. Premake rewrites a project's makefile only when its text changes, so making each binary depend on its
-- own makefile links it again exactly when its link line can have changed. Visual Studio tracks its command lines
-- itself.
local gmake = premake.modules.gmake
if gmake ~= nil and gmake.cpp ~= nil then
   premake.override(gmake.cpp, "ldDeps", function(base, cfg, toolset)
      base(cfg, toolset)
      premake.outln("LDDEPS += " .. path.getname(premake.filename(cfg.project, gmake.getmakefilename(cfg.project, true))))
   end)
end

return AfterNightCommon
