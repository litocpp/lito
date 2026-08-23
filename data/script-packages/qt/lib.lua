local moc = require("qt/moc.lua")
local protobuf = require("qt/protobuf.lua")
local qml = require("qt/qml.lua")
local translations = require("qt/translations.lua")

return {
  moc = moc.generate,
  protobuf = protobuf.generate,
  qml_module = qml.generate_module,
  translations = translations.generate,
}
