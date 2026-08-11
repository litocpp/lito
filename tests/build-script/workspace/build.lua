local result = lito.configure_file({
    package = "fixture-configure-workspace-app",
    input = "config/build_config.hpp.in",
    output = "include/fixture/build_config.hpp",
    values = {
        PROFILE = lito.profile,
    },
})

assert(type(result.output) == "string")
assert(type(result.changed) == "boolean")
