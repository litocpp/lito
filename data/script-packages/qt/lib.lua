local moc = require("qt/moc.lua")
local qml = require("qt/qml.lua")

return {
  moc = moc.generate,
  qml_module = qml.generate_module,
}
