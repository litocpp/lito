local lito = require("@lito")

local resource = {}

local function append(values, value)
  values[#values + 1] = value
end

local function require_field(value, field, expected)
  local result = value[field]
  if type(result) ~= expected then
    error("qt.resource." .. field .. " must be " .. expected)
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

local function resource_prefix(value)
  if type(value) ~= "string" or value == "" or value:sub(1, 1) ~= "/" or
      value:find("\\", 1, true) or value:find("//", 1, true) or value:find("%c") then
    error("qt.resource.prefix must be a canonical absolute resource path")
  end
  for component in value:gmatch("[^/]+") do
    if component == "." or component == ".." then
      error("qt.resource.prefix must not contain '.' or '..' components")
    end
  end
  if value ~= "/" and value:sub(-1) == "/" then
    return value:sub(1, -2)
  end
  return value
end

local function resource_name(value)
  if type(value) ~= "string" or not value:match("^[A-Za-z_][A-Za-z0-9_]*$") then
    error("qt.resource.name must be a C identifier")
  end
  return value
end

local function xml_escape(value)
  value = value:gsub("&", "&amp;")
  value = value:gsub("<", "&lt;")
  value = value:gsub(">", "&gt;")
  value = value:gsub("\"", "&quot;")
  return value:gsub("'", "&apos;")
end

function resource.compile(request)
  if type(request) ~= "table" then
    error("qt.resource request must be a table")
  end
  local target = require_field(request, "target", "userdata")
  local qt = require_field(request, "qt", "userdata")
  local name = resource_name(require_field(request, "name", "string"))
  local prefix = resource_prefix(require_field(request, "prefix", "string"))
  local files = require_field(request, "files", "table")
  if #files == 0 then
    error("qt.resource.files must not be empty")
  end
  local qrc_output = safe_path(require_field(request, "qrc_output", "string"),
                               "qt.resource.qrc_output")
  local cpp_output = safe_path(require_field(request, "cpp_output", "string"),
                               "qt.resource.cpp_output")

  local inputs = {}
  local aliases = {}
  local content = "<RCC>\n  <qresource prefix=\"" .. xml_escape(prefix) .. "\">\n"
  for index, file in ipairs(files) do
    if type(file) ~= "table" then
      error("qt.resource.files[" .. index .. "] must be a table")
    end
    local alias = safe_path(file.alias, "qt.resource.files[" .. index .. "].alias")
    if aliases[alias] then
      error("qt.resource alias '" .. alias .. "' is declared more than once")
    end
    if type(file.input) ~= "string" and type(file.input) ~= "userdata" then
      error("qt.resource.files[" .. index .. "].input must be a source or generated output")
    end
    aliases[alias] = true
    append(inputs, file.input)
    content = content .. "    <file alias=\"" .. xml_escape(alias) ..
        "\">@INPUT_XML:" .. index .. "@</file>\n"
  end
  content = content .. "  </qresource>\n</RCC>\n"

  local qrc = lito.write({
    output = qrc_output,
    content = content,
    inputs = inputs,
  }).output
  local cpp = lito.run({
    tool = lito.external_tool(qt, "rcc"),
    cwd = ".",
    args = { "--output", "@OUTPUT@", "--name", name, "@INPUT:1@" },
    inputs = { qrc },
    outputs = { cpp_output },
  }).outputs[1]
  lito.target_add_resource(target, qrc)
  lito.target_add_generated_source(target, cpp)
  return { qrc = qrc, cpp = cpp }
end

return resource
