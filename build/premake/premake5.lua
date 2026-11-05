--[[
    AfterNight Build Configuration Entry Point
    
    This file orchestrates the build configuration for the entire AfterNight project.
    It loads necessary modules and configuration files for generating build files.
    
    Supported build systems:
    - Visual Studio 2026 (MSVC/Clang)
    - GNU Make (gmake) for Linux
    - CodeLite for Linux
]]

-- Load required modules
require "qt"

-- The target registry: whether each target reaches the engine, and whether it is generated at all
local targets = require "afternight-targets"

-- Load project configurations
dofile "afternight-3rdparty-pm5.lua"
dofile "afternight-pm5.lua"
dofile "afternight-test-pm5.lua"

-- Hold every workspace to the registry: a project it does not declare stops the generator
targets.apply()
