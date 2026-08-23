local lito = require("@lito")
local moc = require("qt/moc.lua")

local qml = {}

local function append(values, value)
  values[#values + 1] = value
end

local function require_field(value, field, expected)
  local result = value[field]
  if type(result) ~= expected then
    error("qt.qml_module." .. field .. " must be " .. expected)
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
    error("qt.qml_module.resource_prefix must be a canonical absolute resource path")
  end
  for component in value:gmatch("[^/]+") do
    if component == "." or component == ".." then
      error("qt.qml_module.resource_prefix must not contain '.' or '..' components")
    end
  end
  if value ~= "/" and value:sub(-1) == "/" then
    return value:sub(1, -2)
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

local function checked_files(values, context)
  local result = {}
  for index, value in ipairs(values) do
    append(result, safe_path(value, context .. "[" .. index .. "]"))
  end
  return result
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

local function qml_type(path)
  local name = path:match("([^/]+)%.qml$")
  if name == nil or not name:match("^[A-Z][A-Za-z0-9_]*$") then
    error("qt.qml_module QML file '" .. path .. "' must have an exported type name")
  end
  return name
end

local function output_name(path)
  return path:gsub("[^A-Za-z0-9_]", "_")
end

local function contains(values, expected)
  for _, value in ipairs(values) do
    if value == expected then
      return true
    end
  end
  return false
end

local function write_qrc(output, prefix, files)
  local inputs = {}
  local content = "<RCC>\n  <qresource prefix=\"" .. xml_escape(prefix) .. "\">\n"
  for index, file in ipairs(files) do
    append(inputs, file.input)
    content = content .. "    <file alias=\"" .. xml_escape(file.alias) ..
        "\">@INPUT_XML:" .. index .. "@</file>\n"
  end
  content = content .. "  </qresource>\n</RCC>\n"
  return lito.write({ output = output, content = content, inputs = inputs }).output
end

local function compile_qrc(target, tool, name, qrc, output)
  local generated = lito.run({
    tool = tool,
    cwd = ".",
    args = { "--output", "@OUTPUT@", "--name", name, "@INPUT:1@" },
    inputs = { qrc },
    outputs = { output },
  }).outputs[1]
  lito.target_add_resource(target, qrc)
  lito.target_add_generated_source(target, generated)
  return generated
end


local function generate_registration(request, prefix, major, minor)
  local moc_files = request.moc_files or {}
  if #moc_files == 0 then
    return nil
  end
  for _, entry in ipairs(moc_files) do
    entry.output_json = true
    if entry.depfile == nil then
      entry.depfile = true
    end
  end
  local generated = moc.generate({
    target = request.target,
    qt = request.qt,
    cwd = request.cwd or ".",
    files = moc_files,
    generated_include = request.generated_include,
  })
  local json_inputs = {}
  for _, result in ipairs(generated) do
    if result.json == nil then
      error("qt.qml_module moc input did not produce metatype JSON")
    end
    append(json_inputs, result.json)
  end

  local moc_tool = lito.external_tool(request.qt, "moc")
  local collect_args = { "-o", "@OUTPUT@", "--collect-json" }
  for index, _ in ipairs(json_inputs) do
    append(collect_args, "@INPUT:" .. index .. "@")
  end
  local metatypes = lito.run({
    tool = moc_tool,
    cwd = ".",
    args = collect_args,
    inputs = json_inputs,
    outputs = { prefix .. "/metatypes.json" },
  }).outputs[1]

  local registrar = lito.external_tool(request.qt, "qmltyperegistrar")
  local registered = lito.run({
    tool = registrar,
    cwd = ".",
    args = {
      "--generate-qmltypes=@OUTPUT:2@",
      "--import-name=" .. request.uri,
      "--major-version=" .. major,
      "--minor-version=" .. minor,
      "-o", "@OUTPUT:1@",
      "@INPUT:1@",
    },
    inputs = { metatypes },
    outputs = {
      prefix .. "/qmltyperegistrations.cpp",
      prefix .. "/module.qmltypes",
    },
  })
  lito.target_add_generated_source(request.target, registered.outputs[1])
  lito.target_add_metadata(request.target, metatypes)
  lito.target_add_metadata(request.target, registered.outputs[2])
  return {
    cpp = registered.outputs[1],
    qmltypes = registered.outputs[2],
    metatypes = metatypes,
  }
end

local function generate_qmldir(request, prefix, resource_path, version, qml_files,
                               registration)
  local content = "module " .. request.uri .. "\n"
  if registration ~= nil then
    content = content .. "typeinfo module.qmltypes\n"
  end
  content = content .. "prefer :" .. resource_path .. "\n"
  for _, path in ipairs(qml_files) do
    local qualifier = contains(request.singletons or {}, path) and "singleton " or ""
    content = content .. qualifier .. qml_type(path) .. " " .. version .. " " .. path .. "\n"
  end
  for _, imported in ipairs(request.imports or {}) do
    if not module_name(imported) then
      error("qt.qml_module imports must contain module names")
    end
    content = content .. "depends " .. imported .. "\n"
  end
  local result = lito.write({
    output = prefix .. "/qmldir",
    content = content,
  }).output
  lito.target_add_metadata(request.target, result)
  return result
end

local function scan_imports(request, prefix, qml_files)
  local arguments = { "-qmlFiles" }
  for index, _ in ipairs(qml_files) do
    append(arguments, "@INPUT:" .. index .. "@")
  end
  append(arguments, "-output-file")
  append(arguments, "@OUTPUT@")
  local scanned = lito.run({
    tool = lito.external_tool(request.qt, "qmlimportscanner"),
    cwd = request.cwd or ".",
    args = arguments,
    inputs = qml_files,
    outputs = { prefix .. "/imports.json" },
  }).outputs[1]
  lito.target_add_metadata(request.target, scanned)
  return scanned
end

local function generate_cache(request, prefix, resource_prefix, qml_files, qmldir,
                              module_qrc, raw_qrc)
  if request.cache == false then
    return {}
  end
  local tool = lito.external_tool(request.qt, "qmlcachegen")
  local generated = {}
  local resource_paths = {}
  local cache_name = output_name(request.uri)
  for _, path in ipairs(qml_files) do
    local resource_path = resource_prefix .. path
    local output = prefix .. "/cache/" .. cache_name .. "_" .. output_name(path) .. ".cpp"
    local result = lito.run({
      tool = tool,
      cwd = ".",
      args = {
        "--resource-path", resource_path,
        "-i", "@INPUT:2@",
        "--resource", "@INPUT:3@",
        "--resource", "@INPUT:4@",
        "-o", "@OUTPUT@",
        "@INPUT:1@",
      },
      inputs = { path, qmldir, module_qrc, raw_qrc },
      outputs = { output },
    }).outputs[1]
    lito.target_add_generated_source(request.target, result)
    append(generated, result)
    append(resource_paths, resource_path)
  end

  local response_content = "--resource\n@INPUT:1@\n--resource\n@INPUT:2@\n"
  for _, path in ipairs(resource_paths) do
    response_content = response_content .. path .. "\n"
  end
  local response = lito.write({
    output = prefix .. "/cache/loader.rsp",
    content = response_content,
    inputs = { module_qrc, raw_qrc },
  }).output
  local loader = lito.run({
    tool = tool,
    cwd = ".",
    args = {
      "--resource-name", "qmlcache_" .. cache_name,
      "-o", "@OUTPUT@",
      "@@INPUT:1@",
    },
    inputs = { response, module_qrc, raw_qrc },
    outputs = { prefix .. "/cache/" .. cache_name .. "_qmlcache_loader.cpp" },
  }).outputs[1]
  lito.target_add_generated_source(request.target, loader)
  append(generated, loader)
  return generated
end

local function generate_types(request, prefix, qml_files, module_qrc, raw_qrc,
                              information)
  if request.type_compiler ~= true then
    return {}
  end
  if not contains(information.targets, "Qt6::QmlPrivate") or
      not contains(information.targets, "Qt6::QuickPrivate") then
    error("qt.qml_module.type_compiler requires Qt6::QmlPrivate and Qt6::QuickPrivate targets")
  end
  local generated = {}
  local directory = prefix .. "/qmltc"
  lito.target_add_generated_include(request.target, directory)
  local tool = lito.external_tool(request.qt, "qmltc")
  for _, path in ipairs(qml_files) do
    local base = qml_type(path):lower()
    local lookup = lito.copy({
      input = path,
      output = prefix .. "/" .. path,
    }).output
    local result = lito.run({
      tool = tool,
      cwd = request.cwd or ".",
      output_cwd = 1,
      args = {
        "--header", "@OUTPUT_NAME:1@",
        "--impl", "@OUTPUT_NAME:2@",
        "--namespace", request.uri:gsub("%.", "::"),
        "--module", request.uri,
        "--resource", "@INPUT:2@",
        "--resource", "@INPUT:3@",
        "@INPUT:1@",
      },
      inputs = { path, module_qrc, raw_qrc, lookup },
      outputs = {
        directory .. "/" .. base .. ".h",
        directory .. "/" .. base .. ".cpp",
      },
    })
    lito.target_add_generated_source(request.target, result.outputs[2])
    local generated_moc = moc.generate({
      target = request.target,
      qt = request.qt,
      cwd = request.cwd or ".",
      generated_include = false,
      files = {
        {
          source = result.outputs[1],
          mode = "separate",
          output = directory .. "/moc_" .. base .. ".cpp",
        },
      },
    })[1]
    append(generated, {
      header = result.outputs[1],
      cpp = result.outputs[2],
      moc = generated_moc.cpp,
    })
  end
  return generated
end

local function generate_static_plugin(request, prefix, registration, cache_enabled)
  local name = output_name(request.uri)
  local plugin_name = name .. "Plugin"
  local resources = { name .. "_module", name .. "_raw" }
  if cache_enabled then
    append(resources, "qmlcache_" .. name)
  end

  local content = "#define QT_STATICPLUGIN\n" ..
      "#include <QtCore/qtsymbolmacros.h>\n" ..
      "#include <QtQml/qqmlextensionplugin.h>\n\n"
  for _, resource in ipairs(resources) do
    content = content .. "QT_DECLARE_EXTERN_RESOURCE(" .. resource .. ")\n"
  end
  if registration ~= nil then
    content = content .. "QT_DECLARE_EXTERN_SYMBOL_VOID(qml_register_types_" .. name .. ")\n"
  end
  content = content .. "\nclass " .. plugin_name .. " : public QQmlEngineExtensionPlugin {\n" ..
      "    Q_OBJECT\n" ..
      "    Q_PLUGIN_METADATA(IID QQmlEngineExtensionInterface_iid)\n\n" ..
      "public:\n" ..
      "    explicit " .. plugin_name ..
      "(QObject* parent = nullptr) : QQmlEngineExtensionPlugin(parent) {\n"
  if registration ~= nil then
    content = content .. "        QT_KEEP_SYMBOL(qml_register_types_" .. name .. ")\n"
  end
  for _, resource in ipairs(resources) do
    content = content .. "        QT_KEEP_RESOURCE(" .. resource .. ")\n"
  end
  content = content .. "    }\n};\n"

  local source = lito.write({
    output = prefix .. "/" .. plugin_name .. ".hpp",
    content = content,
  }).output
  local generated = moc.generate({
    target = request.target,
    qt = request.qt,
    cwd = request.cwd or ".",
    generated_include = prefix,
    files = {
      {
        source = source,
        mode = "separate",
        output = prefix .. "/moc_" .. plugin_name .. ".cpp",
        plugin_metadata = { uri = request.uri },
      },
    },
  })[1]
  return { source = source, moc = generated.cpp }
end

function qml.generate_module(request)
  if type(request) ~= "table" then
    error("qt.qml_module request must be a table")
  end
  require_field(request, "target", "userdata")
  require_field(request, "qt", "userdata")
  local uri = require_field(request, "uri", "string")
  if not module_name(uri) then
    error("qt.qml_module.uri must be a dotted QML module name")
  end
  local version = request.version or "1.0"
  local major, minor = version:match("^(%d+)%.(%d+)$")
  if major == nil then
    error("qt.qml_module.version must be major.minor")
  end
  local qml_files = checked_files(require_field(request, "qml_files", "table"),
                                  "qt.qml_module.qml_files")
  if #qml_files == 0 then
    error("qt.qml_module.qml_files must not be empty")
  end
  local singletons = checked_files(request.singletons or {}, "qt.qml_module.singletons")
  for _, path in ipairs(singletons) do
    if not contains(qml_files, path) then
      error("qt.qml_module singleton '" .. path .. "' is not present in qml_files")
    end
  end
  request.singletons = singletons
  local resources = checked_files(request.resources or {}, "qt.qml_module.resources")
  local plugin = request.plugin or "static"
  if plugin ~= "static" and plugin ~= "none" then
    error("qt.qml_module.plugin must be 'static' or 'none'")
  end
  local information = lito.external_dependency_info(request.qt)
  if information.provider ~= "cmake" or not information.version:match("^6%.11%.") then
    error("qt.qml_module currently requires a Qt 6.11 CMake dependency")
  end

  local prefix = request.output or ("lito-qml/" .. uri:gsub("%.", "_"))
  safe_path(prefix, "qt.qml_module.output")
  local resource_base = resource_prefix(request.resource_prefix or "/qt/qml")
  local resource_prefix = resource_base .. (resource_base == "/" and "" or "/") ..
      uri:gsub("%.", "/") .. "/"
  local registration = generate_registration(request, prefix, major, minor)
  local qmldir = generate_qmldir(request, prefix, resource_prefix, version, qml_files,
                                 registration)
  local import_metadata = scan_imports(request, prefix, qml_files)

  local raw_files = {}
  for _, path in ipairs(qml_files) do
    append(raw_files, { alias = path, input = path })
  end
  for _, path in ipairs(resources) do
    append(raw_files, { alias = path, input = path })
  end
  local raw_qrc = write_qrc(prefix .. "/raw.qrc", resource_prefix, raw_files)

  local module_files = { { alias = "qmldir", input = qmldir } }
  if registration ~= nil then
    append(module_files, { alias = "module.qmltypes", input = registration.qmltypes })
  end
  local module_qrc = write_qrc(prefix .. "/module.qrc", resource_prefix, module_files)
  local rcc = lito.external_tool(request.qt, "rcc")
  compile_qrc(request.target, rcc, output_name(uri) .. "_module", module_qrc,
              prefix .. "/qrc_module.cpp")
  compile_qrc(request.target, rcc, output_name(uri) .. "_raw", raw_qrc,
              prefix .. "/qrc_raw.cpp")
  local cache_sources = generate_cache(request, prefix, resource_prefix, qml_files, qmldir,
                                       module_qrc, raw_qrc)
  local type_sources = generate_types(request, prefix, qml_files, module_qrc, raw_qrc,
                                      information)
  local plugin_artifact = nil
  if plugin == "static" then
    plugin_artifact = generate_static_plugin(request, prefix, registration,
                                             #cache_sources ~= 0)
    lito.target_add_auxiliary_artifact(request.target, module_qrc)
  end
  return {
    qmldir = qmldir,
    imports = import_metadata,
    qmltypes = registration ~= nil and registration.qmltypes or nil,
    module_resource = module_qrc,
    raw_resource = raw_qrc,
    cache_sources = cache_sources,
    type_sources = type_sources,
    plugin = plugin_artifact,
  }
end

return qml
