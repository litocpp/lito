local target = lito.target({
  kind = "bin",
  name = "lito",
})
local pack = lito.host_tool(target, "lito-pack", "lito-package-embed")

local function embed(id, source_name)
  local source = lito.external_source(target, source_name)
  local generated = lito.run({
    tool = pack,
    cwd = ".",
    input_roots = { source },
    args = {
      "--root", "@INPUT_ROOT:1@",
      "--registry", "https://registry.litocpp.org/",
      "--archive", "@OUTPUT:1@",
      "--descriptor", "@OUTPUT:2@",
      "--source", "@OUTPUT:3@",
      "--blob", "@OUTPUT:4@",
      "--depfile", "@OUTPUT:5@",
      "--module", "lito.executable",
      "--namespace", "lito::builtin",
      "--function", id .. "_resources",
    },
    inputs = { source.file("lito.toml") },
    outputs = {
      "builtin/" .. id .. ".tar.zst",
      "builtin/" .. id .. ".json",
      "builtin/" .. id .. ".cpp",
      "builtin/" .. id .. ".bin",
      "builtin/" .. id .. ".d",
    },
    depfile = { output = 5 },
  })
  lito.target_add_generated_source(target, generated.outputs[3])
end

embed("pmacro", "builtin-pmacro")
embed("safety", "builtin-safety")
embed("libxml2", "builtin-libxml2")
