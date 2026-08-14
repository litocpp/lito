local result = lito.configure_file({
    package = "fixture-configure-file",
    input = "config/build_config.hpp.in",
    output = "include/fixture/build_config.hpp",
    values = {
        PROFILE = lito.profile,
        ENABLE_TRACE = false,
        ABI_REVISION = 3,
        LITERAL = "@NOT_RECURSIVE@",
    },
})

assert(type(result.output) == "string")
assert(type(result.changed) == "boolean")
