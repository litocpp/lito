export module tenon.build_profile;

import rstd;
import tenon.model;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace tenon
{

template<typename T>
auto failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::InvalidRequest, rstd::move(message)));
}

} // namespace tenon

export namespace tenon
{

auto is_supported_cpp_standard(ref<str> standard) -> bool {
    return standard == "c++20"_str || standard == "c++23"_str || standard == "c++26"_str ||
           standard == "c++2b"_str || standard == "c++2c"_str;
}

auto build_profile_name(BuildProfile profile) -> ref<str> {
    switch (profile) {
    case BuildProfile::Debug: return "debug"_str;
    case BuildProfile::Release: return "release"_str;
    }
    return "debug"_str;
}

auto parse_build_profile(ref<str> name) -> Result<BuildProfile> {
    if (name == "debug"_str) return Ok(BuildProfile::Debug);
    if (name == "release"_str) return Ok(BuildProfile::Release);
    return failure<BuildProfile>(
        rstd::format("unknown profile '{}'; expected debug or release", name));
}

auto is_profile_owned_option(ref<str> option) -> bool {
    const bool optimization = option == "-O"_str || option == "-O0"_str || option == "-O1"_str ||
                              option == "-O2"_str || option == "-O3"_str || option == "-O4"_str ||
                              option == "-Ofast"_str || option == "-Og"_str ||
                              option == "-Os"_str || option == "-Oz"_str;
    const bool debug_info = option == "-g"_str || option == "-g0"_str ||
                            option.starts_with("-gdwarf-"_str) || option.starts_with("-ggdb"_str) ||
                            option.starts_with("-gline-"_str) || option == "-gmodules"_str ||
                            option.starts_with("-gsplit-dwarf"_str);
    return optimization || debug_info || option == "-DNDEBUG"_str ||
           option.starts_with("-DNDEBUG="_str) || option == "-UNDEBUG"_str;
}

auto is_profile_owned_definition(ref<str> definition) -> bool {
    return definition == "NDEBUG"_str || definition.starts_with("NDEBUG="_str);
}

auto make_profile_spec(const BuildConfiguration& configuration) -> Result<ProfileSpec> {
    for (const auto& option : configuration.options) {
        if (is_profile_owned_option(option.as_str())) {
            return failure<ProfileSpec>(
                rstd::format("build option '{}' overrides the selected profile", option.as_str()));
        }
    }

    auto options = Vec<String>::make();
    switch (configuration.profile) {
    case BuildProfile::Debug:
        options.push(String::make("-O0"_str));
        options.push(String::make("-g"_str));
        break;
    case BuildProfile::Release:
        options.push(String::make("-O3"_str));
        options.push(String::make("-DNDEBUG"_str));
        break;
    }
    for (const auto& option : configuration.options) options.push(option.clone());

    return Ok(ProfileSpec {
        .name              = String::make(build_profile_name(configuration.profile)),
        .standard_library  = configuration.standard_library,
        .bmi_mode          = configuration.bmi_mode,
        .language_standard = configuration.language_standard.clone(),
        .options           = rstd::move(options),
        .linker_options    = configuration.linker_options.clone(),
    });
}

} // namespace tenon
