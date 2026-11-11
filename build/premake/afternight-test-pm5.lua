--[[
    AfterNightTest Project Build Configuration
    
    This script configures the build for the main AfterNight Testing projects:
    - Test_EngineBasic
    
    Supported build systems:
    - Visual Studio 2026 (MSVC/Clang)
]]

local common = require "afternight-common"
local targets = require "afternight-targets"
-- The opt-in OpenCL proof backend's suite is generated with the backend or not at all, under the switch
-- afternight-pm5.lua reads. build/scripts/tools/check_gpu_backend_optionality.py reads this line in
-- both workspace files.
local openClBackendEnabled = os.getenv("AFTERNIGHT_OPENCL") == "1"

-- The engine-neutral benchmark layer is the one static library of this workspace that an
-- instrumented tree also builds, and a static library is where an instrumentation can go stale: the
-- two copies would share one output path, and the librarian keeps whichever is newer than its own
-- objects - which are in a different directory for each. So the instrumented one is a differently
-- named file, and a target that links the wrong one cannot exist. On Linux the link is by project
-- name and follows the target name on its own.
local sanitizerLibrarySuffix = common.Sanitizer and ("-sanitize-" .. common.Sanitizer) or ""
local benchCommonReleaseLib = "EngineBenchCommon" .. sanitizerLibrarySuffix .. ".lib"
local benchCommonDebugLib = "EngineBenchCommon_d" .. sanitizerLibrarySuffix .. ".lib"

-- As in afternight-pm5.lua: a generation without the opt-in removes the suite's project file an earlier
-- generation with it left behind, because Premake never deletes one.
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

workspace "AfterNightTest"

    configurations(common.Configs.configurations)
    platforms(common.Configs.platforms)
    
    cppdialect(common.Configs.cppdialect)
    cdialect(common.Configs.cdialect)
    common.applyWarningsAsErrors()
    
    startproject "Test_EngineCore"
    
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
        location(common.ProjectDirs.codelite.linux .. "/tests")
        basedir(common.ProjectDirs.codelite.linux .. "/tests")
        toolset "clang"
    
    filter {"action:gmake","system:linux"}
        location(common.ProjectDirs.gmake.linux .. "/tests")
        basedir(common.ProjectDirs.gmake.linux .. "/tests")
        toolset "clang"

    filter "action:vs*"
        if _OPTIONS["cc"] == "msc" then
            location(common.ProjectDirs.vs2026.msc .. "/tests")
            basedir(common.ProjectDirs.vs2026.msc .. "/tests")
            toolset "msc"
        elseif _OPTIONS["cc"] == "clang" then
            location(common.ProjectDirs.vs2026.clang .. "/tests")
            basedir(common.ProjectDirs.vs2026.clang .. "/tests")
            toolset "clang"
        end
        
    filter {"system:windows"}
        defines { "WIN64" }
        
    filter {"system:windows","action:vs*"}
        systemversion "latest"
    
    -- Reference the platform library, which every suite that logs or annotates crashes links, so it is
    -- listed first and those suites build after it.
if _ACTION and _ACTION:find("^vs") then
    if _OPTIONS["cc"] == "msc" then
        externalproject "AfterNightPlatform"
            location(common.ProjectDirs.vs2026.msc)
            filename "AfterNightPlatform"
            kind "SharedLib"
            language "C++"
    elseif _OPTIONS["cc"] == "clang" then
        externalproject "AfterNightPlatform"
            location(common.ProjectDirs.vs2026.clang)
            filename "AfterNightPlatform"
            kind "SharedLib"
            language "C++"
    end
end

    -- Reference the AfterNightEngine project so the suites can depend on it. Its project lives in the
    -- main workspace; the engine's direct suites and its benchmark runner link it and nothing else of
    -- AfterNight's, which build/scripts/tools/check_build_graph.py enforces.
if _ACTION and _ACTION:find("^vs") then
    if _OPTIONS["cc"] == "msc" then
        externalproject "AfterNightEngine"
            location(common.ProjectDirs.vs2026.msc)
            filename "AfterNightEngine"
            kind "SharedLib"
            language "C++"
    elseif _OPTIONS["cc"] == "clang" then
        externalproject "AfterNightEngine"
            location(common.ProjectDirs.vs2026.clang)
            filename "AfterNightEngine"
            kind "SharedLib"
            language "C++"
    end
end

    -- Reference the optional Python adapter for the same reason, and for one suite only. It is not
    -- part of the engine binary, so the suite that tests it links something of AfterNight's besides
    -- the engine.
    -- It links the engine, so this solution builds it after the engine as well: the main solution
    -- stating that edge orders a build of the main solution, not of this one, and a clean build of
    -- this one otherwise links the adapter before the engine's import library exists. Every project
    -- this solution lists from the main workspace states the edge for the same reason.
if _ACTION and _ACTION:find("^vs") then
    if _OPTIONS["cc"] == "msc" then
        externalproject "AfterNightEnginePythonAdapter"
            location(common.ProjectDirs.vs2026.msc)
            filename "AfterNightEnginePythonAdapter"
            kind "SharedLib"
            language "C++"
            dependson { "AfterNightEngine" }
    elseif _OPTIONS["cc"] == "clang" then
        externalproject "AfterNightEnginePythonAdapter"
            location(common.ProjectDirs.vs2026.clang)
            filename "AfterNightEnginePythonAdapter"
            kind "SharedLib"
            language "C++"
            dependson { "AfterNightEngine" }
    end
end

    -- The optional G'MIC adapter, referenced on the same terms: for its own suite, and for the isolated suite of the
    -- processes that run a G'MIC command through it.
if _ACTION and _ACTION:find("^vs") then
    if _OPTIONS["cc"] == "msc" then
        externalproject "AfterNightEngineGmicAdapter"
            location(common.ProjectDirs.vs2026.msc)
            filename "AfterNightEngineGmicAdapter"
            kind "SharedLib"
            language "C++"
            dependson { "AfterNightEngine" }
    elseif _OPTIONS["cc"] == "clang" then
        externalproject "AfterNightEngineGmicAdapter"
            location(common.ProjectDirs.vs2026.clang)
            filename "AfterNightEngineGmicAdapter"
            kind "SharedLib"
            language "C++"
            dependson { "AfterNightEngine" }
    end
end

    -- The consumer runtime, referenced for its own suite and for the engine's fixture adapter, which runs on it.
if _ACTION and _ACTION:find("^vs") then
    if _OPTIONS["cc"] == "msc" then
        externalproject "AfterNightConsumerRuntime"
            location(common.ProjectDirs.vs2026.msc)
            filename "AfterNightConsumerRuntime"
            kind "SharedLib"
            language "C++"
            dependson { "AfterNightEngine", "AfterNightPlatform" }
    elseif _OPTIONS["cc"] == "clang" then
        externalproject "AfterNightConsumerRuntime"
            location(common.ProjectDirs.vs2026.clang)
            filename "AfterNightConsumerRuntime"
            kind "SharedLib"
            language "C++"
            dependson { "AfterNightEngine", "AfterNightPlatform" }
    end
end

    -- The command-line tool, referenced so its suite builds after it: the suite runs the tool's binary beside it.
if _ACTION and _ACTION:find("^vs") then
    if _OPTIONS["cc"] == "msc" then
        externalproject "AfterNightCLI"
            location(common.ProjectDirs.vs2026.msc)
            filename "AfterNightCLI"
            kind "ConsoleApp"
            language "C++"
            dependson { "AfterNightConsumerRuntime", "AfterNightEngine", "AfterNightPlatform" }
    elseif _OPTIONS["cc"] == "clang" then
        externalproject "AfterNightCLI"
            location(common.ProjectDirs.vs2026.clang)
            filename "AfterNightCLI"
            kind "ConsoleApp"
            language "C++"
            dependson { "AfterNightConsumerRuntime", "AfterNightEngine", "AfterNightPlatform" }
    end
end

    -- The opt-in OpenCL proof backend, referenced for its one suite and only in a tree that asked for it.
if openClBackendEnabled and _ACTION and _ACTION:find("^vs") then
    if _OPTIONS["cc"] == "msc" then
        externalproject "AfterNightEngineOpenClBackend"
            location(common.ProjectDirs.vs2026.msc)
            filename "AfterNightEngineOpenClBackend"
            kind "SharedLib"
            language "C++"
            dependson { "AfterNightEngine" }
    elseif _OPTIONS["cc"] == "clang" then
        externalproject "AfterNightEngineOpenClBackend"
            location(common.ProjectDirs.vs2026.clang)
            filename "AfterNightEngineOpenClBackend"
            kind "SharedLib"
            language "C++"
            dependson { "AfterNightEngine" }
    end
end

    filter {} -- Clear all filters


    ----------------------------------------------------------------------------------
    -- Isolated test executables
    --
    -- An isolated executable compiles exactly the source list build/scripts/tools/isolated_sources.txt names
    -- for it - no directory glob, and none of the files the consumer suites' factory below adds to every suite -
    -- includes exactly the directories that list names, and links the engine exactly when its record in
    -- build/scripts/tools/consumer_targets.txt gives it the engine's class. What one compiles is what its list says
    -- and nothing more, so a suite proves the closure it names; build/scripts/tools/consumer_targets.py holds the
    -- generated project to both files.
    ----------------------------------------------------------------------------------

    local function repositoryPaths(entries)
        local paths = {}
        for _, entry in ipairs(entries) do
            table.insert(paths, "../../" .. entry)
        end
        return paths
    end

    -- What a class adds beside the list: the engine's include directories and its link line for class engine. Class
    -- none reaches no engine and adds nothing, which is what makes an engine-neutral executable neutral.
    local isolatedClasses = {
        none = {
            includes = {},
            windows_release = {}, windows_debug = {}, linux_release = {}, linux_debug = {},
            dependson = {}, linux_libdirs = {}, windows_linkoptions = {}
        },
        engine = {
            includes = { "../../src/AfterNightEngine/include" },
            windows_release = { "AfterNightEngine.lib" },
            windows_debug = { "AfterNightEngine_d.lib" },
            linux_release = { "-lAfterNightEngine" },
            linux_debug = { "-lAfterNightEngine_d" },
            dependson = { "AfterNightEngine" },
            linux_libdirs = {},
            windows_linkoptions = {}
        }
    }

    -- Where the docking framework's install tree keeps its library. GNU ld finds the libraries a linked shared library
    -- needs through -rpath-link or that library's runpath, never through -L, and the Controls library needs the
    -- framework whether or not a suite that links it names it; the runtime directory holds a copy only once deployed.
    local kddockwidgetsLinuxLibdir =
        path.getabsolute(path.join(_MAIN_SCRIPT_DIR, "../../kddockwidgets/build-clang-install/lib"))

    -- What a list's `link` entry adds: a registry library beyond the class's engine. A library the class already links
    -- adds nothing, and one this table does not know stops the generator.
    local isolatedLinks = {
        AfterNightPlatform = {
            windows_release = { "AfterNightPlatform.lib" }, windows_debug = { "AfterNightPlatform_d.lib" },
            linux_release = { "-lAfterNightPlatform" }, linux_debug = { "-lAfterNightPlatform_d" },
            dependson = { "AfterNightPlatform" }
        },
        AfterNightConsumerRuntime = {
            windows_release = { "AfterNightConsumerRuntime.lib" }, windows_debug = { "AfterNightConsumerRuntime_d.lib" },
            linux_release = { "-lAfterNightConsumerRuntime" }, linux_debug = { "-lAfterNightConsumerRuntime_d" },
            dependson = { "AfterNightConsumerRuntime" }
        },
        -- The engine-free protocol library: the shared-buffer descriptors, their validation and the file mapper the
        -- extension buffer seam translates through.
        AfterNightExtensionProtocol = {
            windows_release = { "AfterNightExtensionProtocol.lib" },
            windows_debug = { "AfterNightExtensionProtocol_d.lib" },
            linux_release = { "-lAfterNightExtensionProtocol" }, linux_debug = { "-lAfterNightExtensionProtocol_d" },
            dependson = { "AfterNightExtensionProtocol" }
        },
        -- The Controls library: a suite that links it compiles its own cases against the library's binary instead of
        -- compiling the library's sources into itself.
        AfterNightControls = {
            windows_release = { "AfterNightControls.lib" }, windows_debug = { "AfterNightControls_d.lib" },
            linux_release = { "-lAfterNightControls", "-Wl,-rpath-link," .. kddockwidgetsLinuxLibdir },
            linux_debug = { "-lAfterNightControls_d", "-Wl,-rpath-link," .. kddockwidgetsLinuxLibdir },
            dependson = { "AfterNightControls" }
        },
        -- The processes library: a suite that links it compiles its own cases against the library's binary instead of
        -- compiling the library's sources into itself. It links the Controls library, so GNU ld needs the docking
        -- framework's directory for the same reason.
        AfterNightProcesses = {
            windows_release = { "AfterNightProcesses.lib" }, windows_debug = { "AfterNightProcesses_d.lib" },
            linux_release = { "-lAfterNightProcesses", "-Wl,-rpath-link," .. kddockwidgetsLinuxLibdir },
            linux_debug = { "-lAfterNightProcesses_d", "-Wl,-rpath-link," .. kddockwidgetsLinuxLibdir },
            dependson = { "AfterNightProcesses" }
        },
        -- The optional G'MIC adapter, which the processes that run a G'MIC command reach; built with or without the
        -- library, as AFTERNIGHT_GMIC says.
        AfterNightEngineGmicAdapter = {
            windows_release = { "AfterNightEngineGmicAdapter.lib" },
            windows_debug = { "AfterNightEngineGmicAdapter_d.lib" },
            linux_release = { "-lAfterNightEngineGmicAdapter" },
            linux_debug = { "-lAfterNightEngineGmicAdapter_d" },
            dependson = { "AfterNightEngineGmicAdapter" }
        }
    }

    -- What a list's `external` entry adds: a third-party library that is not a registry target, with the include
    -- directory, macros and link line its installed build needs on each platform. One this table does not know stops
    -- the generator.
    local isolatedExternals = {
        -- The docking framework every dockable Controls window derives from, built into its own install trees.
        kddockwidgets = {
            windows_includes = { "../../kddockwidgets/build-msvc-install/include/kddockwidgets-qt6" },
            linux_includes = { "../../kddockwidgets/build-clang-install/include/kddockwidgets-qt6" },
            windows_libdirs = { "../../kddockwidgets/build-msvc-install/lib" },
            linux_libdirs = { "../../kddockwidgets/build-clang-install/lib" },
            windows_release = { "kddockwidgets-qt6.lib" }, windows_debug = { "kddockwidgets-qt6d.lib" },
            linux_release = { "-lkddockwidgets-qt6" }, linux_debug = { "-lkddockwidgets-qt6" },
            defines = { "KDDW_FRONTEND_QT", "KDDW_FRONTEND_QTWIDGETS" }
        },
        -- oneTBB, for a suite whose cases call it directly. GNU ld resolves a symbol only from a library named on its
        -- command line, which the engine library's own dependency does not satisfy; MSVC links it through the
        -- pragma in oneTBB's header.
        tbb = {
            linux_release = { "-ltbb" }, linux_debug = { "-ltbb" }
        },
        -- libsodium and zstd, which sign and compress the extension repository assets the extension manager's cases
        -- install, linked as the Controls library links them.
        sodium = {
            windows_release = { "libsodium.lib" }, windows_debug = { "libsodium.lib" },
            linux_release = { "-lsodium" }, linux_debug = { "-lsodium" }
        },
        zstd = {
            windows_release = { "zstd.lib" }, windows_debug = { "zstd.lib" },
            linux_release = { "-lzstd" }, linux_debug = { "-lzstd" }
        },
        -- The bundled interpreter, for a suite that starts it as the full extension host does and reads the runtime
        -- backend's session capsule through it: its headers and its import library, from the deployed runtime's trees.
        python = {
            windows_includes = { "../../python/python/tools_msvc/include" },
            linux_includes = { "../../python/python/tools_linux/include/python3.14" },
            windows_libdirs = { "../../python/python/tools_msvc/libs" },
            linux_libdirs = { "../../python/python/tools_linux/lib" },
            windows_release = { "python314.lib" }, windows_debug = { "python314.lib" },
            linux_release = { "-lpython3.14", "-ldl" }, linux_debug = { "-lpython3.14", "-ldl" }
        }
    }

    local function addIsolatedTestProject(opts)
        local list = targets.sourceList(opts.name)
        local class = isolatedClasses[targets.engineClass(opts.name)]
        local usesQt = #list.qt > 0

        local linked = {}
        for _, key in ipairs({ "windows_release", "windows_debug", "linux_release", "linux_debug", "dependson" }) do
            linked[key] = {}
            for _, entry in ipairs(class[key]) do
                table.insert(linked[key], entry)
            end
        end
        for _, name in ipairs(list.links) do
            local extra = isolatedLinks[name]
            if extra == nil then
                error(opts.name .. " links " .. name .. ", which the isolated factory does not know how to link")
            end
            for key, entries in pairs(extra) do
                for _, entry in ipairs(entries) do
                    if not table.contains(linked[key], entry) then
                        table.insert(linked[key], entry)
                    end
                end
            end
        end

        local external = {
            windows_includes = {}, linux_includes = {}, windows_libdirs = {}, linux_libdirs = {},
            windows_release = {}, windows_debug = {}, linux_release = {}, linux_debug = {}, defines = {}
        }
        for _, name in ipairs(list.externals) do
            local known = isolatedExternals[name]
            if known == nil then
                error(opts.name .. " names the external library " .. name .. ", which the isolated factory does not know")
            end
            for key, entries in pairs(known) do
                for _, entry in ipairs(entries) do
                    table.insert(external[key], entry)
                end
            end
        end

        local function configured(entries, configuration)
            local expanded = {}
            for _, entry in ipairs(entries) do
                table.insert(expanded, (entry:gsub("%%s", configuration)))
            end
            return expanded
        end

        project(opts.name)

            -- Qt only for a list that names its modules: a harness executable owns a QApplication, and nothing else
            -- of Qt reaches an isolated target that did not ask for it.
            if usesQt then
                qt.enable(6)
                qtuseexternalinclude ( true )
                qtprefix "Qt6"
                qtmodules(list.qt)
            end

            kind "ConsoleApp"
            language "C++"

            defines { "AFTERNIGHT_TESTS" }
            if #list.defines > 0 then
                defines(list.defines)
            end
            if #external.defines > 0 then
                defines(external.defines)
            end

            ------------- Source code: the list, and nothing else -------------
            files(repositoryPaths(list.units))
            files(repositoryPaths(list.stubs))
            files(repositoryPaths(list.headers))
            includedirs(repositoryPaths(list.includes))
            includedirs(class.includes)

            filter {"system:linux"}
                files(repositoryPaths(list.linuxUnits))
                includedirs(external.linux_includes)
                libdirs(external.linux_libdirs)

            filter {"system:windows"}
                files(repositoryPaths(list.windowsUnits))
                includedirs(external.windows_includes)
                libdirs(external.windows_libdirs)

            filter {}

            ------------- MSVC -------------
            filter {"action:vs*"}
                if #linked.dependson > 0 then
                    dependson(linked.dependson)
                end
                buildoptions { "/Zc:__cplusplus", "/utf-8", "/FS" }
                -- A list compiles its units in parallel, as the product's projects do: the integration suite's alone is
                -- over a hundred and fifty units, which one compiler process takes most of half an hour to get through.
                multiprocessorcompile "On"
                if #class.windows_linkoptions > 0 then
                    linkoptions(class.windows_linkoptions)
                end

            filter {"action:vs*", "configurations:Debug"}
                -- A Debug object of one of the larger case files has more sections than the default format holds.
                buildoptions { "/bigobj" }
                targetdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
                debugdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
                libdirs { ("../../lib/" .. common.WindowsOutputDir), "../../vcpkg/installed/x64-windows/debug/lib", "../../vcpkg/installed/x64-windows-release/lib" }
                objdir  ("../../obj/AfterNightTests/" .. opts.name .. "/" .. common.WindowsOutputDir)
                links(linked.windows_debug)
                links(external.windows_debug)
                links { "gtest.lib" }
                disablewarnings { "4251" }

            filter {"action:vs*", "configurations:Release"}
                targetdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
                debugdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
                libdirs { ("../../lib/" .. common.WindowsOutputDir), "../../vcpkg/installed/x64-windows/lib", "../../vcpkg/installed/x64-windows-release/lib" }
                objdir  ("../../obj/AfterNightTests/" .. opts.name .. "/" .. common.WindowsOutputDir)
                links(linked.windows_release)
                links(external.windows_release)
                links { "gtest.lib" }
                disablewarnings { "4251" }

            if usesQt then
                filter {"action:vs*", "configurations:Debug"}
                    libdirs { "../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib" }
                    qtincludepath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/include")
                    qtlibpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib")
                    qtbinpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/bin")
                    qtgenerateddir ("../../obj/AfterNightTests/" .. opts.name .. "/" .. common.WindowsOutputDir)
                    qtsuffix "d"
                    postbuildcommands { "call \"$(VCInstallDir)Auxiliary\\Build\\vcvars64.bat\"", "..\\..\\..\\Qt6\\" .. common.QT_VERSION .. "\\msvc2022_64\\bin\\windeployqt $(TargetPath) --no-translations --include-plugins qminimal,qoffscreen" }

                filter {"action:vs*", "configurations:Release"}
                    libdirs { "../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib" }
                    qtincludepath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/include")
                    qtlibpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/lib")
                    qtbinpath("../../Qt6/" .. common.QT_VERSION .. "/msvc2022_64/bin")
                    qtgenerateddir ("../../obj/AfterNightTests/" .. opts.name .. "/" .. common.WindowsOutputDir)
                    postbuildcommands { "call \"$(VCInstallDir)Auxiliary\\Build\\vcvars64.bat\"", "..\\..\\..\\Qt6\\" .. common.QT_VERSION .. "\\msvc2022_64\\bin\\windeployqt $(TargetPath) --no-translations --include-plugins qminimal,qoffscreen" }

                filter {"system:linux"}
                    qtincludepath("../../Qt6/" .. common.QT_VERSION .. "/gcc_64/include")
                    qtlibpath("../../Qt6/" .. common.QT_VERSION .. "/gcc_64/lib")
                    qtbinpath("../../Qt6/" .. common.QT_VERSION .. "/gcc_64/libexec")
                    qtgenerateddir ("../../obj/AfterNightTests/" .. opts.name .. "/clang")
            end

            ------------- Clang (Linux) -------------
            filter {"system:linux"}
                objdir ("../../obj/AfterNightTests/" .. opts.name .. "/clang")

            filter {"system:linux", "configurations:Debug"}
                targetdir "../../bin/clang/debug"
                debugdir "../../bin/clang/debug"
                libdirs { "../../bin/clang/debug", "../../vcpkg/installed/x64-linux/debug/lib", "../../vcpkg/installed/x64-linux-release/lib" }
                libdirs(configured(class.linux_libdirs, "debug"))
                linkoptions(linked.linux_debug)
                linkoptions(external.linux_debug)
                linkoptions { "-lgtest" }
                defines { "_DEBUG", "DEBUG", "linux", "__linux__" }
                symbols "On"
                targetname (opts.name .. "_d")

            filter {"system:linux", "configurations:Release"}
                targetdir "../../bin/clang/release"
                debugdir "../../bin/clang/release"
                libdirs { "../../bin/clang/release", "../../vcpkg/installed/x64-linux/lib", "../../vcpkg/installed/x64-linux-release/lib" }
                libdirs(configured(class.linux_libdirs, "release"))
                linkoptions(linked.linux_release)
                linkoptions(external.linux_release)
                linkoptions { "-lgtest" }
                defines { "NDEBUG", "linux", "__linux__" }
                symbols "On"
                optimize "Speed"
                targetname (opts.name)

            ------------- Common Windows -------------
            filter {"system:windows", "configurations:Debug"}
                defines { "_DEBUG", "DEBUG", "WIN32" }
                symbols "On"
                targetname (opts.name .. "_d")

            filter {"system:windows", "configurations:Release"}
                defines { "NDEBUG", "WIN32" }
                symbols "On"
                optimize "Speed"
                targetname (opts.name)

            -- The instrumentation this tree was generated for, if any: an isolated suite is instrumented with the
            -- libraries it links, because a sanitizer only sees the code it compiled. Last, because it overrides
            -- link-time code generation and incremental linking.
            common.applyEngineSanitizer()

        filter {} -- Clear all filters
    end

    -- The system-information panel's memory parser, held by its suite against the Controls library, which links the
    -- engine.
    addIsolatedTestProject { name = "Test_SystemInformationPanel" }

    -- The Controls suites: each compiles its own cases from its list and links the Controls library instead of
    -- compiling the library's sources into itself.
    --
    -- The session bridge - the document, its writer, commit tickets and the execution scope.
    addIsolatedTestProject { name = "Test_ControlsSession" }

    -- The consumer fault campaign: the engine's fault harness swept over the product's publication and read paths,
    -- each over a session built on the sweep's injecting resource.
    addIsolatedTestProject { name = "Test_ConsumerFaults" }

    -- The consumer fuzz targets: the integration's durable documents and the extension protocol, each with the shared
    -- fuzz harness. AFTERNIGHT_FUZZ=1 regenerates them for libFuzzer as it does the engine's: libFuzzer supplies `main`,
    -- the sanitizers come with it, and the standalone driver their lists name is left out.
    local consumerFuzzInstrumented = os.getenv("AFTERNIGHT_FUZZ") == "1" and not os.istarget("windows")
    for _, consumerFuzzTarget in ipairs({ "Fuzz_ConsumerDocuments", "Fuzz_ConsumerProtocol" }) do
        addIsolatedTestProject { name = consumerFuzzTarget }
        if consumerFuzzInstrumented then
            removefiles { "../../src/AfterNightTests/FuzzEngine/FuzzDriver.cpp" }
            defines { "AFTERNIGHT_FUZZ_LIBFUZZER" }
            filter { "system:linux" }
                buildoptions { "-fsanitize=fuzzer,address,undefined", "-fno-omit-frame-pointer" }
                linkoptions { "-fsanitize=fuzzer,address,undefined" }
            filter {}
        end
    end

    -- The service interfaces - the application context, its events and listeners, the helpers processes reach it through
    -- and the summary reports.
    addIsolatedTestProject { name = "Test_ControlsServices" }

    -- The image view and what it paints - its widget, lens, mask overlay, comparison and autostretch controls and the
    -- one presentation bridge.
    addIsolatedTestProject { name = "Test_ControlsViewer" }

    -- The process framework - the process window, its real-time preview, the typed parameters, the progress dialog and
    -- the channel dispatch - over the viewer it targets.
    addIsolatedTestProject { name = "Test_ControlsProcessFramework" }

    -- The process window's and the real-time preview window's own suites.
    addIsolatedTestProject { name = "Test_ImageProcessControl" }
    addIsolatedTestProject { name = "Test_ImageRTPreviewControl" }

    -- The image history - its tracker, backup writer and snapshots, the companion the view reaches it through and the
    -- window over it.
    addIsolatedTestProject { name = "Test_ControlsHistory" }

    -- The file and image coordinators - the open and save workflows, the recent files and their thumbnails, the settings
    -- an open and a save keep, the temporary directory and the action state.
    addIsolatedTestProject { name = "Test_ControlsFiles" }

    -- The plate solve and the catalogs - the solvers, the internal solver over an installed Gaia catalog, the catalog
    -- search and download services - with the plate-solve and astrometry windows and the overlays a view draws over a
    -- solved image.
    addIsolatedTestProject { name = "Test_ControlsAstrometry" }

    -- The histogram, statistics and curve widgets, the structuring element, the star selection, filter and mask
    -- windows, and the histograms the lens and the process dialogs show.
    addIsolatedTestProject { name = "Test_ControlsStatistics" }

    -- The mask drawing tools - the brush, the shapes, the drawing document, its undo and the queue its edits land
    -- through, the canvas over a view - and the mask drawing window's controls.
    addIsolatedTestProject { name = "Test_MaskDrawing" }

    -- The sequence containers, the sequence companions, the player of a sequence view and the New Sequence and Export
    -- Sequence windows.
    addIsolatedTestProject { name = "Test_ControlsSequences" }

    -- The extension buffer seam: the shared buffer manager over assets and the host manager's request and result paths.
    addIsolatedTestProject { name = "Test_ControlsExtensionSeam" }

    -- The settings pages, the about and system information dialogs, the dashboard's file cards, the inspector windows
    -- and the opener of a view's tool windows, and the PixelMath editor.
    addIsolatedTestProject { name = "Test_ControlsDialogs" }

    -- The cases that need the Controls classes they exercise compiled into the executable: the close-during-work cases,
    -- whose freed-memory quarantine holds a class's freed blocks only when the class is compiled here, and the case that
    -- observes what the view tells its companions through registered stand-ins. Every other Controls suite links the
    -- library.
    addIsolatedTestProject { name = "Test_ControlsSeams" }

    -- The processes suites: each compiles its own cases from its list and links the processes and Controls libraries
    -- instead of compiling their sources into itself.
    --
    -- The processes library's shared helpers and the launch path every process takes - the choice maps, the combined
    -- metadata, the image-list helpers, the launcher's guards and the registry.
    addIsolatedTestProject { name = "Test_ProcessesShared" }

    -- The pointwise and tone processes and the helpers they share.
    addIsolatedTestProject { name = "Test_ProcessesPointwise" }

    -- The stretching processes - the histogram transformation, curves, generalized hyperbolic and statistical stretch
    -- dialogs.
    addIsolatedTestProject { name = "Test_ProcessesStretching" }

    -- The spatial and noise processes, each driven through its own dialog, among them the six that run a G'MIC
    -- command through the G'MIC adapter.
    addIsolatedTestProject { name = "Test_ProcessesSpatial" }

    -- The mask, star and background processes and the helpers they share.
    addIsolatedTestProject { name = "Test_ProcessesMasks" }

    -- The channel and conversion actions and the image menu's other non-interactive actions - the one table every
    -- extraction and conversion runs through, over a still image and over every frame of a sequence.
    addIsolatedTestProject { name = "Test_ProcessesChannels" }

    -- The geometry and filter-array processes - crop, resize, rotate, integer and CFA integer resample, debayer, split
    -- and merge CFA and extract sequence - and the support they share, with the channel actions' suite's support for
    -- its scenes, sequences and waits.
    addIsolatedTestProject { name = "Test_ProcessesGeometry" }

    -- The colour calibration tools - background neutralization, colour calibration, linear match and SCNR - and the
    -- support they share, with the channel actions' suite's support for its scenes, sequences and waits.
    addIsolatedTestProject { name = "Test_ProcessesColorTools" }

    -- The PixelMath window and the support it runs - the binding policy, the fused program or the evaluator per
    -- expression, the settings file - with the channel actions' and geometry suites' support for its scenes,
    -- sequences and runs.
    addIsolatedTestProject { name = "Test_ProcessesPixelMath" }

    -- The external tools - StarNet, StarNet's star mask and DeepSNR - and the support they share; the executable is
    -- also the stand-in program the product is pointed at.
    addIsolatedTestProject { name = "Test_ProcessesExternalTools" }

    -- The photometric and spectrophotometric colour calibrations, their readiness, catalog and response readers.
    addIsolatedTestProject { name = "Test_ProcessesPhotometric" }
    -- The two star profilers, the window they share and the support for their detection, enrichment, tables and
    -- documents.
    addIsolatedTestProject { name = "Test_ProcessesStarProfilers" }
    -- The standalone workflow tools - calibration, stacking, registration and the list analyzer - with the file list
    -- and the runs they share.
    addIsolatedTestProject { name = "Test_ProcessesWorkflowTools" }

    -- The combination processes - blend, LRGB, NBRGB, channel composition, the narrowband mapper and palette composer,
    -- the recombinations and the comparison - and the helpers they share.
    addIsolatedTestProject { name = "Test_ProcessesCombinations" }

    -- The integration workflow's suite - its model, frame import, workflow core, stages, persistence, reporting and
    -- windows, over frames the neutral fixture layer writes as capture programs write them - and the process
    -- framework's.
    addIsolatedTestProject { name = "Test_ImageIntegration" }
    addIsolatedTestProject { name = "Test_ProcessFramework" }

    -- The consumer measurement driver: one measured run of the product's ImageIntegration workflow over a pinned frame
    -- set, launched by build/scripts/tools/product_measurement.py from each side of a pair. It runs the workflow the
    -- processes library defines - every stage and companion, no window - in one engine session. It never ships.
    addIsolatedTestProject { name = "AfterNightWorkflowDriver" }
    common.applyApplicationManifest()

    -- The viewer's, the dashboard's, the persistence repositories', the widgets' and the theme's suites, each compiling
    -- its own cases and linking the Controls library.
    addIsolatedTestProject { name = "Test_ImageViewControl" }
    addIsolatedTestProject { name = "Test_MainDashboard" }
    addIsolatedTestProject { name = "Test_Persistence" }
    addIsolatedTestProject { name = "Test_Widgets" }
    addIsolatedTestProject { name = "Test_ThemeSystem" }

    -- The application's suite: its main window, context and session compiled from the application's sources and
    -- linked against the processes and Controls libraries, beside the cases over the settings pages, dialogs and
    -- coordinators it opens.
    addIsolatedTestProject { name = "Test_AfterNightApp" }

    -- The fixture layers: the engine-neutral layer with the Qt-only harness, and the engine's fixture adapter, each in
    -- an executable of its own so that the neutral one reaches nothing of the engine.
    addIsolatedTestProject { name = "Test_FixtureNeutral" }
    addIsolatedTestProject { name = "Test_FixtureEngine" }

    filter {} -- Clear all filters

    -- The full host's native sources - its application context, its native process window and the session it borrows
    -- from the runtime backend - compiled from their explicit list and linked against the processes and Controls
    -- libraries. It starts the bundled interpreter and loads the backend module as the host does, so it names the
    -- interpreter as an external library.
    addIsolatedTestProject { name = "Test_ExtensionHostNative" }

    filter {} -- Clear all filters

    -- Test_ExtensionRuntimeBackend: the runtime backend's driver. It loads the backend module by file name as the full
    -- host does, runs the embedded Python module through the bundled interpreter, and starts itself again for the
    -- lifecycle scenarios that need a process of their own. It links what the backend links - the consumer runtime,
    -- the engine, the platform library and the interpreter - to read the session capsule as the host does, and no Qt
    -- and no product library, so every module the backend brings into its process is the backend's.
    project "Test_ExtensionRuntimeBackend"

        kind "ConsoleApp"
        language "C++"

        defines { "AFTERNIGHT_TESTS" }

        includedirs { "../../src/AfterNightTests", "../../src/AfterNightExtensionRuntimeBackend",
                      "../../src/AfterNightConsumerRuntime/include", "../../src/AfterNightEngine/include",
                      "../../src/AfterNightPlatform/include" }
        files {
            "../../src/AfterNightTests/Test_ExtensionRuntimeBackend/**.cpp",
            "../../src/AfterNightTests/Test_ExtensionRuntimeBackend/**.h"
        }

        filter {"action:vs*"}
            dependson { "AfterNightExtensionRuntimeBackend", "AfterNightConsumerRuntime", "AfterNightEngine", "AfterNightPlatform" }
            buildoptions { "/utf-8", "/Zc:__cplusplus", "/FS", "/bigobj" }
            includedirs { "../../vcpkg/installed/x64-windows/include", "../../vcpkg/installed/x64-windows-release/include",
                          "../../python/python/tools_msvc/include" }
            disablewarnings { "4251" }
            defines { "NOMINMAX" }
            if _OPTIONS["cc"] == "clang" then
                buildoptions { "/clang:-ffp-contract=off", "-Wno-overriding-option" }
            end

        filter {"action:vs*", "configurations:Debug"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            debugdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            libdirs { ("../../lib/" .. common.WindowsOutputDir), "../../python/python/tools_msvc/libs", "../../vcpkg/installed/x64-windows/debug/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            objdir  ("../../obj/AfterNightTests/Test_ExtensionRuntimeBackend/" .. common.WindowsOutputDir)
            links { "AfterNightConsumerRuntime_d.lib", "AfterNightEngine_d.lib", "AfterNightPlatform_d.lib", "python314.lib", "gtest.lib" }
            targetname "Test_ExtensionRuntimeBackend_d"

        filter {"action:vs*", "configurations:Release"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            debugdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            libdirs { ("../../lib/" .. common.WindowsOutputDir), "../../python/python/tools_msvc/libs", "../../vcpkg/installed/x64-windows/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            objdir  ("../../obj/AfterNightTests/Test_ExtensionRuntimeBackend/" .. common.WindowsOutputDir)
            links { "AfterNightConsumerRuntime.lib", "AfterNightEngine.lib", "AfterNightPlatform.lib", "python314.lib", "gtest.lib" }
            targetname "Test_ExtensionRuntimeBackend"

        filter {"system:linux"}
            objdir  "../../obj/AfterNightTests/Test_ExtensionRuntimeBackend/clang"
            includedirs { "../../vcpkg/installed/x64-linux/include", "../../vcpkg/installed/x64-linux-release/include",
                          "../../python/python/tools_linux/include/python3.14" }
            buildoptions { "-ffp-contract=off" }

        filter {"system:linux", "configurations:Debug"}
            targetdir "../../bin/clang/debug"
            debugdir "../../bin/clang/debug"
            libdirs { "../../bin/clang/debug", "../../python/python/tools_linux/lib", "../../vcpkg/installed/x64-linux/debug/lib", "../../vcpkg/installed/x64-linux-release/lib" }
            linkoptions { "-lAfterNightConsumerRuntime_d", "-lAfterNightEngine_d", "-lAfterNightPlatform_d", "-lpython3.14", "-lgtest", "-ldl", "-Wl,-z,noexecstack" }
            defines { "_DEBUG", "DEBUG", "linux", "__linux__" }
            symbols "On"
            targetname "Test_ExtensionRuntimeBackend_d"

        filter {"system:linux", "configurations:Release"}
            targetdir "../../bin/clang/release"
            debugdir "../../bin/clang/release"
            libdirs { "../../bin/clang/release", "../../python/python/tools_linux/lib", "../../vcpkg/installed/x64-linux/lib", "../../vcpkg/installed/x64-linux-release/lib" }
            linkoptions { "-lAfterNightConsumerRuntime", "-lAfterNightEngine", "-lAfterNightPlatform", "-lpython3.14", "-lgtest", "-ldl", "-Wl,-z,noexecstack" }
            defines { "NDEBUG", "linux", "__linux__" }
            symbols "On"
            optimize "Speed"
            targetname "Test_ExtensionRuntimeBackend"

        filter {"system:windows", "configurations:Debug"}
            defines { "_DEBUG", "DEBUG", "WIN32" }
            symbols "On"

        filter {"system:windows", "configurations:Release"}
            defines { "NDEBUG", "WIN32" }
            symbols "On"
            optimize "Speed"

    filter {} -- Clear all filters


    filter {} -- Clear all filters

        -- The instrumentation this tree was generated for, if any: the consumer closures are instrumented with the
        -- engine they run on, so a sanitizer run over a consumer suite covers the libraries it exercises. Last in the
        -- project, because it overrides link-time code generation and incremental linking.
        common.applyEngineSanitizer()
        filter {}

    ----------------------------------------------------------------------------------
    -- Benchmark system
    --
    -- EngineBenchCommon holds the scenario catalogue, the fixtures, the oracles, the harness and
    -- the result schema, and it never includes or links AfterNightEngine: an oracle that judges
    -- the engine's answer must not be computed by the engine. The absence of an engine include
    -- directory and of a dependson on the engine below is the enforcement.
    ----------------------------------------------------------------------------------

    project "EngineBenchCommon"

        kind "StaticLib"
        language "C++"

        vectorextensions "AVX2"

        includedirs { "../../src/AfterNightBench/common" }
        files {
            "../../src/AfterNightBench/common/**.cpp",
            "../../src/AfterNightBench/common/**.h"
        }

        filter {"action:vs*"}
            if _OPTIONS["cc"] == "msc" then
                buildoptions { "/utf-8", "/Zc:__cplusplus" }
            end
            includedirs { "../../vcpkg/installed/x64-windows/include", "../../vcpkg/installed/x64-windows-release/include" }

        filter {"action:vs*", "configurations:Debug"}
            targetdir ("../../lib/" .. common.WindowsOutputDir)
            objdir  ("../../obj/AfterNightBench/EngineBenchCommon/" .. common.WindowsOutputDir)
            targetname "EngineBenchCommon_d"

        filter {"action:vs*", "configurations:Release"}
            targetdir ("../../lib/" .. common.WindowsOutputDir)
            objdir  ("../../obj/AfterNightBench/EngineBenchCommon/" .. common.WindowsOutputDir)
            targetname "EngineBenchCommon"

        filter {"system:linux"}
            objdir  "../../obj/AfterNightBench/EngineBenchCommon/clang"
            includedirs { "../../vcpkg/installed/x64-linux/include", "../../vcpkg/installed/x64-linux-release/include" }

        filter {"system:linux","configurations:Debug"}
            targetdir "../../lib/clang"
            defines { "_DEBUG", "DEBUG", "linux", "__linux__" }
            symbols "On"
            targetname "EngineBenchCommon_d"

        filter {"system:linux","configurations:Release"}
            targetdir "../../lib/clang"
            defines { "NDEBUG","linux", "__linux__" }
            symbols "On"
            optimize "Speed"
            targetname "EngineBenchCommon"

        filter {"system:windows","configurations:Debug"}
            defines { "_DEBUG","DEBUG", "WIN32" }
            symbols "On"

        filter {"system:windows","configurations:Release"}
            defines { "NDEBUG", "WIN32" }
            symbols "On"
            optimize "Speed"

        -- The instrumentation this tree was generated for, if any. The benchmark layer carries it
        -- with the targets that link it, because Microsoft's address sanitizer turns the standard
        -- library's container annotations on and those annotations are part of the object's
        -- interface: an instrumented suite linking an ordinary static library of this workspace is
        -- refused by the linker rather than quietly mixed.
        common.applyEngineSanitizer()

    filter {} -- Clear all filters

    ----------------------------------------------------------------------------------
    -- The benchmark runner
    --
    -- It links EngineBenchCommon and AfterNightEngine, and nothing else of AfterNight's, which
    -- build/scripts/tools/check_build_graph.py enforces.
    ----------------------------------------------------------------------------------

    project "AfterNightEngineBench"

        kind "ConsoleApp"
        language "C++"
        -- Declared long-path aware on Windows, as every executable of the product is.
        common.applyApplicationManifest()

        vectorextensions "AVX2"

        includedirs { "../../src/AfterNightBench/common", "../../src/AfterNightEngine/include" }
        files { "../../src/AfterNightBench/engine/**.cpp", "../../src/AfterNightBench/engine/**.h" }

        -- EngineBenchCommon builds in this workspace, so this edge has to reach every
        -- generator rather than only Visual Studio. Without it a parallel make is free to
        -- link this target against a library it has not rebuilt, or has not built at all.
        dependson { "EngineBenchCommon" }

        filter {"action:vs*"}
            dependson { "AfterNightEngine" }
            if _OPTIONS["cc"] == "msc" then
                -- The adapter defines every scenario's body in one unit, and its Debug object holds more
                -- sections than the default object format allows.
                buildoptions { "/utf-8", "/Zc:__cplusplus", "/bigobj" }
            end
            includedirs { "../../vcpkg/installed/x64-windows/include", "../../vcpkg/installed/x64-windows-release/include" }

        filter {"action:vs*", "configurations:Debug"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            debugdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            libdirs { ("../../lib/" .. common.WindowsOutputDir), "../../vcpkg/installed/x64-windows/debug/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            objdir  ("../../obj/AfterNightBench/Engine/" .. common.WindowsOutputDir)
            -- FFmpeg for the shared FFV1 fixture writer only; the runner decodes through the engine.
            links { benchCommonDebugLib, "AfterNightEngine_d.lib", "avformat.lib", "avcodec.lib", "avutil.lib" }
            targetname "AfterNightEngineBench_d"

        filter {"action:vs*", "configurations:Release"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            debugdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            libdirs { ("../../lib/" .. common.WindowsOutputDir), "../../vcpkg/installed/x64-windows/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            objdir  ("../../obj/AfterNightBench/Engine/" .. common.WindowsOutputDir)
            links { benchCommonReleaseLib, "AfterNightEngine.lib", "avformat.lib", "avcodec.lib", "avutil.lib" }
            targetname "AfterNightEngineBench"

        filter {"system:linux"}
            objdir  "../../obj/AfterNightBench/Engine/clang"
            includedirs { "../../vcpkg/installed/x64-linux/include", "../../vcpkg/installed/x64-linux-release/include" }

        filter {"system:linux","configurations:Debug"}
            targetdir "../../bin/clang/debug"
            debugdir "../../bin/clang/debug"
            libdirs { "../../lib/clang", "../../bin/clang/debug" }
            links { "EngineBenchCommon" }
            linkoptions { "-lAfterNightEngine_d", "-lavformat", "-lavcodec", "-lavutil" }
            defines { "_DEBUG", "DEBUG", "linux", "__linux__" }
            symbols "On"
            targetname "AfterNightEngineBench_d"

        filter {"system:linux","configurations:Release"}
            targetdir "../../bin/clang/release"
            debugdir "../../bin/clang/release"
            libdirs { "../../lib/clang", "../../bin/clang/release" }
            links { "EngineBenchCommon" }
            linkoptions { "-lAfterNightEngine", "-lavformat", "-lavcodec", "-lavutil" }
            defines { "NDEBUG", "linux", "__linux__" }
            symbols "On"
            optimize "Speed"
            targetname "AfterNightEngineBench"

        filter {"system:windows","configurations:Debug"}
            defines { "_DEBUG","DEBUG", "WIN32" }
            symbols "On"

        filter {"system:windows","configurations:Release"}
            defines { "NDEBUG", "WIN32" }
            symbols "On"
            optimize "Speed"

        -- The instrumentation this tree was generated for, if any. The benchmark layer carries it
        -- with the targets that link it, because Microsoft's address sanitizer turns the standard
        -- library's container annotations on and those annotations are part of the object's
        -- interface: an instrumented suite linking an ordinary static library of this workspace is
        -- refused by the linker rather than quietly mixed.
        common.applyEngineSanitizer()

    filter {} -- Clear all filters




    project "Test_EngineBenchCommon"

        kind "ConsoleApp"
        language "C++"

        vectorextensions "AVX2"

        defines { "AFTERNIGHT_TESTS" }

        includedirs { "../../src/AfterNightBench/common" }
        files { "../../src/AfterNightTests/Test_EngineBenchCommon/**.cpp" }

        -- EngineBenchCommon builds in this workspace, so this edge has to reach every
        -- generator rather than only Visual Studio. Without it a parallel make is free to
        -- link this target against a library it has not rebuilt, or has not built at all.
        dependson { "EngineBenchCommon" }

        filter {"action:vs*"}
            if _OPTIONS["cc"] == "msc" then
                buildoptions { "/utf-8", "/Zc:__cplusplus" }
            end
            includedirs { "../../vcpkg/installed/x64-windows/include", "../../vcpkg/installed/x64-windows-release/include" }

        filter {"action:vs*", "configurations:Debug"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            debugdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            libdirs { ("../../lib/" .. common.WindowsOutputDir), "../../vcpkg/installed/x64-windows/debug/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            objdir  ("../../obj/AfterNightTests/Test_EngineBenchCommon/" .. common.WindowsOutputDir)
            links { benchCommonDebugLib, "gtest.lib" }
            targetname "Test_EngineBenchCommon_d"

        filter {"action:vs*", "configurations:Release"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            debugdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            libdirs { ("../../lib/" .. common.WindowsOutputDir), "../../vcpkg/installed/x64-windows/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            objdir  ("../../obj/AfterNightTests/Test_EngineBenchCommon/" .. common.WindowsOutputDir)
            links { benchCommonReleaseLib, "gtest.lib" }
            targetname "Test_EngineBenchCommon"

        filter {"system:linux"}
            objdir  "../../obj/AfterNightTests/Test_EngineBenchCommon/clang"
            includedirs { "../../vcpkg/installed/x64-linux/include", "../../vcpkg/installed/x64-linux-release/include" }

        filter {"system:linux","configurations:Debug"}
            targetdir "../../bin/clang/debug"
            libdirs { "../../lib/clang", "../../vcpkg/installed/x64-linux/debug/lib", "../../vcpkg/installed/x64-linux-release/lib" }
            links { "EngineBenchCommon" }
            linkoptions { "-lgtest" }
            defines { "_DEBUG", "DEBUG", "linux", "__linux__" }
            symbols "On"
            targetname "Test_EngineBenchCommon_d"

        filter {"system:linux","configurations:Release"}
            targetdir "../../bin/clang/release"
            libdirs { "../../lib/clang", "../../vcpkg/installed/x64-linux/lib", "../../vcpkg/installed/x64-linux-release/lib" }
            links { "EngineBenchCommon" }
            linkoptions { "-lgtest" }
            defines { "NDEBUG","linux", "__linux__" }
            symbols "On"
            optimize "Speed"
            targetname "Test_EngineBenchCommon"

        filter {"system:windows","configurations:Debug"}
            defines { "_DEBUG","DEBUG", "WIN32" }
            symbols "On"

        filter {"system:windows","configurations:Release"}
            defines { "NDEBUG", "WIN32" }
            symbols "On"
            optimize "Speed"

        -- The instrumentation this tree was generated for, if any. The benchmark layer carries it
        -- with the targets that link it, because Microsoft's address sanitizer turns the standard
        -- library's container annotations on and those annotations are part of the object's
        -- interface: an instrumented suite linking an ordinary static library of this workspace is
        -- refused by the linker rather than quietly mixed.
        common.applyEngineSanitizer()

    filter {} -- Clear all filters


    ----------------------------------------------------------------------------------
    -- Direct AfterNightEngine tests
    --
    -- A direct suite links the engine and nothing else of AfterNight's: not Controls, not
    -- Processes. Self-containment is what makes what it tests the engine and not the product built
    -- on it, and a suite that quietly reached a second first-party library would stop proving it.
    -- check_build_graph.py fails the graph when one does.
    --
    -- The public include root is the only include directory these projects get. The engine's
    -- private src tree is absent deliberately: a test that reaches into it stops being a test of
    -- the published surface and starts being a test of an implementation detail.
    --
    -- They are defined through one factory rather than copied per suite, because a direct suite has
    -- nothing distinctive to say about its own build: each is a name, a source directory, and a
    -- choice about the test framework. New domain suites are added by calling it.
    ----------------------------------------------------------------------------------

    local function addEngineTestProject(opts)
        project(opts.name)

            kind "ConsoleApp"
            language "C++"

            vectorextensions "AVX2"

            if opts.links_gtest then
                defines { "AFTERNIGHT_TESTS" }
            end

            ------------- Source code -------------
            includedirs { "../../src/AfterNightEngine/include" }
            files { "../../src/AfterNightTests/" .. opts.name .. "/**.cpp" }
            if opts.additional_includedirs then
                includedirs(opts.additional_includedirs)
            end
            if opts.additional_files then
                files(opts.additional_files)
            end

            local windows_release_links = { "AfterNightEngine.lib" }
            local windows_debug_links = { "AfterNightEngine_d.lib" }
            local linux_release_links = { "-lAfterNightEngine" }
            local linux_debug_links = { "-lAfterNightEngine_d" }
            if opts.links_gtest then
                table.insert(windows_release_links, "gtest.lib")
                table.insert(windows_debug_links, "gtest.lib")
                table.insert(linux_release_links, "-lgtest")
                table.insert(linux_debug_links, "-lgtest")
            end
            -- A suite that compares the engine against an external library links that library for
            -- its answers. The library's headers reach the suite's sources only; the engine's own
            -- include directory is the same one every other suite sees.
            for _, link in ipairs(opts.windows_release_links or {}) do
                table.insert(windows_release_links, link)
            end
            for _, link in ipairs(opts.windows_debug_links or {}) do
                table.insert(windows_debug_links, link)
            end
            for _, link in ipairs(opts.linux_links or {}) do
                table.insert(linux_release_links, link)
                table.insert(linux_debug_links, link)
            end
            -- A library whose debug build carries a different name is named per configuration
            -- instead, because the two link lines are then not the same line with one suffix.
            for _, link in ipairs(opts.linux_release_links or {}) do
                table.insert(linux_release_links, link)
            end
            for _, link in ipairs(opts.linux_debug_links or {}) do
                table.insert(linux_debug_links, link)
            end
            if opts.windows_includedirs then
                filter {"system:windows"}
                    includedirs(opts.windows_includedirs)
                filter {}
            end
            if opts.linux_includedirs then
                filter {"system:linux"}
                    includedirs(opts.linux_includedirs)
                filter {}
            end

            ------------- MSVC -------------
            filter {"action:vs*"}
                dependson { "AfterNightEngine" }
                if opts.additional_dependson then
                    dependson(opts.additional_dependson)
                end
                if _OPTIONS["cc"] == "msc" then
                    buildoptions { "/utf-8", "/Zc:__cplusplus" }
                elseif _OPTIONS["cc"] == "clang" then
                    -- Test-side reference expressions and seeded fixtures are part of the
                    -- numeric oracle. Compile them with the engine's contraction policy so a
                    -- compiler cannot fuse an expected value or an input that the engine is
                    -- required to evaluate with separate roundings.
                    buildoptions { "/clang:-ffp-contract=off", "-Wno-overriding-option" }
                end

            filter {"action:vs*", "configurations:Debug"}
                targetdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
                debugdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
                libdirs { ("../../lib/" .. common.WindowsOutputDir), "../../vcpkg/installed/x64-windows/debug/lib", "../../vcpkg/installed/x64-windows-release/lib" }
                objdir("../../obj/AfterNightTests/" .. opts.name .. "/" .. common.WindowsOutputDir)
                links(windows_debug_links)

            filter {"action:vs*", "configurations:Release"}
                targetdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
                debugdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
                libdirs { ("../../lib/" .. common.WindowsOutputDir), "../../vcpkg/installed/x64-windows/lib", "../../vcpkg/installed/x64-windows-release/lib" }
                objdir("../../obj/AfterNightTests/" .. opts.name .. "/" .. common.WindowsOutputDir)
                links(windows_release_links)

            ------------- Clang (Linux) -------------
            filter {"system:linux"}
                objdir("../../obj/AfterNightTests/" .. opts.name .. "/clang")
                -- This is the Linux spelling of the same oracle rule as the clang-cl option
                -- above; it is deliberately attached to every direct suite of the engine.
                buildoptions { "-ffp-contract=off" }

            filter {"system:linux", "configurations:Debug"}
                targetdir "../../bin/clang/debug"
                debugdir "../../bin/clang/debug"
                libdirs { "../../bin/clang/debug", "../../vcpkg/installed/x64-linux/debug/lib", "../../vcpkg/installed/x64-linux-release/lib" }
                linkoptions(linux_debug_links)
                defines { "_DEBUG", "DEBUG", "linux", "__linux__" }
                symbols "On"
                targetname(opts.name .. "_d")

            filter {"system:linux", "configurations:Release"}
                targetdir "../../bin/clang/release"
                debugdir "../../bin/clang/release"
                libdirs { "../../bin/clang/release", "../../vcpkg/installed/x64-linux/lib", "../../vcpkg/installed/x64-linux-release/lib" }
                linkoptions(linux_release_links)
                defines { "NDEBUG", "linux", "__linux__" }
                symbols "On"
                optimize "Speed"
                targetname(opts.name)

            ------------- Common Windows -------------
            filter {"system:windows", "configurations:Debug"}
                defines { "_DEBUG", "DEBUG", "WIN32" }
                symbols "On"
                targetname(opts.name .. "_d")

            filter {"system:windows", "configurations:Release"}
                defines { "NDEBUG", "WIN32" }
                symbols "On"
                optimize "Speed"
                targetname(opts.name)

            -- The instrumentation this tree was generated for, if any. Every direct suite is
            -- instrumented with the engine it links, because a sanitizer only sees the code it
            -- compiled: a suite left out would report nothing about the fixture that built the
            -- input, and a run over an uninstrumented engine would report nothing at all.
            common.applyEngineSanitizer()

            filter {} -- Clear all filters
    end

    -- The core domain. Later domains arrive with the code they cover.
    addEngineTestProject { name = "Test_EngineCore", links_gtest = true }

    -- The runtime domain: the engine context, its lifetime and its configuration. Separate from
    -- the core suite because a failure here is a lifetime rule breaking rather than a value type
    -- computing the wrong number.
    addEngineTestProject { name = "Test_EngineRuntime", links_gtest = true }

    -- The image-model domain: descriptors, storage, views, raw frames, metadata, validity and
    -- conversion. Separate from the core suite because a failure here is a descriptor claiming
    -- something untrue about an image rather than a scalar computing the wrong number.
    --
    -- It also writes its own video fixtures: the engine reads video through FFmpeg and has no encoder,
    -- so the FFmpeg source's cases encode and mux each video they read with FFmpeg's own libraries,
    -- which is the one thing this suite links beyond the engine and the test framework.
    -- The XISF suites write the forms of a unit the adapter is held to reading - compressed in
    -- subblocks, shuffled, checksummed - and read back what the adapter writes, through OpenXISF's
    -- own writer and reader rather than the code under test. The suite links the shared library the
    -- engine links, from the install tree its build script makes, whose headers are named first; on
    -- Linux by absolute path, since a link option is passed as written and this makefile lives a
    -- directory deeper than the engine's.
    addEngineTestProject { name = "Test_EngineImage", links_gtest = true,
                          windows_includedirs = { "../../openxisf/build-msvc-install/include",
                                                  "../../vcpkg/installed/x64-windows/include" },
                          linux_includedirs = { "../../openxisf/build-clang-install/include" },
                          windows_release_links = { "avformat.lib", "avcodec.lib", "avutil.lib",
                                                    "../../openxisf/build-msvc-install/lib/openxisf.lib" },
                          windows_debug_links = { "avformat.lib", "avcodec.lib", "avutil.lib",
                                                  "../../openxisf/build-msvc-install/lib/openxisfd.lib" },
                          linux_links = { "-lavformat", "-lavcodec", "-lvpx", "-lm", "-lx264", "-lpthread",
                                          "-lx265", "-lstdc++", "-lrt", "-ldl", "-lswresample", "-lavutil",
                                          "-lm", "-pthread" },
                          linux_release_links = { path.getabsolute(path.join(_MAIN_SCRIPT_DIR,
                              "../../openxisf/build-clang-install/lib/libopenxisf.so")) },
                          linux_debug_links = { path.getabsolute(path.join(_MAIN_SCRIPT_DIR,
                              "../../openxisf/build-clang-install/lib/libopenxisfd.so")) } }

    -- The high-frequency image-operation domains: statistics, histograms, arithmetic, masks and
    -- expression execution. Kept out of the image-model suite because a failure here is a
    -- computation producing the wrong answer rather than a view or descriptor describing the
    -- wrong image.
    addEngineTestProject { name = "Test_EngineImageOps", links_gtest = true }

    -- Calibration is a pipeline boundary rather than one more arithmetic primitive: it validates
    -- the light and every master together, prepares reusable normalization state, and restates the
    -- result's processing meaning. Its own suite keeps those contracts visible as the later
    -- calibration consumers arrive.
    addEngineTestProject { name = "Test_EngineCalibration", links_gtest = true }

    -- The star domain: detection, the point-spread-function fit, the frame figures a ranking reads
    -- and the masks a star list paints. Its own suite because a failure here is a star being found
    -- or measured wrongly, which is a different kind of failure from an arithmetic primitive
    -- returning the wrong number, and because the accuracy cases it carries need a planted field
    -- with known truth rather than an oracle over one operation.
    addEngineTestProject { name = "Test_EngineStars", links_gtest = true }

    -- Registration consumes the star domain but has a different failure boundary: reduction writes
    -- a derived image, while matching estimates a coordinate model and its quality. Keeping both in
    -- one suite exercises the hand-off from detected points without making detector failures look
    -- like transform failures.
    addEngineTestProject { name = "Test_EngineRegistration", links_gtest = true }

    -- The astrometry domain: catalogs and their lookups today, projection, world-coordinate
    -- models and plate solving as those arrive. Its own suite because a failure here is a claim
    -- about the sky rather than about an image - a catalog answering with the wrong object, or a
    -- coordinate model placing one in the wrong place.
    addEngineTestProject { name = "Test_EngineAstrometry", links_gtest = true }

    -- The colour-science domain: photometric and spectrophotometric calibration, and later the
    -- narrowband palettes and compositions. Its own suite because a failure here is a claim about
    -- what a star of a known colour should measure as, which is neither an image operation nor a
    -- statement about the sky.
    addEngineTestProject { name = "Test_EngineColor", links_gtest = true }

    -- The stacking domain: the request model and its compatibility table today, and the planner,
    -- accumulators, methods, rejection estimators, weighting and streaming executor as those
    -- arrive. Its own suite because a failure here is a stack being asked for something it cannot
    -- mean - a median with weights, a rejection over two frames - which is a different failure from
    -- a calibration or a normalization computing the wrong number.
    addEngineTestProject { name = "Test_EngineStacking", links_gtest = true }

    -- The external-adapter domain: the checked bridges to OpenCV, FFTW and GSL. Separate from the
    -- image suite because a failure here is a bridge being wrong about somebody else's memory
    -- model rather than this engine being wrong about an image - and because this suite proves a
    -- build property as well as a behaviour, by being buildable with none of those three libraries
    -- present.
    addEngineTestProject { name = "Test_EngineInterop", links_gtest = true }

    -- The kernel substrate: backend-neutral contracts and the scalar oracle every optimized
    -- implementation is compared against. It sees only the public surface, like every other
    -- direct suite of the engine, so the backend contract itself cannot hide in a private test seam.
    addEngineTestProject { name = "Test_EngineKernels", links_gtest = true }

    -- The public-header smoke client, built as a consumer outside this repository would build:
    -- public headers, the import library, and no test framework.
    addEngineTestProject { name = "Test_EnginePublicHeaders", links_gtest = false }

    -- The consumer-shape probes: non-shipping orchestrations written the way a consumer writes one,
    -- so that API friction is found while the surface is still cheap to reshape. They are their own
    -- suite rather than files in a domain suite because what they exercise is the shape of the
    -- whole surface rather than the answer of any one operation, and because a probe that fails
    -- should say "using this engine is awkward here" rather than "this operation is wrong".
    addEngineTestProject { name = "Test_EngineConsumerProbes", links_gtest = true }

    -- The fault and cancellation campaign: every scenario runs its subject once per place it can be
    -- made to fail, with an allocation refused, a stop requested, a read or a write refused, or an
    -- exception thrown at that place. Its own suite because what it covers is a property that spans
    -- every domain - a failure path that leaks, half-commits, deadlocks or answers with a partial
    -- result - rather than any one operation's answer, and because a campaign has a registry that
    -- has to agree with the scenarios the suite defines.
    addEngineTestProject { name = "Test_EngineFaults", links_gtest = true }

    -- The library-parity sweep: operations this engine computes itself that OpenCV or GSL also
    -- provide, run against that library on the same seeded inputs and held to a stated, explained
    -- distance. The one direct suite that links those libraries, and it links
    -- them for their answers only - nothing of the engine includes them, and the suite sees the
    -- engine's public surface exactly as every other suite does.
    addEngineTestProject { name = "Test_EngineLibraryParity", links_gtest = true,
        -- The bilinear row uses the runner's engine-neutral long-double oracle to decide which
        -- side moved when an OpenCV build differs beyond the stated single-precision bound.
        additional_includedirs = { "../../src/AfterNightBench/common" },
        additional_files = { "../../src/AfterNightBench/common/BilinearResampleOracle.cpp" },
        -- Both triplet roots, like every other project: a development machine installs x64-windows, continuous
        -- integration only x64-windows-release.
        windows_includedirs = { "../../vcpkg/installed/x64-windows/include",
                                "../../vcpkg/installed/x64-windows/include/opencv4",
                                "../../vcpkg/installed/x64-windows-release/include",
                                "../../vcpkg/installed/x64-windows-release/include/opencv4" },
        linux_includedirs = { "/usr/include/opencv4" },
        windows_release_links = { "opencv_core4.lib", "opencv_imgproc4.lib", "gsl.lib", "gslcblas.lib", "raw_r.lib" },
        windows_debug_links = { "opencv_core4d.lib", "opencv_imgproc4d.lib", "gsld.lib", "gslcblasd.lib", "raw_rd.lib" },
        linux_links = { "-lopencv_core", "-lopencv_imgproc", "-lgsl", "-lgslcblas", "-lraw_r" } }

    -- The independent RAW oracle links the same static decoder dependencies as the engine.
    filter {"system:linux", "configurations:Debug"}
        linkoptions { "-ljasperd", "-llcms2", "-lz", "-ljpeg", "-lm" }
    filter {"system:linux", "configurations:Release"}
        linkoptions { "-ljasper", "-llcms2", "-lz", "-ljpeg", "-lm" }

    filter {} -- Clear all filters

    -- The optional Python adapter. Its own suite because what it covers is not part of the engine
    -- binary: the adapter's capability report, its buffer bridge, and - when a Python runtime is
    -- present - the interpreter and global-interpreter-lock lifetimes it owns. It is the one direct
    -- suite that links a second AfterNight library, which is the shape the optional adapter has by
    -- construction, and check_build_graph.py permits that link here and nowhere else.
    addEngineTestProject { name = "Test_EnginePythonAdapter", links_gtest = true,
        additional_includedirs = { "../../src/AfterNightEnginePythonAdapter/include" },
        additional_dependson = { "AfterNightEnginePythonAdapter" },
        windows_release_links = { "AfterNightEnginePythonAdapter.lib" },
        windows_debug_links = { "AfterNightEnginePythonAdapter_d.lib" },
        linux_release_links = { "-lAfterNightEnginePythonAdapter" },
        linux_debug_links = { "-lAfterNightEnginePythonAdapter_d" } }

    -- The optional G'MIC adapter. Its own suite for the reason the Python adapter's is its own: what
    -- it covers is not part of the engine binary - the adapter's capability report, the image bridge
    -- whose whole job is to refuse a layout the library cannot address, and, when a library is
    -- present, the commands, the copies and the cancellation it owns. check_build_graph.py permits
    -- the link here and nowhere else.
    addEngineTestProject { name = "Test_EngineGmicAdapter", links_gtest = true,
        additional_includedirs = { "../../src/AfterNightEngineGmicAdapter/include" },
        additional_dependson = { "AfterNightEngineGmicAdapter" },
        windows_release_links = { "AfterNightEngineGmicAdapter.lib" },
        windows_debug_links = { "AfterNightEngineGmicAdapter_d.lib" },
        linux_release_links = { "-lAfterNightEngineGmicAdapter" },
        linux_debug_links = { "-lAfterNightEngineGmicAdapter_d" } }

    -- The opt-in OpenCL proof backend's suite, generated with the backend or not at all. What it covers
    -- is a real GPU behind the engine's device seam, reached through the checked facade exactly as the
    -- mock in Test_EngineKernels is, and every device case skips by name on a host with no OpenCL GPU.
    -- check_build_graph.py permits the link here and nowhere else.
    if openClBackendEnabled and os.istarget("windows") then
        addEngineTestProject { name = "Test_EngineOpenClBackend", links_gtest = true,
            additional_includedirs = { "../../src/AfterNightEngineOpenClBackend/include" },
            additional_dependson = { "AfterNightEngineOpenClBackend" },
            windows_release_links = { "AfterNightEngineOpenClBackend.lib" },
            windows_debug_links = { "AfterNightEngineOpenClBackend_d.lib" } }
    else
        removeOpenClBackendProjectFiles("Test_EngineOpenClBackend", "/tests")
    end

    ----------------------------------------------------------------------------------
    -- The fuzz targets
    --
    -- One executable per subject, because that is the shape a coverage-guided fuzzer requires: it
    -- supplies the program's entry point and drives one `LLVMFuzzerTestOneInput`. They are built by
    -- the same factory as the suites - a fuzz target is a test that generates its own inputs, links
    -- the same one library, and is held to the same isolation rules - with the test framework left
    -- out, because a target that could report a failure through gtest would have two ways to fail and
    -- a fuzzer only recognises one.
    --
    -- Each compiles the shared harness and, unless this tree was generated for libFuzzer, the
    -- standalone driver that replays the seeds and mutates them deterministically. That driver is
    -- what makes the campaign runnable on a toolchain with no libFuzzer, which is every MSVC build.
    --
    -- AFTERNIGHT_FUZZ=1 regenerates them instrumented instead: libFuzzer supplies `main`, the address
    -- and undefined-behaviour sanitizers come with it, and the standalone driver is left out of the
    -- build so there is exactly one entry point. It is a Clang arrangement, so a tree generated with
    -- it on MSVC leaves the targets as they were.
    ----------------------------------------------------------------------------------

    local fuzzTargets = { "Fuzz_EngineDescriptor", "Fuzz_EngineConversion", "Fuzz_EnginePixelMath", "Fuzz_EngineMetadata",
                          "Fuzz_EngineCodec", "Fuzz_EngineSequence" }
    local fuzzInstrumented = os.getenv("AFTERNIGHT_FUZZ") == "1" and not os.istarget("windows")
    for _, fuzzTarget in ipairs(fuzzTargets) do
        local fuzzSources = { "../../src/AfterNightTests/FuzzEngine/FuzzHarness.cpp" }
        if not fuzzInstrumented then
            table.insert(fuzzSources, "../../src/AfterNightTests/FuzzEngine/FuzzDriver.cpp")
        end
        addEngineTestProject { name = fuzzTarget, links_gtest = false, additional_files = fuzzSources }
        if fuzzInstrumented then
            defines { "AFTERNIGHT_FUZZ_LIBFUZZER" }
            filter { "system:linux" }
                buildoptions { "-fsanitize=fuzzer,address,undefined", "-fno-omit-frame-pointer" }
                linkoptions { "-fsanitize=fuzzer,address,undefined" }
            filter {}
        end
    end


    ----------------------------------------------------------------------------------
    -- The fixture bridge
    --
    -- The one direct suite that is not built by the factory above, because it links two things
    -- rather than one: the engine and the engine-neutral benchmark layer. That is the seam
    -- it exists to test - the benchmark layer must not know about the engine, the runner has to,
    -- and the translation between them is where a benchmark ends up measuring a different frame
    -- than the scenario described.
    --
    -- It compiles the runner's own bridge source rather than a copy, so what is tested is what
    -- AfterNightEngineBench runs. It still links nothing else of AfterNight's, which
    -- check_build_graph.py enforces by name.
    ----------------------------------------------------------------------------------

    project "Test_EngineFixtureBridge"

        kind "ConsoleApp"
        language "C++"

        vectorextensions "AVX2"

        defines { "AFTERNIGHT_TESTS" }

        includedirs { "../../src/AfterNightBench/common", "../../src/AfterNightBench/engine",
                      "../../src/AfterNightEngine/include" }
        files { "../../src/AfterNightTests/Test_EngineFixtureBridge/**.cpp",
                "../../src/AfterNightBench/engine/FixtureBridge.cpp",
                "../../src/AfterNightBench/engine/RawDumpExport.cpp" }

        -- EngineBenchCommon builds in this workspace, so this edge has to reach every
        -- generator rather than only Visual Studio. Without it a parallel make is free to
        -- link this target against a library it has not rebuilt, or has not built at all.
        dependson { "EngineBenchCommon" }

        -- The raw-dump cases run the runner beside this suite, so it is built first.
        dependson { "AfterNightEngineBench" }

        filter {"action:vs*"}
            dependson { "AfterNightEngine" }
            if _OPTIONS["cc"] == "msc" then
                buildoptions { "/utf-8", "/Zc:__cplusplus" }
            end
            includedirs { "../../vcpkg/installed/x64-windows/include", "../../vcpkg/installed/x64-windows-release/include" }

        filter {"action:vs*", "configurations:Debug"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            debugdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            libdirs { ("../../lib/" .. common.WindowsOutputDir), "../../vcpkg/installed/x64-windows/debug/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            objdir  ("../../obj/AfterNightTests/Test_EngineFixtureBridge/" .. common.WindowsOutputDir)
            links { "AfterNightEngine_d.lib", benchCommonDebugLib, "gtest.lib" }
            targetname "Test_EngineFixtureBridge_d"

        filter {"action:vs*", "configurations:Release"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            debugdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            libdirs { ("../../lib/" .. common.WindowsOutputDir), "../../vcpkg/installed/x64-windows/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            objdir  ("../../obj/AfterNightTests/Test_EngineFixtureBridge/" .. common.WindowsOutputDir)
            links { "AfterNightEngine.lib", benchCommonReleaseLib, "gtest.lib" }
            targetname "Test_EngineFixtureBridge"

        filter {"system:linux"}
            objdir  "../../obj/AfterNightTests/Test_EngineFixtureBridge/clang"
            includedirs { "../../vcpkg/installed/x64-linux/include", "../../vcpkg/installed/x64-linux-release/include" }

        filter {"system:linux", "configurations:Debug"}
            targetdir "../../bin/clang/debug"
            debugdir "../../bin/clang/debug"
            libdirs { "../../lib/clang", "../../bin/clang/debug", "../../vcpkg/installed/x64-linux/debug/lib", "../../vcpkg/installed/x64-linux-release/lib" }
            links { "EngineBenchCommon" }
            linkoptions { "-lAfterNightEngine_d", "-lgtest" }
            defines { "_DEBUG", "DEBUG", "linux", "__linux__" }
            symbols "On"
            targetname "Test_EngineFixtureBridge_d"

        filter {"system:linux", "configurations:Release"}
            targetdir "../../bin/clang/release"
            debugdir "../../bin/clang/release"
            libdirs { "../../lib/clang", "../../bin/clang/release", "../../vcpkg/installed/x64-linux/lib", "../../vcpkg/installed/x64-linux-release/lib" }
            links { "EngineBenchCommon" }
            linkoptions { "-lAfterNightEngine", "-lgtest" }
            defines { "NDEBUG", "linux", "__linux__" }
            symbols "On"
            optimize "Speed"
            targetname "Test_EngineFixtureBridge"

        filter {"system:windows", "configurations:Debug"}
            defines { "_DEBUG", "DEBUG", "WIN32" }
            symbols "On"

        filter {"system:windows", "configurations:Release"}
            defines { "NDEBUG", "WIN32" }
            symbols "On"
            optimize "Speed"

        -- The instrumentation this tree was generated for, if any, for the reason the suites
        -- above carry it: this one links the engine too.
        common.applyEngineSanitizer()

    filter {} -- Clear all filters

    -- Test_Platform: the platform library's own suite. It links that library and gtest and nothing else of
    -- AfterNight's - not the engine, no Qt - which is the property the command-line tool and the runtime
    -- backend rely on, asserted here at link time by the build graph and at run time by the suite itself.
    project "Test_Platform"

        kind "ConsoleApp"
        language "C++"

        defines { "AFTERNIGHT_TESTS" }

        includedirs { "../../src/AfterNightPlatform/include" }
        files { "../../src/AfterNightTests/Test_Platform/**.h", "../../src/AfterNightTests/Test_Platform/**.cpp" }

        filter {"action:vs*"}
            dependson { "AfterNightPlatform" }
            buildoptions { "/utf-8", "/Zc:__cplusplus" }
            includedirs { "../../vcpkg/installed/x64-windows/include", "../../vcpkg/installed/x64-windows-release/include" }
            disablewarnings { "4251" }

        filter {"action:vs*", "configurations:Debug"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            debugdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            libdirs { ("../../lib/" .. common.WindowsOutputDir), "../../vcpkg/installed/x64-windows/debug/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            objdir  ("../../obj/AfterNightTests/Test_Platform/" .. common.WindowsOutputDir)
            links { "AfterNightPlatform_d.lib", "gtest.lib" }
            targetname "Test_Platform_d"

        filter {"action:vs*", "configurations:Release"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            debugdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            libdirs { ("../../lib/" .. common.WindowsOutputDir), "../../vcpkg/installed/x64-windows/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            objdir  ("../../obj/AfterNightTests/Test_Platform/" .. common.WindowsOutputDir)
            links { "AfterNightPlatform.lib", "gtest.lib" }
            targetname "Test_Platform"

        filter {"system:linux"}
            objdir  "../../obj/AfterNightTests/Test_Platform/clang"
            includedirs { "../../vcpkg/installed/x64-linux/include", "../../vcpkg/installed/x64-linux-release/include" }

        filter {"system:linux", "configurations:Debug"}
            targetdir "../../bin/clang/debug"
            debugdir "../../bin/clang/debug"
            libdirs { "../../bin/clang/debug", "../../vcpkg/installed/x64-linux/debug/lib", "../../vcpkg/installed/x64-linux-release/lib" }
            linkoptions { "-lAfterNightPlatform_d", "-lgtest" }
            defines { "_DEBUG", "DEBUG", "linux", "__linux__" }
            symbols "On"
            targetname "Test_Platform_d"

        filter {"system:linux", "configurations:Release"}
            targetdir "../../bin/clang/release"
            debugdir "../../bin/clang/release"
            libdirs { "../../bin/clang/release", "../../vcpkg/installed/x64-linux/lib", "../../vcpkg/installed/x64-linux-release/lib" }
            linkoptions { "-lAfterNightPlatform", "-lgtest" }
            defines { "NDEBUG", "linux", "__linux__" }
            symbols "On"
            optimize "Speed"
            targetname "Test_Platform"

        filter {"system:windows", "configurations:Debug"}
            defines { "_DEBUG", "DEBUG", "WIN32" }
            symbols "On"

        filter {"system:windows", "configurations:Release"}
            defines { "NDEBUG", "WIN32" }
            symbols "On"
            optimize "Speed"

    filter {} -- Clear all filters

        -- The instrumentation this tree was generated for, if any: the consumer closures are instrumented with the
        -- engine they run on, so a sanitizer run over a consumer suite covers the libraries it exercises. Last in the
        -- project, because it overrides link-time code generation and incremental linking.
        common.applyEngineSanitizer()
        filter {}

    -- Test_ConsumerRuntime: the consumer runtime's own suite. It links that library, the engine it is built on, the
    -- platform library and gtest, and nothing else of AfterNight's - no Qt, no Python - which is the closure the
    -- command-line tool and the runtime backend rely on, asserted at link time by the build graph and at run time by
    -- the suite itself. Its images are built through the engine directly.
    project "Test_ConsumerRuntime"

        kind "ConsoleApp"
        language "C++"

        defines { "AFTERNIGHT_TESTS" }

        includedirs { "../../src/AfterNightConsumerRuntime/include", "../../src/AfterNightEngine/include",
                      "../../src/AfterNightPlatform/include" }
        files { "../../src/AfterNightTests/Test_ConsumerRuntime/**.h", "../../src/AfterNightTests/Test_ConsumerRuntime/**.cpp" }

        filter {"action:vs*"}
            dependson { "AfterNightConsumerRuntime", "AfterNightEngine", "AfterNightPlatform" }
            buildoptions { "/utf-8", "/Zc:__cplusplus" }
            includedirs { "../../vcpkg/installed/x64-windows/include", "../../vcpkg/installed/x64-windows-release/include" }
            disablewarnings { "4251" }

        filter {"action:vs*", "configurations:Debug"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            debugdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            libdirs { ("../../lib/" .. common.WindowsOutputDir), "../../vcpkg/installed/x64-windows/debug/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            objdir  ("../../obj/AfterNightTests/Test_ConsumerRuntime/" .. common.WindowsOutputDir)
            links { "AfterNightConsumerRuntime_d.lib", "AfterNightEngine_d.lib", "AfterNightPlatform_d.lib", "gtest.lib" }
            targetname "Test_ConsumerRuntime_d"

        filter {"action:vs*", "configurations:Release"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            debugdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            libdirs { ("../../lib/" .. common.WindowsOutputDir), "../../vcpkg/installed/x64-windows/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            objdir  ("../../obj/AfterNightTests/Test_ConsumerRuntime/" .. common.WindowsOutputDir)
            links { "AfterNightConsumerRuntime.lib", "AfterNightEngine.lib", "AfterNightPlatform.lib", "gtest.lib" }
            targetname "Test_ConsumerRuntime"

        filter {"system:linux"}
            objdir  "../../obj/AfterNightTests/Test_ConsumerRuntime/clang"
            includedirs { "../../vcpkg/installed/x64-linux/include", "../../vcpkg/installed/x64-linux-release/include" }

        filter {"system:linux", "configurations:Debug"}
            targetdir "../../bin/clang/debug"
            debugdir "../../bin/clang/debug"
            libdirs { "../../bin/clang/debug", "../../vcpkg/installed/x64-linux/debug/lib", "../../vcpkg/installed/x64-linux-release/lib" }
            linkoptions { "-lAfterNightConsumerRuntime_d", "-lAfterNightEngine_d", "-lAfterNightPlatform_d", "-lgtest" }
            defines { "_DEBUG", "DEBUG", "linux", "__linux__" }
            symbols "On"
            targetname "Test_ConsumerRuntime_d"

        filter {"system:linux", "configurations:Release"}
            targetdir "../../bin/clang/release"
            debugdir "../../bin/clang/release"
            libdirs { "../../bin/clang/release", "../../vcpkg/installed/x64-linux/lib", "../../vcpkg/installed/x64-linux-release/lib" }
            linkoptions { "-lAfterNightConsumerRuntime", "-lAfterNightEngine", "-lAfterNightPlatform", "-lgtest" }
            defines { "NDEBUG", "linux", "__linux__" }
            symbols "On"
            optimize "Speed"
            targetname "Test_ConsumerRuntime"

        filter {"system:windows", "configurations:Debug"}
            defines { "_DEBUG", "DEBUG", "WIN32" }
            symbols "On"

        filter {"system:windows", "configurations:Release"}
            defines { "NDEBUG", "WIN32" }
            symbols "On"
            optimize "Speed"

    filter {} -- Clear all filters

        -- The instrumentation this tree was generated for, if any: the consumer closures are instrumented with the
        -- engine they run on, so a sanitizer run over a consumer suite covers the libraries it exercises. Last in the
        -- project, because it overrides link-time code generation and incremental linking.
        common.applyEngineSanitizer()
        filter {}

    -- Test_AfterNightCLI: the command-line tool's suite. It compiles the tool's units but its entry point, with the
    -- engine-neutral fixtures and records it is held to, and runs the tool's own binary beside it, which is why it
    -- builds after the tool. It links what the tool links - the runtime, the engine and the platform library - and
    -- nothing else of AfterNight's: no Qt, no Python.
    project "Test_AfterNightCLI"

        kind "ConsoleApp"
        language "C++"

        defines { "AFTERNIGHT_TESTS" }

        includedirs { "../../src/AfterNightTests", "../../src/AfterNightCLI", "../../src/AfterNightConsumerRuntime/include",
                      "../../src/AfterNightEngine/include", "../../src/AfterNightPlatform/include" }
        files {
            "../../src/AfterNightTests/Test_AfterNightCLI/**.h",
            "../../src/AfterNightTests/Test_AfterNightCLI/**.cpp",
            "../../src/AfterNightCLI/CommandLine.cpp",
            "../../src/AfterNightCLI/ConsoleText.cpp",
            "../../src/AfterNightCLI/InterruptWatch.cpp",
            "../../src/AfterNightCLI/OneShotRun.cpp",
            "../../src/AfterNightCLI/OutputPolicy.cpp",
            "../../src/AfterNightTests/fixtures/common/FixtureCatalogue.cpp",
            "../../src/AfterNightTests/fixtures/common/FixtureDigest.cpp",
            "../../src/AfterNightTests/fixtures/common/FixtureExpectation.cpp",
            "../../src/AfterNightTests/fixtures/common/FixtureFits.cpp",
            "../../src/AfterNightTests/fixtures/common/FixtureImage.cpp",
            "../../src/AfterNightTests/fixtures/common/PresentationCases.cpp"
        }

        filter {"action:vs*"}
            dependson { "AfterNightCLI", "AfterNightConsumerRuntime", "AfterNightEngine", "AfterNightPlatform" }
            buildoptions { "/utf-8", "/Zc:__cplusplus" }
            includedirs { "../../vcpkg/installed/x64-windows/include", "../../vcpkg/installed/x64-windows-release/include" }
            disablewarnings { "4251" }
            if _OPTIONS["cc"] == "clang" then
                buildoptions { "/clang:-ffp-contract=off", "-Wno-overriding-option" }
            end

        filter {"action:vs*", "configurations:Debug"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            debugdir ("../../bin/" .. common.WindowsOutputDir .. "/debug")
            libdirs { ("../../lib/" .. common.WindowsOutputDir), "../../vcpkg/installed/x64-windows/debug/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            objdir  ("../../obj/AfterNightTests/Test_AfterNightCLI/" .. common.WindowsOutputDir)
            links { "AfterNightConsumerRuntime_d.lib", "AfterNightEngine_d.lib", "AfterNightPlatform_d.lib", "gtest.lib" }
            targetname "Test_AfterNightCLI_d"

        filter {"action:vs*", "configurations:Release"}
            targetdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            debugdir ("../../bin/" .. common.WindowsOutputDir .. "/release")
            libdirs { ("../../lib/" .. common.WindowsOutputDir), "../../vcpkg/installed/x64-windows/lib", "../../vcpkg/installed/x64-windows-release/lib" }
            objdir  ("../../obj/AfterNightTests/Test_AfterNightCLI/" .. common.WindowsOutputDir)
            links { "AfterNightConsumerRuntime.lib", "AfterNightEngine.lib", "AfterNightPlatform.lib", "gtest.lib" }
            targetname "Test_AfterNightCLI"

        filter {"system:linux"}
            objdir  "../../obj/AfterNightTests/Test_AfterNightCLI/clang"
            includedirs { "../../vcpkg/installed/x64-linux/include", "../../vcpkg/installed/x64-linux-release/include" }
            buildoptions { "-ffp-contract=off" }

        filter {"system:linux", "configurations:Debug"}
            targetdir "../../bin/clang/debug"
            debugdir "../../bin/clang/debug"
            libdirs { "../../bin/clang/debug", "../../vcpkg/installed/x64-linux/debug/lib", "../../vcpkg/installed/x64-linux-release/lib" }
            linkoptions { "-lAfterNightConsumerRuntime_d", "-lAfterNightEngine_d", "-lAfterNightPlatform_d", "-lgtest" }
            defines { "_DEBUG", "DEBUG", "linux", "__linux__" }
            symbols "On"
            targetname "Test_AfterNightCLI_d"

        filter {"system:linux", "configurations:Release"}
            targetdir "../../bin/clang/release"
            debugdir "../../bin/clang/release"
            libdirs { "../../bin/clang/release", "../../vcpkg/installed/x64-linux/lib", "../../vcpkg/installed/x64-linux-release/lib" }
            linkoptions { "-lAfterNightConsumerRuntime", "-lAfterNightEngine", "-lAfterNightPlatform", "-lgtest" }
            defines { "NDEBUG", "linux", "__linux__" }
            symbols "On"
            optimize "Speed"
            targetname "Test_AfterNightCLI"

        filter {"system:windows", "configurations:Debug"}
            defines { "_DEBUG", "DEBUG", "WIN32" }
            symbols "On"

        filter {"system:windows", "configurations:Release"}
            defines { "NDEBUG", "WIN32" }
            symbols "On"
            optimize "Speed"

    filter {} -- Clear all filters

        -- The instrumentation this tree was generated for, if any: the consumer closures are instrumented with the
        -- engine they run on, so a sanitizer run over a consumer suite covers the libraries it exercises. Last in the
        -- project, because it overrides link-time code generation and incremental linking.
        common.applyEngineSanitizer()
        filter {}
