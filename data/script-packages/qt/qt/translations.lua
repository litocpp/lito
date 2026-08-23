local lito = require("@lito")
local resource = require("qt/resource.lua")

local translations = {}

local function append(values, value)
  values[#values + 1] = value
end

local function require_field(value, field, expected)
  local result = value[field]
  if type(result) ~= expected then
    error("qt.translations." .. field .. " must be " .. expected)
  end
  return result
end

local function safe_path(value, context)
  if type(value) ~= "string" or value == "" or value:sub(1, 1) == "/" or
      value:find("\\", 1, true) or value:find("%c") then
    error(context .. " must be a safe relative path")
  end
  for component in value:gmatch("[^/]+") do
    if component == "." or component == ".." then
      error(context .. " must not contain '.' or '..' components")
    end
  end
  return value
end

local function resource_name(value)
  if type(value) ~= "string" or not value:match("^[A-Za-z_][A-Za-z0-9_]*$") then
    error("qt.translations.name must be a C identifier")
  end
  return value
end

local function checked_ts_files(values)
  if #values == 0 then
    error("qt.translations.ts_files must not be empty")
  end
  local result = {}
  local basenames = {}
  for index, value in ipairs(values) do
    local path = safe_path(value, "qt.translations.ts_files[" .. index .. "]")
    local basename = path:match("([^/]+)%.ts$")
    if basename == nil or basename == "" then
      error("qt.translations.ts_files[" .. index .. "] must name a .ts file")
    end
    if basenames[basename] then
      error("qt.translations TS basename '" .. basename .. "' is declared more than once")
    end
    basenames[basename] = true
    append(result, { path = path, basename = basename })
  end
  return result
end

function translations.generate(request)
  if type(request) ~= "table" then
    error("qt.translations request must be a table")
  end
  local target = require_field(request, "target", "userdata")
  local qt = require_field(request, "qt", "userdata")
  local name = resource_name(require_field(request, "name", "string"))
  local files = checked_ts_files(require_field(request, "ts_files", "table"))
  local output = safe_path(request.output or ("lito-translations/" .. name),
                           "qt.translations.output")
  local information = lito.external_dependency_info(qt)
  if information.provider ~= "cmake" or not information.version:match("^6%.11%.") then
    error("qt.translations currently requires a Qt 6.11 CMake dependency")
  end

  local tool = lito.external_tool(qt, "lrelease")
  local generated = {}
  local resource_files = {}
  for _, file in ipairs(files) do
    local qm = lito.run({
      tool = tool,
      cwd = ".",
      args = { "@INPUT:1@", "-qm", "@OUTPUT@" },
      inputs = { file.path },
      outputs = { output .. "/" .. file.basename .. ".qm" },
    }).outputs[1]
    append(generated, { ts = file.path, qm = qm })
    append(resource_files, { alias = file.basename .. ".qm", input = qm })
  end

  local compiled = resource.compile({
    target = target,
    qt = qt,
    name = name .. "_translations",
    prefix = request.resource_prefix or "/i18n",
    files = resource_files,
    qrc_output = output .. "/translations.qrc",
    cpp_output = output .. "/qrc_translations.cpp",
  })
  return { files = generated, resource = compiled }
end

return translations
