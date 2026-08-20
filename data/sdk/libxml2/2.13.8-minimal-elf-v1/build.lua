lito.configure_file({
  input = "config/config.h.in",
  output = "config.h",
  values = { VERSION = "2.13.8" },
})

lito.configure_file({
  input = "config/xmlversion.h.in",
  output = "include/libxml/xmlversion.h",
  values = {
    VERSION = "2.13.8",
    VERSION_NUMBER = 21308,
  },
})
