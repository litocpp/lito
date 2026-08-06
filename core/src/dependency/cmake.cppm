module;
#include <rstd/macro.hpp>

export module tenon.dependency:cmake;

import rstd;
import rstd.json;
import tenon.model;
import tenon.process;
import tenon.storage;

using namespace rstd::prelude;
using namespace rstd::literals;
using Json      = rstd::json::Value;
using JsonArray = rstd::json::Array;

namespace tenon
{

template<typename T>
auto cmake_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Dependency, rstd::move(message)));
}

template<typename T>
auto cmake_failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Dependency, message));
}

auto path_text(ref<rstd::path::Path> path, ref<str> context) -> Result<String> {
    auto text = path.to_str();
    if (text.is_none()) {
        return cmake_failure<String>(
            rstd::format("{} path '{}' is not valid UTF-8", context, path));
    }
    if (text->contains(";"_str)) {
        return cmake_failure<String>(rstd::format("{} path '{}' contains ';'", context, path));
    }
    return Ok(String::make(*text));
}

auto append_identity(String& output, ref<str> value) -> void {
    output.push_str(rstd::format("{}:{}\n", value.len(), value).as_str());
}

auto identity_hash(ref<str> value) -> String {
    auto hash = uint64_t(14695981039346656037ull);
    for (auto byte : value) {
        hash ^= byte.to_primitive();
        hash *= uint64_t(1099511628211ull);
    }
    static constexpr char digits[] = "0123456789abcdef";
    char                  result[16];
    for (size_t index = 0; index < 16; ++index) {
        result[15 - index] = digits[hash & 0xfu];
        hash >>= 4u;
    }
    return String::make(
        ref<str>::from_raw_parts_unchecked(reinterpret_cast<const byte*>(result), usize(16)));
}

struct CMakeWorkArea {
    PathBuf root;
    PathBuf source;
    PathBuf build;
    PathBuf install;
    PathBuf query_root;
    PathBuf query_source;
    PathBuf query_build;
};

auto work_area(const ResolvedCMakeDependencyRequirement& requirement,
               const CMakeProviderConfig&                provider,
               const BuildConfiguration&                 build,
               ref<str> effective_target) -> Result<CMakeWorkArea> {
    auto recipe = String::make("tenon-cmake-install-v2\n"_str);
    append_identity(recipe,
                    requirement.source_identity.is_some() ? requirement.source_identity->as_str()
                                                          : "installed"_str);
    auto executable = path_text(provider.executable.as_path(), "CMake executable"_str);
    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
    auto compiler = path_text(build.toolchain.compiler.as_path(), "C++ compiler"_str);
    if (compiler.is_err()) return Err(rstd::move(compiler).unwrap_err());
    auto archiver = path_text(build.toolchain.archiver.as_path(), "archiver"_str);
    if (archiver.is_err()) return Err(rstd::move(archiver).unwrap_err());
    append_identity(recipe, executable->as_str());
    append_identity(recipe, provider.generator.as_str());
    append_identity(recipe, compiler->as_str());
    append_identity(recipe, archiver->as_str());
    append_identity(recipe, effective_target);
    append_identity(recipe, build.profile == BuildProfile::Debug ? "Debug"_str : "Release"_str);
    append_identity(recipe, build.language_standard.as_str());
    append_identity(
        recipe, build.standard_library == StandardLibrary::Libcxx ? "libc++"_str : "libstdc++"_str);
    append_identity(recipe, build.exceptions ? "exceptions"_str : "no-exceptions"_str);
    append_identity(recipe, build.rtti ? "rtti"_str : "no-rtti"_str);
    for (const auto& entry : requirement.cache) {
        append_identity(recipe, entry.name.as_str());
        append_identity(recipe, entry.value.as_str());
    }
    auto cache =
        tenon_cache_directory(PathBuf::from("cmake"_str).as_path(), "CMake dependencies"_str);
    if (cache.is_err()) return Err(rstd::move(cache).unwrap_err());
    auto root         = cache->join(PathBuf::from(identity_hash(recipe.as_str())).as_path());
    auto query_recipe = String::make("tenon-cmake-query-v2\n"_str);
    append_identity(query_recipe, requirement.package.as_str());
    if (requirement.config_directory.is_some()) {
        auto directory =
            path_text(requirement.config_directory->as_path(), "CMake config directory"_str);
        if (directory.is_err()) return Err(rstd::move(directory).unwrap_err());
        append_identity(query_recipe, directory->as_str());
    } else {
        append_identity(query_recipe, "default-config-directory"_str);
    }
    for (const auto& target : requirement.targets) {
        append_identity(query_recipe, target.name.as_str());
    }
    append_identity(query_recipe, effective_target);
    auto query_root = root.join(PathBuf::from("queries"_str).as_path())
                          .join(PathBuf::from(identity_hash(query_recipe.as_str())).as_path());
    return Ok(CMakeWorkArea {
        .root = root.clone(),
        .source =
            requirement.source_root.is_some() ? requirement.source_root->clone() : PathBuf::make(),
        .build        = root.join(PathBuf::from("build"_str).as_path()),
        .install      = root.join(PathBuf::from("install"_str).as_path()),
        .query_root   = query_root.clone(),
        .query_source = query_root.join(PathBuf::from("source"_str).as_path()),
        .query_build  = query_root.join(PathBuf::from("build"_str).as_path()),
    });
}

auto run_cmake(Vec<String>                   arguments,
               ref<str>                      operation,
               Option<ref<rstd::path::Path>> working_directory = None()) -> Result<empty> {
    auto output = run_command(arguments, working_directory);
    if (output.is_err()) {
        return cmake_failure<empty>(rstd::format("{} could not execute: {}",
                                                 operation,
                                                 rstd::move(output).unwrap_err().message.as_str()));
    }
    if (output->exit_code != i32 {}) {
        auto diagnostics = output->standard_output.clone();
        if (! diagnostics.is_empty() && ! output->standard_error.is_empty()) {
            diagnostics.push('\n');
        }
        diagnostics.push_str(output->standard_error.as_str());
        return cmake_failure<empty>(rstd::format("{} failed with exit code {}:\n{}",
                                                 operation,
                                                 output->exit_code,
                                                 diagnostics.as_str()));
    }
    return Ok(empty {});
}

auto push_path_argument(Vec<String>&          arguments,
                        ref<str>              prefix,
                        ref<rstd::path::Path> path,
                        ref<str>              context) -> Result<empty> {
    auto text = path_text(path, context);
    if (text.is_err()) return Err(rstd::move(text).unwrap_err());
    auto argument = String::make(prefix);
    argument.push_str(text->as_str());
    arguments.push(rstd::move(argument));
    return Ok(empty {});
}

auto cmake_cxx_standard(ref<str> value) -> ref<str> {
    if (value.starts_with("c++"_str)) return *value.strip_prefix("c++"_str);
    if (value.starts_with("gnu++"_str)) return *value.strip_prefix("gnu++"_str);
    return value;
}

auto cmake_cxx_flags(const BuildConfiguration& configuration) -> String {
    auto result = String::make(configuration.standard_library == StandardLibrary::Libcxx
                                   ? "-stdlib=libc++"_str
                                   : "-stdlib=libstdc++"_str);
    result.push_str(configuration.exceptions ? " -fexceptions"_str : " -fno-exceptions"_str);
    result.push_str(configuration.rtti ? " -frtti"_str : " -fno-rtti"_str);
    return result;
}

auto configure_and_install(const ResolvedCMakeDependencyRequirement& requirement,
                           const CMakeProviderConfig&                provider,
                           const BuildConfiguration&                 configuration,
                           const CMakeWorkArea&                      area) -> Result<empty> {
    if (requirement.source_root.is_none()) return Ok(empty {});
    auto marker = area.root.join(PathBuf::from("installed"_str).as_path());
    auto ready  = rstd::fs::exists(marker.as_path());
    if (ready.is_err()) {
        return cmake_failure<empty>(rstd::format("cannot inspect CMake install marker '{}': {}",
                                                 marker.as_path(),
                                                 rstd::move(ready).unwrap_err()));
    }
    if (*ready) return Ok(empty {});
    auto arguments  = Vec<String>::make();
    auto executable = path_text(provider.executable.as_path(), "CMake executable"_str);
    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
    arguments.push(rstd::move(executable).unwrap());
    arguments.push(String::make("-S"_str));
    auto source = path_text(area.source.as_path(), "CMake source"_str);
    if (source.is_err()) return Err(rstd::move(source).unwrap_err());
    arguments.push(rstd::move(source).unwrap());
    arguments.push(String::make("-B"_str));
    auto build = path_text(area.build.as_path(), "CMake build"_str);
    if (build.is_err()) return Err(rstd::move(build).unwrap_err());
    arguments.push(rstd::move(build).unwrap());
    arguments.push(String::make("-G"_str));
    arguments.push(provider.generator.clone());
    rstd_try(push_path_argument(
        arguments, "-DCMAKE_INSTALL_PREFIX="_str, area.install.as_path(), "CMake install"_str));
    rstd_try(push_path_argument(arguments,
                                "-DCMAKE_CXX_COMPILER="_str,
                                configuration.toolchain.compiler.as_path(),
                                "C++ compiler"_str));
    rstd_try(push_path_argument(
        arguments, "-DCMAKE_AR="_str, configuration.toolchain.archiver.as_path(), "archiver"_str));
    auto build_type = configuration.profile == BuildProfile::Debug ? "Debug"_str : "Release"_str;
    arguments.push(rstd::format("-DCMAKE_BUILD_TYPE={}", build_type));
    arguments.push(rstd::format("-DCMAKE_CXX_STANDARD={}",
                                cmake_cxx_standard(configuration.language_standard.as_str())));
    arguments.push(String::make("-DCMAKE_CXX_EXTENSIONS=OFF"_str));
    arguments.push(rstd::format("-DCMAKE_CXX_FLAGS={}", cmake_cxx_flags(configuration).as_str()));
    for (const auto& entry : requirement.cache) {
        arguments.push(rstd::format("-D{}={}", entry.name.as_str(), entry.value.as_str()));
    }
    rstd_try(run_cmake(
        rstd::move(arguments),
        rstd::format("CMake dependency '{}' configure", requirement.package.as_str()).as_str()));

    arguments  = Vec<String>::make();
    executable = path_text(provider.executable.as_path(), "CMake executable"_str);
    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
    arguments.push(rstd::move(executable).unwrap());
    arguments.push(String::make("--build"_str));
    build = path_text(area.build.as_path(), "CMake build"_str);
    if (build.is_err()) return Err(rstd::move(build).unwrap_err());
    arguments.push(rstd::move(build).unwrap());
    arguments.push(String::make("--config"_str));
    arguments.push(String::make(build_type));
    rstd_try(run_cmake(
        rstd::move(arguments),
        rstd::format("CMake dependency '{}' build", requirement.package.as_str()).as_str()));

    arguments  = Vec<String>::make();
    executable = path_text(provider.executable.as_path(), "CMake executable"_str);
    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
    arguments.push(rstd::move(executable).unwrap());
    arguments.push(String::make("--install"_str));
    build = path_text(area.build.as_path(), "CMake build"_str);
    if (build.is_err()) return Err(rstd::move(build).unwrap_err());
    arguments.push(rstd::move(build).unwrap());
    arguments.push(String::make("--config"_str));
    arguments.push(String::make(build_type));
    rstd_try(run_cmake(
        rstd::move(arguments),
        rstd::format("CMake dependency '{}' install", requirement.package.as_str()).as_str()));
    auto marked = rstd::fs::write_atomic(marker.as_path(), ("installed\n"_str).as_bytes());
    if (marked.is_err()) {
        return cmake_failure<empty>(rstd::format("cannot write CMake install marker '{}': {}",
                                                 marker.as_path(),
                                                 rstd::move(marked).unwrap_err()));
    }
    return Ok(empty {});
}

auto probe_project(const ResolvedCMakeDependencyRequirement& requirement) -> String {
    auto result = String::make("cmake_minimum_required(VERSION 3.28)\n"
                               "project(tenon_cmake_probe LANGUAGES CXX)\n"
                               "set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)\n"_str);
    if (requirement.source_root.is_some()) {
        result.push_str("find_package("_str);
        result.push_str(requirement.package.as_str());
        result.push_str(
            " REQUIRED CONFIG PATHS \"${TENON_CMAKE_DEPENDENCY_PREFIX}\" NO_DEFAULT_PATH)\n"_str);
    } else {
        result.push_str("find_package("_str);
        result.push_str(requirement.package.as_str());
        result.push_str(" REQUIRED)\n"_str);
    }
    for (const auto& target : requirement.targets) {
        result.push_str("if(NOT TARGET "_str);
        result.push_str(target.name.as_str());
        result.push_str(")\n  message(FATAL_ERROR \"required imported target "_str);
        result.push_str(target.name.as_str());
        result.push_str(" is unavailable\")\nendif()\n"_str);
    }
    result.push_str("if(DEFINED "_str);
    result.push_str(requirement.package.as_str());
    result.push_str(
        "_VERSION)\n  file(WRITE \"${CMAKE_BINARY_DIR}/tenon-package-version.txt\" \"${"_str);
    result.push_str(requirement.package.as_str());
    result.push_str(
        "_VERSION}\")\nelse()\n  file(WRITE \"${CMAKE_BINARY_DIR}/tenon-package-version.txt\" \"\")\nendif()\n"_str);
    result.push_str("add_executable(tenon_cmake_baseline probe.cpp)\n"_str);
    for (usize index {}; index < requirement.targets.len(); ++index) {
        result.push_str(
            rstd::format("add_executable(tenon_cmake_dependency_{} probe.cpp)\n", index).as_str());
        result.push_str(
            rstd::format("target_link_libraries(tenon_cmake_dependency_{} PRIVATE ", index)
                .as_str());
        result.push_str(requirement.targets[index].name.as_str());
        result.push_str(")\n"_str);
    }
    result.push_str("add_executable(tenon_cmake_combined probe.cpp)\n"
                    "target_link_libraries(tenon_cmake_combined PRIVATE"_str);
    for (const auto& target : requirement.targets) {
        result.push_ascii(u8(' '));
        result.push_str(target.name.as_str());
    }
    result.push_str(")\n"_str);
    return result;
}

auto write_probe_files(const ResolvedCMakeDependencyRequirement& requirement,
                       const CMakeWorkArea&                      area) -> Result<empty> {
    auto query =
        area.query_build.join(PathBuf::from(".cmake/api/v1/query/client-tenon"_str).as_path());
    auto directories = Vec<PathBuf>::make();
    directories.push(area.root.clone());
    directories.push(area.build.clone());
    directories.push(area.install.clone());
    directories.push(area.query_root.clone());
    directories.push(area.query_source.clone());
    directories.push(area.query_build.clone());
    directories.push(query.clone());
    for (const auto& directory : directories) {
        auto created = rstd::fs::create_dir_all(directory.as_path());
        if (created.is_err()) {
            return cmake_failure<empty>(rstd::format("cannot create CMake directory '{}': {}",
                                                     directory.as_path(),
                                                     rstd::move(created).unwrap_err()));
        }
    }
    auto cmake_lists = area.query_source.join(PathBuf::from("CMakeLists.txt"_str).as_path());
    auto source      = area.query_source.join(PathBuf::from("probe.cpp"_str).as_path());
    auto query_file  = query.join(PathBuf::from("query.json"_str).as_path());
    auto project     = probe_project(requirement);
    auto written     = rstd::fs::write_atomic(cmake_lists.as_path(), project.as_str().as_bytes());
    if (written.is_err()) {
        return cmake_failure<empty>(
            rstd::format("cannot write CMake probe project: {}", rstd::move(written).unwrap_err()));
    }
    written =
        rstd::fs::write_atomic(source.as_path(), ("int main() { return 0; }\n"_str).as_bytes());
    if (written.is_err()) {
        return cmake_failure<empty>(
            rstd::format("cannot write CMake probe source: {}", rstd::move(written).unwrap_err()));
    }
    written = rstd::fs::write_atomic(
        query_file.as_path(),
        ("{\"requests\":[{\"kind\":\"codemodel\",\"version\":2}]}\n"_str).as_bytes());
    if (written.is_err()) {
        return cmake_failure<empty>(rstd::format("cannot write CMake File API query: {}",
                                                 rstd::move(written).unwrap_err()));
    }
    return Ok(empty {});
}

auto configure_probe(const ResolvedCMakeDependencyRequirement& requirement,
                     const CMakeProviderConfig&                provider,
                     const BuildConfiguration&                 configuration,
                     const CMakeWorkArea&                      area) -> Result<empty> {
    auto arguments  = Vec<String>::make();
    auto executable = path_text(provider.executable.as_path(), "CMake executable"_str);
    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
    arguments.push(rstd::move(executable).unwrap());
    arguments.push(String::make("-S"_str));
    auto source = path_text(area.query_source.as_path(), "CMake query source"_str);
    if (source.is_err()) return Err(rstd::move(source).unwrap_err());
    arguments.push(rstd::move(source).unwrap());
    arguments.push(String::make("-B"_str));
    auto build = path_text(area.query_build.as_path(), "CMake query build"_str);
    if (build.is_err()) return Err(rstd::move(build).unwrap_err());
    arguments.push(rstd::move(build).unwrap());
    arguments.push(String::make("-G"_str));
    arguments.push(provider.generator.clone());
    rstd_try(push_path_argument(arguments,
                                "-DCMAKE_CXX_COMPILER="_str,
                                configuration.toolchain.compiler.as_path(),
                                "C++ compiler"_str));
    auto build_type = configuration.profile == BuildProfile::Debug ? "Debug"_str : "Release"_str;
    arguments.push(rstd::format("-DCMAKE_BUILD_TYPE={}", build_type));
    arguments.push(rstd::format("-DCMAKE_CXX_STANDARD={}",
                                cmake_cxx_standard(configuration.language_standard.as_str())));
    arguments.push(String::make("-DCMAKE_CXX_EXTENSIONS=OFF"_str));
    arguments.push(rstd::format("-DCMAKE_CXX_FLAGS={}", cmake_cxx_flags(configuration).as_str()));
    if (requirement.source_root.is_some()) {
        rstd_try(push_path_argument(arguments,
                                    "-DTENON_CMAKE_DEPENDENCY_PREFIX="_str,
                                    area.install.as_path(),
                                    "CMake install"_str));
        if (requirement.config_directory.is_some()) {
            auto prefix    = rstd::format("-D{}_DIR=", requirement.package.as_str());
            auto directory = area.install.join(requirement.config_directory->as_path());
            rstd_try(push_path_argument(
                arguments, prefix.as_str(), directory.as_path(), "CMake config directory"_str));
        }
    }
    return run_cmake(
        rstd::move(arguments),
        rstd::format("CMake package '{}' query", requirement.package.as_str()).as_str());
}

auto read_json(ref<rstd::path::Path> path, ref<str> context) -> Result<Json> {
    auto contents = rstd::fs::read_to_string(path);
    if (contents.is_err()) {
        return cmake_failure<Json>(rstd::format(
            "cannot read {} '{}': {}", context, path, rstd::move(contents).unwrap_err()));
    }
    auto parsed = rstd::json::from_str(contents->as_str());
    if (parsed.is_err()) {
        return cmake_failure<Json>(rstd::format(
            "cannot parse {} '{}': {}", context, path, rstd::move(parsed).unwrap_err()));
    }
    return Ok(rstd::move(parsed).unwrap());
}

auto required_json_member(const Json& value, ref<str> key, ref<str> context) -> Result<ref<Json>> {
    auto member = value.get(key);
    if (member.is_none()) {
        return cmake_failure<ref<Json>>(rstd::format("{} is missing '{}'", context, key));
    }
    return Ok(*member);
}

auto required_json_string(const Json& value, ref<str> key, ref<str> context) -> Result<ref<str>> {
    auto member = required_json_member(value, key, context);
    if (member.is_err()) return Err(rstd::move(member).unwrap_err());
    auto text = (**member).as_str();
    if (text.is_none()) {
        return cmake_failure<ref<str>>(rstd::format("{}.{} must be a string", context, key));
    }
    return Ok(*text);
}

auto required_json_array(const Json& value, ref<str> key, ref<str> context)
    -> Result<ref<JsonArray>> {
    auto member = required_json_member(value, key, context);
    if (member.is_err()) return Err(rstd::move(member).unwrap_err());
    auto array = (**member).as_array();
    if (array.is_none()) {
        return cmake_failure<ref<JsonArray>>(rstd::format("{}.{} must be an array", context, key));
    }
    return Ok(*array);
}

auto current_reply_index(ref<rstd::path::Path> reply) -> Result<PathBuf> {
    auto opened = rstd::fs::read_dir(reply);
    if (opened.is_err()) {
        return cmake_failure<PathBuf>(rstd::format(
            "cannot read CMake File API reply '{}': {}", reply, rstd::move(opened).unwrap_err()));
    }
    auto selected      = Option<PathBuf> {};
    auto selected_name = String::make();
    auto entries       = rstd::move(opened).unwrap();
    for (auto entry = entries.next(); entry.is_some(); entry = entries.next()) {
        if (entry->is_err()) {
            return cmake_failure<PathBuf>(rstd::format("cannot read CMake File API entry: {}",
                                                       rstd::move(*entry).unwrap_err()));
        }
        auto value = rstd::move(*entry).unwrap();
        auto name  = value.file_name().into_string();
        if (name.is_err()) continue;
        auto text = rstd::move(name).unwrap();
        if (! text.as_str().starts_with("index-"_str) || ! text.as_str().ends_with(".json"_str)) {
            continue;
        }
        if (selected.is_none() || selected_name < text) {
            selected_name = rstd::move(text);
            selected      = Some(value.path());
        }
    }
    if (selected.is_none()) {
        return cmake_failure<PathBuf>("CMake File API produced no reply index"_str);
    }
    return Ok(rstd::move(selected).unwrap());
}

auto codemodel_path(const CMakeWorkArea& area) -> Result<PathBuf> {
    auto reply      = area.query_build.join(PathBuf::from(".cmake/api/v1/reply"_str).as_path());
    auto index_path = current_reply_index(reply.as_path());
    if (index_path.is_err()) return Err(rstd::move(index_path).unwrap_err());
    auto index = read_json(index_path->as_path(), "CMake File API index"_str);
    if (index.is_err()) return Err(rstd::move(index).unwrap_err());
    auto reply_member = required_json_member(*index, "reply"_str, "CMake File API index"_str);
    if (reply_member.is_err()) return Err(rstd::move(reply_member).unwrap_err());
    auto client =
        required_json_member(**reply_member, "client-tenon"_str, "CMake File API reply"_str);
    if (client.is_err()) return Err(rstd::move(client).unwrap_err());
    auto query =
        required_json_member(**client, "query.json"_str, "CMake File API client reply"_str);
    if (query.is_err()) return Err(rstd::move(query).unwrap_err());
    auto responses =
        required_json_array(**query, "responses"_str, "CMake File API query reply"_str);
    if (responses.is_err()) return Err(rstd::move(responses).unwrap_err());
    if ((**responses).is_empty()) {
        return cmake_failure<PathBuf>("CMake File API query returned no response"_str);
    }
    auto file = required_json_string(
        (**responses)[usize {}], "jsonFile"_str, "CMake File API codemodel response"_str);
    if (file.is_err()) return Err(rstd::move(file).unwrap_err());
    return Ok(reply.join(PathBuf::from(*file).as_path()));
}

struct ProbeTargets {
    PathBuf      baseline;
    Vec<PathBuf> dependencies;
    PathBuf      combined;
};

auto probe_target_paths(const CMakeWorkArea&                      area,
                        const ResolvedCMakeDependencyRequirement& requirement)
    -> Result<ProbeTargets> {
    auto path = codemodel_path(area);
    if (path.is_err()) return Err(rstd::move(path).unwrap_err());
    auto model = read_json(path->as_path(), "CMake File API codemodel"_str);
    if (model.is_err()) return Err(rstd::move(model).unwrap_err());
    auto configurations = required_json_array(*model, "configurations"_str, "CMake codemodel"_str);
    if (configurations.is_err()) return Err(rstd::move(configurations).unwrap_err());
    if ((**configurations).is_empty()) {
        return cmake_failure<ProbeTargets>("CMake codemodel has no configuration"_str);
    }
    auto targets = required_json_array(
        (**configurations)[usize {}], "targets"_str, "CMake codemodel configuration"_str);
    if (targets.is_err()) return Err(rstd::move(targets).unwrap_err());
    auto baseline     = Option<PathBuf> {};
    auto combined     = Option<PathBuf> {};
    auto dependencies = Vec<Option<PathBuf>>::with_capacity(requirement.targets.len());
    for (usize index {}; index < requirement.targets.len(); ++index) {
        dependencies.emplace_back(None());
    }
    auto reply = area.query_build.join(PathBuf::from(".cmake/api/v1/reply"_str).as_path());
    for (const auto& target : **targets) {
        auto name = required_json_string(target, "name"_str, "CMake codemodel target"_str);
        auto file = required_json_string(target, "jsonFile"_str, "CMake codemodel target"_str);
        if (name.is_err()) return Err(rstd::move(name).unwrap_err());
        if (file.is_err()) return Err(rstd::move(file).unwrap_err());
        if (*name == "tenon_cmake_baseline"_str)
            baseline = Some(reply.join(PathBuf::from(*file).as_path()));
        else if (*name == "tenon_cmake_combined"_str)
            combined = Some(reply.join(PathBuf::from(*file).as_path()));
        else {
            for (usize index {}; index < dependencies.len(); ++index) {
                if (*name == rstd::format("tenon_cmake_dependency_{}", index).as_str()) {
                    dependencies[index] = Some(reply.join(PathBuf::from(*file).as_path()));
                    break;
                }
            }
        }
    }
    if (baseline.is_none() || combined.is_none()) {
        return cmake_failure<ProbeTargets>("CMake codemodel is missing probe targets"_str);
    }
    auto resolved_dependencies = Vec<PathBuf>::with_capacity(dependencies.len());
    for (auto& dependency : dependencies) {
        if (dependency.is_none()) {
            return cmake_failure<ProbeTargets>("CMake codemodel is missing probe targets"_str);
        }
        resolved_dependencies.push(rstd::move(dependency).unwrap());
    }
    return Ok(ProbeTargets {
        .baseline     = rstd::move(baseline).unwrap(),
        .dependencies = rstd::move(resolved_dependencies),
        .combined     = rstd::move(combined).unwrap(),
    });
}

auto append_fragment_tokens(Vec<String>& output, ref<str> fragment, ref<str> context)
    -> Result<empty> {
    auto tokens = tokenize_command_fragments(fragment, context);
    if (tokens.is_err()) return Err(rstd::move(tokens).unwrap_err());
    for (auto& token : *tokens) output.push(rstd::move(token));
    return Ok(empty {});
}

auto compile_tokens(const Json& target) -> Result<Vec<String>> {
    auto result = Vec<String>::make();
    auto groups = target.get("compileGroups"_str);
    if (groups.is_none()) return Ok(rstd::move(result));
    auto array = (**groups).as_array();
    if (array.is_none())
        return cmake_failure<Vec<String>>("CMake compileGroups is not an array"_str);
    for (const auto& group : **array) {
        auto language = required_json_string(group, "language"_str, "CMake compile group"_str);
        if (language.is_err()) return Err(rstd::move(language).unwrap_err());
        if (*language != "CXX"_str) continue;
        auto fragments = group.get("compileCommandFragments"_str);
        if (fragments.is_some()) {
            auto values = (**fragments).as_array();
            if (values.is_none()) {
                return cmake_failure<Vec<String>>("CMake compile fragments is not an array"_str);
            }
            for (const auto& fragment : **values) {
                auto text =
                    required_json_string(fragment, "fragment"_str, "CMake compile fragment"_str);
                if (text.is_err()) return Err(rstd::move(text).unwrap_err());
                rstd_try(append_fragment_tokens(result, *text, "CMake compile fragment"_str));
            }
        }
        auto definitions = group.get("defines"_str);
        if (definitions.is_some()) {
            auto values = (**definitions).as_array();
            if (values.is_none())
                return cmake_failure<Vec<String>>("CMake defines is not an array"_str);
            for (const auto& definition : **values) {
                auto text = required_json_string(definition, "define"_str, "CMake definition"_str);
                if (text.is_err()) return Err(rstd::move(text).unwrap_err());
                result.push(rstd::format("-D{}", *text));
            }
        }
        auto includes = group.get("includes"_str);
        if (includes.is_some()) {
            auto values = (**includes).as_array();
            if (values.is_none())
                return cmake_failure<Vec<String>>("CMake includes is not an array"_str);
            for (const auto& include : **values) {
                auto path = required_json_string(include, "path"_str, "CMake include"_str);
                if (path.is_err()) return Err(rstd::move(path).unwrap_err());
                auto system    = include.get("isSystem"_str);
                auto is_system = false;
                if (system.is_some()) {
                    auto value = (**system).as_bool();
                    if (value.is_none())
                        return cmake_failure<Vec<String>>("CMake isSystem is not a boolean"_str);
                    is_system = *value;
                }
                if (is_system) {
                    result.push(String::make("-isystem"_str));
                    result.push(String::make(*path));
                } else {
                    result.push(rstd::format("-I{}", *path));
                }
            }
        }
        break;
    }
    return Ok(rstd::move(result));
}

auto link_tokens(const Json& target) -> Result<Vec<String>> {
    auto result = Vec<String>::make();
    auto link   = target.get("link"_str);
    if (link.is_none()) return Ok(rstd::move(result));
    auto fragments = (**link).get("commandFragments"_str);
    if (fragments.is_none()) return Ok(rstd::move(result));
    auto values = (**fragments).as_array();
    if (values.is_none())
        return cmake_failure<Vec<String>>("CMake link fragments is not an array"_str);
    for (const auto& fragment : **values) {
        auto text = required_json_string(fragment, "fragment"_str, "CMake link fragment"_str);
        if (text.is_err()) return Err(rstd::move(text).unwrap_err());
        rstd_try(append_fragment_tokens(result, *text, "CMake link fragment"_str));
    }
    return Ok(rstd::move(result));
}

auto subtract_baseline(const Vec<String>& baseline, Vec<String> values) -> Vec<String> {
    auto consumed = Vec<bool>::with_capacity(baseline.len());
    for (usize index {}; index < baseline.len(); ++index) consumed.emplace_back(false);
    auto result = Vec<String>::make();
    for (auto& value : values) {
        auto matched = false;
        for (usize index {}; index < baseline.len(); ++index) {
            if (! consumed[index] && baseline[index] == value) {
                consumed[index] = true;
                matched         = true;
                break;
            }
        }
        if (! matched) result.push(rstd::move(value));
    }
    return result;
}

struct ProbeSnapshot {
    Vec<String> compile;
    Vec<String> link;
};

struct ProbeSnapshots {
    Vec<ProbeSnapshot> targets;
    ProbeSnapshot      combined;
};

auto snapshot_from_targets(const Json& baseline, const Json& dependency) -> Result<ProbeSnapshot> {
    auto baseline_compile   = compile_tokens(baseline);
    auto baseline_link      = link_tokens(baseline);
    auto dependency_compile = compile_tokens(dependency);
    auto dependency_link    = link_tokens(dependency);
    if (baseline_compile.is_err()) return Err(rstd::move(baseline_compile).unwrap_err());
    if (baseline_link.is_err()) return Err(rstd::move(baseline_link).unwrap_err());
    if (dependency_compile.is_err()) return Err(rstd::move(dependency_compile).unwrap_err());
    if (dependency_link.is_err()) return Err(rstd::move(dependency_link).unwrap_err());
    return Ok(ProbeSnapshot {
        .compile = subtract_baseline(*baseline_compile, rstd::move(dependency_compile).unwrap()),
        .link    = subtract_baseline(*baseline_link, rstd::move(dependency_link).unwrap()),
    });
}

auto read_probe_snapshots(const CMakeWorkArea&                      area,
                          const ResolvedCMakeDependencyRequirement& requirement)
    -> Result<ProbeSnapshots> {
    auto paths = probe_target_paths(area, requirement);
    if (paths.is_err()) return Err(rstd::move(paths).unwrap_err());
    auto baseline = read_json(paths->baseline.as_path(), "CMake baseline target"_str);
    if (baseline.is_err()) return Err(rstd::move(baseline).unwrap_err());
    auto snapshots = Vec<ProbeSnapshot>::with_capacity(paths->dependencies.len());
    for (const auto& path : paths->dependencies) {
        auto dependency = read_json(path.as_path(), "CMake dependency target"_str);
        if (dependency.is_err()) return Err(rstd::move(dependency).unwrap_err());
        auto snapshot = snapshot_from_targets(*baseline, *dependency);
        if (snapshot.is_err()) return Err(rstd::move(snapshot).unwrap_err());
        snapshots.push(rstd::move(snapshot).unwrap());
    }
    auto combined = read_json(paths->combined.as_path(), "CMake combined target"_str);
    if (combined.is_err()) return Err(rstd::move(combined).unwrap_err());
    auto combined_snapshot = snapshot_from_targets(*baseline, *combined);
    if (combined_snapshot.is_err()) return Err(rstd::move(combined_snapshot).unwrap_err());
    return Ok(ProbeSnapshots {
        .targets  = rstd::move(snapshots),
        .combined = rstd::move(combined_snapshot).unwrap(),
    });
}

auto target_snapshot_identity(const CMakeProviderConfig&                provider,
                              const ResolvedCMakeDependencyRequirement& requirement,
                              ref<str>                                  target,
                              ref<str>                                  version,
                              const ProbeSnapshot&                      snapshot,
                              ref<str> effective_target) -> Result<String> {
    auto executable = path_text(provider.executable.as_path(), "CMake executable"_str);
    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
    auto result = String::make("tenon-cmake-dependency-v1\n"_str);
    append_identity(result, executable->as_str());
    append_identity(result, provider.generator.as_str());
    append_identity(result, requirement.package.as_str());
    append_identity(result, target);
    append_identity(result, version);
    append_identity(result, effective_target);
    if (requirement.source_identity.is_some()) {
        append_identity(result, requirement.source_identity->as_str());
    }
    for (const auto& token : snapshot.compile) append_identity(result, token.as_str());
    return Ok(rstd::move(result));
}

auto dependency_identity(const CMakeProviderConfig&                provider,
                         const ResolvedCMakeDependencyRequirement& requirement,
                         ref<str>                                  version,
                         const ProbeSnapshots&                     snapshots,
                         ref<str> effective_target) -> Result<String> {
    auto executable = path_text(provider.executable.as_path(), "CMake executable"_str);
    if (executable.is_err()) return Err(rstd::move(executable).unwrap_err());
    auto result = String::make("tenon-cmake-declaration-v1\n"_str);
    append_identity(result, executable->as_str());
    append_identity(result, provider.generator.as_str());
    append_identity(result, requirement.package.as_str());
    append_identity(result, version);
    append_identity(result, effective_target);
    if (requirement.source_identity.is_some()) {
        append_identity(result, requirement.source_identity->as_str());
    }
    for (usize index {}; index < requirement.targets.len(); ++index) {
        append_identity(result, requirement.targets[index].name.as_str());
        for (const auto& token : snapshots.targets[index].compile) {
            append_identity(result, token.as_str());
        }
    }
    for (const auto& token : snapshots.combined.link) append_identity(result, token.as_str());
    return Ok(rstd::move(result));
}

} // namespace tenon

export namespace tenon
{

auto resolve_cmake_dependency(const ResolvedCMakeDependencyRequirement& requirement,
                              const CMakeProviderConfig&                provider,
                              const BuildConfiguration&                 configuration,
                              const TargetInfo&                         default_target,
                              ref<str>                                  effective_target,
                              const CppArgumentParser&                  parser)
    -> Result<ResolvedExternalDependency> {
    if (effective_target != default_target.triple.as_str()) {
        return cmake_failure<ResolvedExternalDependency>(rstd::format(
            "CMake dependency '{}' cannot resolve cross target '{}' without an explicit CMake "
            "toolchain contract",
            requirement.alias.as_str(),
            effective_target));
    }
    auto area = work_area(requirement, provider, configuration, effective_target);
    if (area.is_err()) return Err(rstd::move(area).unwrap_err());
    auto created = rstd::fs::create_dir_all(area->root.as_path());
    if (created.is_err()) {
        return cmake_failure<ResolvedExternalDependency>(
            rstd::format("cannot create CMake work directory '{}': {}",
                         area->root.as_path(),
                         rstd::move(created).unwrap_err()));
    }
    auto lock_path = area->root.join(PathBuf::from("lock"_str).as_path());
    auto lock_file = rstd::fs::File::create(lock_path.as_path());
    if (lock_file.is_err()) {
        return cmake_failure<ResolvedExternalDependency>(
            rstd::format("cannot open CMake dependency lock '{}': {}",
                         lock_path.as_path(),
                         rstd::move(lock_file).unwrap_err()));
    }
    auto locked = lock_file->lock();
    if (locked.is_err()) {
        return cmake_failure<ResolvedExternalDependency>(
            rstd::format("cannot lock CMake dependency '{}': {}",
                         requirement.alias.as_str(),
                         rstd::move(locked).unwrap_err()));
    }
    rstd_try(write_probe_files(requirement, *area));
    rstd_try(configure_and_install(requirement, provider, configuration, *area));
    rstd_try(configure_probe(requirement, provider, configuration, *area));
    auto snapshots = rstd_try(read_probe_snapshots(*area, requirement));
    auto version_path =
        area->query_build.join(PathBuf::from("tenon-package-version.txt"_str).as_path());
    auto version = rstd::fs::read_to_string(version_path.as_path());
    if (version.is_err()) {
        return cmake_failure<ResolvedExternalDependency>(
            rstd::format("cannot read CMake package '{}' version: {}",
                         requirement.package.as_str(),
                         rstd::move(version).unwrap_err()));
    }
    auto normalized_version = String::make(version->as_str().trim_ascii());
    if (normalized_version.is_empty()) normalized_version = String::make("unknown"_str);
    auto targets = Vec<ResolvedExternalTargetUsage>::with_capacity(requirement.targets.len());
    for (usize index {}; index < requirement.targets.len(); ++index) {
        const auto& target   = requirement.targets[index];
        const auto& snapshot = snapshots.targets[index];
        auto        source   = rstd::format("CMake dependency '{}' package '{}' target '{}'",
                                            requirement.alias.as_str(),
                                            requirement.package.as_str(),
                                            target.name.as_str());
        auto        compile  = parser.parse(snapshot.compile, source.as_str());
        if (compile.is_err()) {
            return cmake_failure<ResolvedExternalDependency>(
                rstd::format("{} has invalid compile requirements: {}",
                             source.as_str(),
                             rstd::move(compile).unwrap_err()));
        }
        auto identity = target_snapshot_identity(provider,
                                                 requirement,
                                                 target.name.as_str(),
                                                 normalized_version.as_str(),
                                                 snapshot,
                                                 effective_target);
        if (identity.is_err()) return Err(rstd::move(identity).unwrap_err());
        targets.push(ResolvedExternalTargetUsage {
            .name              = target.name.clone(),
            .visibility        = target.visibility,
            .compile_arguments = rstd::move(compile).unwrap(),
            .identity          = rstd::move(identity).unwrap(),
        });
    }
    auto identity = dependency_identity(
        provider, requirement, normalized_version.as_str(), snapshots, effective_target);
    if (identity.is_err()) return Err(rstd::move(identity).unwrap_err());
    return Ok(ResolvedExternalDependency {
        .alias    = requirement.alias.clone(),
        .provider = String::make("cmake"_str),
        .version  = rstd::move(normalized_version),
        .targets  = rstd::move(targets),
        .link_arguments =
            LinkArgumentSequence {
                .tokens   = as<rstd::clone::Clone>(snapshots.combined.link).clone(),
                .source   = rstd::format("CMake dependency '{}' package '{}'",
                                         requirement.alias.as_str(),
                                         requirement.package.as_str()),
                .identity = identity->clone(),
            },
        .identity = rstd::move(identity).unwrap(),
    });
}

} // namespace tenon
