local lito = require("@lito")

local moc = {}

local function append(values, value)
  values[#values + 1] = value
end

local function require_field(value, field, expected)
  local result = value[field]
  if type(result) ~= expected then
    error("qt.moc." .. field .. " must be " .. expected)
  end
  return result
end

local function append_preprocessor_arguments(arguments, environment)
  append(arguments, "--compiler-flavor")
  append(arguments, environment.compiler_flavor)
  for _, value in ipairs(environment.definitions) do
    append(arguments, "-D" .. value)
  end
  for _, value in ipairs(environment.undefinitions) do
    append(arguments, "-U" .. value)
  end
  for _, value in ipairs(environment.include_directories) do
    append(arguments, "-I")
    append(arguments, value)
  end
  for _, value in ipairs(environment.system_include_directories) do
    append(arguments, "-I")
    append(arguments, value)
  end
end

local function append_plugin_metadata(arguments, metadata)
  if metadata == nil then
    return
  end
  if type(metadata) ~= "table" then
    error("qt.moc.plugin_metadata must be table")
  end

  local keys = {}
  for key, value in pairs(metadata) do
    if type(key) ~= "string" or key == "" or key:find("=", 1, true) ~= nil or
        key:find("%c") ~= nil then
      error("qt.moc.plugin_metadata keys must be non-empty strings without '=' or control characters")
    end
    if type(value) ~= "string" or value:find("%c") ~= nil then
      error("qt.moc.plugin_metadata." .. key .. " must be string without control characters")
    end
    append(keys, key)
  end
  table.sort(keys)
  for _, key in ipairs(keys) do
    append(arguments, "-M" .. key .. "=" .. metadata[key])
  end
end

local function depfile_roots(environment)
  local result = {}
  for _, value in ipairs(environment.include_directories) do
    append(result, value)
  end
  for _, value in ipairs(environment.system_include_directories) do
    append(result, value)
  end
  return result
end

local function output_marker(index, count)
  if count == 1 then
    return "@OUTPUT@"
  end
  return "@OUTPUT:" .. index .. "@"
end

local function generate_one(request, tool, environment, entry)
  local source = entry.source
  if type(source) ~= "string" and type(source) ~= "userdata" then
    error("qt.moc.source must be string or a generated output")
  end
  local mode = require_field(entry, "mode", "string")
  local output = require_field(entry, "output", "string")
  local moc_output = output
  if mode == "module-split" then
    moc_output = output .. ".moc"
  end

  local outputs = { moc_output }
  local json_output = nil
  local depfile_output = nil
  if entry.output_json == true then
    json_output = moc_output .. ".json"
    append(outputs, json_output)
  end
  if entry.depfile ~= false then
    depfile_output = moc_output .. ".d"
    append(outputs, depfile_output)
  end

  local arguments = {}
  append_preprocessor_arguments(arguments, environment)
  append_plugin_metadata(arguments, entry.plugin_metadata)
  if mode == "include" or mode == "module-split" then
    append(arguments, "-i")
  elseif mode ~= "separate" then
    error("qt.moc mode must be 'separate', 'include', or 'module-split'")
  else
    append(arguments, "-f")
    append(arguments, "@INPUT:1@")
  end
  append(arguments, "-o")
  append(arguments, output_marker(1, #outputs))
  if json_output ~= nil then
    append(arguments, "--output-json")
  end
  if depfile_output ~= nil then
    append(arguments, "--output-dep-file")
    append(arguments, "--dep-file-path")
    append(arguments, output_marker(#outputs, #outputs))
  end
  append(arguments, "@INPUT:1@")

  local result = lito.run({
    tool = tool,
    cwd = request.cwd or ".",
    args = arguments,
    inputs = { source },
    outputs = outputs,
    depfile = depfile_output ~= nil and {
      output = #outputs,
      roots = depfile_roots(environment),
    } or nil,
  })
  local cpp = result.outputs[1]

  if mode == "module-split" then
    local transformed = lito.transform({
      kind = "cpp-leading-preamble",
      input = cpp,
      outputs = { output .. ".moc.h", output .. ".moc.cpp" },
    })
    cpp = transformed.outputs[2]
  elseif mode == "separate" and entry.compile ~= false then
    lito.target_add_generated_source(request.target, cpp)
  end

  return {
    cpp = cpp,
    json = json_output ~= nil and result.outputs[2] or nil,
    depfile = depfile_output ~= nil and result.outputs[#result.outputs] or nil,
  }
end

function moc.generate(request)
  require_field(request, "target", "userdata")
  local qt = require_field(request, "qt", "userdata")
  local files = require_field(request, "files", "table")
  local tool = lito.external_tool(qt, "moc")
  local environment = lito.target_preprocessor_environment(request.target)
  local results = {}
  for _, entry in ipairs(files) do
    append(results, generate_one(request, tool, environment, entry))
  end
  if request.generated_include ~= false then
    lito.target_add_generated_include(request.target, request.generated_include or ".")
  end
  return results
end

return moc
