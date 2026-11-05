--[[
    AfterNight target registry, as the generator reads it

    build/scripts/tools/consumer_targets.txt states, for every project these definitions generate, whether it
    reaches the engine and - for an isolated test executable - which explicit source list in
    build/scripts/tools/isolated_sources.txt it compiles, so the generator, not a person, decides what is
    generated, and it decides from the registry.

    Three things happen here:

    - the registry and the source lists are read, and a file this module cannot read stops the generator;
    - AFTERNIGHT_PRODUCT_ENGINE is refused. Whether a target reaches the engine is stated per target in the
      registry, and engineClass() is the only place the generator reads it; no environment flag sets it for every
      source file at once;
    - apply(), called once every workspace is defined, refuses a project the registry does not declare.

    build/scripts/tools/consumer_targets.py holds the generated projects to the same registry; this module is
    what makes them agree in the first place.
]]

local Targets = {}

local REGISTRY = path.join(_MAIN_SCRIPT_DIR, "../scripts/tools/consumer_targets.txt")
local SOURCE_LISTS = path.join(_MAIN_SCRIPT_DIR, "../scripts/tools/isolated_sources.txt")

-- Reads a file in the registries' shared format: a record opens with "<keyword> <name>" at the start of a line,
-- each field is an indented "<field> <value>" line, a line indented further continues the field before it, and
-- comments and blank lines are ignored. Returns the records in file order, each { name, fields = { field = { values } } }.
local function readRecords(file, keyword)
    local handle = io.open(file, "r")
    if handle == nil then
        error(file .. " does not exist; the generator reads the target registry and cannot run without it")
    end
    handle:close()

    local records = {}
    local current = nil
    local lastField = nil
    local fieldIndent = 0
    local number = 0
    for raw in io.lines(file) do
        number = number + 1
        local line = raw:gsub("\r$", "")
        if line:match("^%s*$") == nil and line:match("^%s*#") == nil then
            local indent = #line:match("^(%s*)")
            if indent == 0 then
                local word, name, rest = line:match("^(%S+)%s+(%S+)%s*(.*)$")
                if word ~= keyword or name == nil or rest ~= "" then
                    error(file .. ":" .. number .. ": expected \"" .. keyword .. " <name>\"")
                end
                current = { name = name, fields = {} }
                table.insert(records, current)
                lastField = nil
            elseif current == nil then
                error(file .. ":" .. number .. ": a field outside any record")
            elseif lastField ~= nil and indent > fieldIndent then
                local values = current.fields[lastField]
                values[#values] = values[#values] .. " " .. line:match("^%s*(.-)%s*$")
            else
                local field, value = line:match("^%s*(%S+)%s*(.-)%s*$")
                current.fields[field] = current.fields[field] or {}
                table.insert(current.fields[field], value)
                lastField = field
                fieldIndent = indent
            end
        end
    end
    return records
end

local function splitList(value)
    local items = {}
    if value == nil or value == "-" then
        return items
    end
    for item in value:gmatch("[^,]+") do
        local trimmed = item:match("^%s*(.-)%s*$")
        if trimmed ~= "" then
            table.insert(items, trimmed)
        end
    end
    return items
end

local registry = {}
for _, record in ipairs(readRecords(REGISTRY, "target")) do
    if registry[record.name] ~= nil then
        error(REGISTRY .. ": " .. record.name .. " is declared twice")
    end
    local function field(name)
        local values = record.fields[name]
        return values and values[1] or "-"
    end
    registry[record.name] = {
        workspace = field("workspace"),
        engineClass = field("class"),
        state = field("state"),
        depends = splitList(field("depends")),
        sources = field("sources"),
    }
end

local sourceLists = {}
for _, record in ipairs(readRecords(SOURCE_LISTS, "sources")) do
    local list = { units = {}, linuxUnits = {}, windowsUnits = {}, headers = {}, stubs = {}, includes = {}, defines = {},
                   links = {}, externals = {}, qt = {} }
    for _, value in ipairs(record.fields["unit"] or {}) do
        local file, only = value:match("^(%S+)%s*(.*)$")
        if only == "only=linux" then
            table.insert(list.linuxUnits, file)
        elseif only == "only=windows" then
            table.insert(list.windowsUnits, file)
        elseif only == "" then
            table.insert(list.units, file)
        else
            error(SOURCE_LISTS .. ": " .. record.name .. ": " .. file .. " carries " .. only)
        end
    end
    for _, value in ipairs(record.fields["header"] or {}) do
        table.insert(list.headers, value)
    end
    for _, value in ipairs(record.fields["stub"] or {}) do
        table.insert(list.stubs, (value:match("^(%S+)")))
    end
    for _, value in ipairs(record.fields["include"] or {}) do
        table.insert(list.includes, value)
    end
    for _, value in ipairs(record.fields["define"] or {}) do
        table.insert(list.defines, value)
    end
    for _, value in ipairs(record.fields["link"] or {}) do
        table.insert(list.links, value)
    end
    for _, value in ipairs(record.fields["external"] or {}) do
        table.insert(list.externals, value)
    end
    for _, value in ipairs(record.fields["qt"] or {}) do
        for module in value:gmatch("%S+") do
            table.insert(list.qt, module)
        end
    end
    sourceLists[record.name] = list
end

if os.getenv("AFTERNIGHT_PRODUCT_ENGINE") ~= nil then
    error("AFTERNIGHT_PRODUCT_ENGINE is set; whether a target reaches the engine is stated per target in " ..
          "build/scripts/tools/consumer_targets.txt, and no environment flag overrides it")
end

local function entryOf(name)
    local entry = registry[name]
    if entry == nil then
        error(name .. " is not declared in build/scripts/tools/consumer_targets.txt; declare it in the change that adds it")
    end
    return entry
end

-- The class one target's registry record names: "none", or "engine" when it reaches the engine.
function Targets.engineClass(name)
    return entryOf(name).engineClass
end

-- The explicit source list an isolated target compiles, with every path relative to the repository root.
function Targets.sourceList(name)
    local entry = entryOf(name)
    local listName = entry.sources:match("^@(%S+)$")
    if listName == nil then
        error(name .. " compiles source roots, not an explicit list; only an isolated target is generated from a list")
    end
    local list = sourceLists[listName]
    if list == nil then
        error(name .. " names the source list " .. listName .. ", which build/scripts/tools/isolated_sources.txt does not declare")
    end
    return list
end

-- Called once every workspace is defined: holds the definitions to the registry.
function Targets.apply()
    for wks in premake.global.eachWorkspace() do
        for _, prj in ipairs(wks.projects) do
            entryOf(prj.name)
        end
    end
end

return Targets
