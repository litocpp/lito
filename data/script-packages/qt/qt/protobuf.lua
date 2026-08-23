local lito = require("@lito")
local moc = require("qt/moc.lua")
local qml = require("qt/qml.lua")

local protobuf = {}

local function append(values, value)
  values[#values + 1] = value
end

local function require_field(value, field, expected)
  local result = value[field]
  if type(result) ~= expected then
    error("qt.protobuf." .. field .. " must be " .. expected)
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

local function source_path(value, context)
  if value == "." then
    return value
  end
  return safe_path(value, context)
end

local function contains(values, expected)
  for _, value in ipairs(values) do
    if value == expected then
      return true
    end
  end
  return false
end

local function module_name(value)
  if type(value) ~= "string" or value == "" or value:sub(1, 1) == "." or
      value:sub(-1) == "." or value:find("..", 1, true) then
    return false
  end
  for segment in value:gmatch("[^.]+") do
    if not segment:match("^[A-Za-z_][A-Za-z0-9_]*$") then
      return false
    end
  end
  return true
end

local function checked_proto_files(values)
  if #values == 0 then
    error("qt.protobuf.proto_files must not be empty")
  end
  local result = {}
  local basenames = {}
  for index, value in ipairs(values) do
    local path = safe_path(value, "qt.protobuf.proto_files[" .. index .. "]")
    local basename = path:match("([^/]+)%.proto$")
    if basename == nil or basename == "" then
      error("qt.protobuf.proto_files[" .. index .. "] must name a .proto file")
    end
    if basenames[basename] then
      error("qt.protobuf proto basename '" .. basename .. "' is declared more than once")
    end
    basenames[basename] = true
    append(result, { path = path, basename = basename })
  end
  return result
end

local function checked_proto_includes(values)
  local result = {}
  for index, value in ipairs(values) do
    append(result, source_path(value, "qt.protobuf.proto_includes[" .. index .. "]"))
  end
  if #result == 0 then
    append(result, ".")
  end
  return result
end

local function include_argument(path)
  if path == "." then
    return "-I@INPUT_ROOT:1@"
  end
  return "-I@INPUT_ROOT:1@/" .. path
end

function protobuf.generate(request)
  if type(request) ~= "table" then
    error("qt.protobuf request must be a table")
  end
  local target = require_field(request, "target", "userdata")
  local qt = require_field(request, "qt", "userdata")
  local source = require_field(request, "source", "table")
  if type(source.file) ~= "function" then
    error("qt.protobuf.source must be an external source object")
  end
  local files = checked_proto_files(require_field(request, "proto_files", "table"))
  local includes = checked_proto_includes(request.proto_includes or {})
  local output = safe_path(request.output or "lito-protobuf", "qt.protobuf.output")
  if request.qml_uri ~= nil and not module_name(request.qml_uri) then
    error("qt.protobuf.qml_uri must be a dotted QML module name")
  end
  if request.qml_version ~= nil and
      (type(request.qml_version) ~= "string" or
       not request.qml_version:match("^%d+%.%d+$")) then
    error("qt.protobuf.qml_version must be major.minor")
  end
  local information = lito.external_dependency_info(qt)
  if information.provider ~= "cmake" or not information.version:match("^6%.11%.") then
    error("qt.protobuf currently requires a Qt 6.11 CMake dependency")
  end
  if not contains(information.targets, "Qt6::Protobuf") then
    error("qt.protobuf requires the Qt6::Protobuf target")
  end
  if request.qml_uri ~= nil and not contains(information.targets, "Qt6::ProtobufQuick") then
    error("qt.protobuf.qml_uri requires the Qt6::ProtobufQuick target")
  end

  local protoc = lito.external_tool(qt, "protoc")
  local generator = lito.external_tool(qt, "qtprotobufgen")
  local generated = {}
  local moc_files = {}
  lito.target_add_generated_include(target, output)
  lito.target_add_generated_definition(target, "QT_USE_PROTOBUF_LIST_ALIASES")

  for _, file in ipairs(files) do
    local prefix = output .. "/" .. file.basename
    local outputs = {
      prefix .. ".qpb.h",
      prefix .. ".qpb.cpp",
      prefix .. "_qtprotoreg.cpp",
      prefix .. ".d",
    }
    local arguments = {
      "--plugin=protoc-gen-qtprotobufgen=@TOOL:1@",
      "--qtprotobufgen_out=.",
    }
    if request.qml_uri ~= nil then
      append(arguments, "--qtprotobufgen_opt=QML;QML=true")
    end
    append(arguments, "--dependency_out=@OUTPUT:4@")
    for _, include in ipairs(includes) do
      append(arguments, include_argument(include))
    end
    append(arguments, "@INPUT:1@")
    local result = lito.run({
      tool = protoc,
      tools = { generator },
      input_roots = { source },
      cwd = ".",
      output_cwd = 1,
      args = arguments,
      inputs = { source.file(file.path) },
      outputs = outputs,
      depfile = { output = 4 },
    })
    lito.target_add_generated_source(target, result.outputs[2])
    lito.target_add_generated_source(target, result.outputs[3])
    append(generated, {
      proto = file.path,
      header = result.outputs[1],
      cpp = result.outputs[2],
      registrar = result.outputs[3],
      depfile = result.outputs[4],
    })
    append(moc_files, {
      source = result.outputs[1],
      mode = "include",
      output = output .. "/moc_" .. file.basename .. ".qpb.cpp",
    })
  end

  local qml_module = nil
  if request.qml_uri ~= nil then
    qml_module = qml.generate_module({
      target = target,
      qt = qt,
      uri = request.qml_uri,
      version = request.qml_version or "1.0",
      qml_files = {},
      moc_files = moc_files,
      imports = { "QtProtobuf" },
      output = output .. "/qml",
      generated_include = output,
    })
  else
    moc.generate({
      target = target,
      qt = qt,
      files = moc_files,
      generated_include = false,
    })
  end
  return { files = generated, qml = qml_module }
end

return protobuf
