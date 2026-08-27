module;
#include <rstd/macro.hpp>

export module lito.toolchain.clang:toolchain;

import rstd;
import lito.crypto;
import lito.core;
import lito.cpp;
import lito.toolchain.common;
import lito.system;
import lito.frontend;
import :arguments;
import :options;
import :preprocessor_environment;
import :support;
import :compile_executor;
import :strip;
import :standard_library_module;
import :target;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

export namespace lito
{

class ClangToolchain {
public:
    static auto create(const lito::config::ToolchainSpec& specification)
        -> ToolchainResult<ClangToolchain> {
        auto environment = ResolvedProcessEnvironment::resolve(ProcessEnvironmentSpec {});
        if (environment.is_err()) {
            return Err(rstd::into<ToolchainError>(rstd::move(environment).unwrap_err()));
        }
        return create(specification, lito::config::StandardLibrarySelection::Auto, *environment);
    }

    static auto create(const lito::config::ToolchainSpec& specification,
                       const ResolvedProcessEnvironment&  environment)
        -> ToolchainResult<ClangToolchain> {
        return create(specification, lito::config::StandardLibrarySelection::Auto, environment);
    }

    static auto create(const lito::config::ToolchainSpec&     specification,
                       lito::config::StandardLibrarySelection standard_library,
                       const ResolvedProcessEnvironment&      environment)
        -> ToolchainResult<ClangToolchain> {
        return create_impl(specification, standard_library, environment, nullptr);
    }

    static auto create_for_target(const lito::config::ToolchainSpec&     specification,
                                  lito::config::StandardLibrarySelection standard_library,
                                  const TargetInfo&                      target,
                                  const ResolvedProcessEnvironment&      environment)
        -> ToolchainResult<ClangToolchain> {
        return create_impl(specification, standard_library, environment, rstd::addressof(target));
    }

private:
    static auto create_impl(const lito::config::ToolchainSpec&     specification,
                            lito::config::StandardLibrarySelection standard_library,
                            const ResolvedProcessEnvironment&      environment,
                            const TargetInfo* requested_target) -> ToolchainResult<ClangToolchain> {
        auto argument_parser = make_clang_cpp_argument_parser();
        if (argument_parser.is_err()) {
            return Err(ToolchainError::Cpp(rstd::move(argument_parser).unwrap_err()));
        }
        const auto resolve = [&](ref<rstd::path::Path> requested,
                                 ref<str>              description) -> ToolchainResult<PathBuf> {
            auto located = environment.locate_executable(requested, description);
            if (located.is_err()) {
                return Err(rstd::into<ToolchainError>(rstd::move(located).unwrap_err()));
            }
            if (located->is_none()) {
                return failure<PathBuf>(rstd::format(
                    "cannot resolve {} '{}' from effective PATH", description, requested));
            }
            return Ok(rstd::move(located).unwrap().unwrap());
        };
        auto configured_compiler   = resolve(specification.cxx.as_path(), "clang++"_str);
        auto configured_c_compiler = resolve(specification.cc.as_path(), "clang"_str);
        auto configured_archiver   = resolve(specification.ar.as_path(), "llvm-ar"_str);
        if (configured_compiler.is_err()) {
            return Err(rstd::move(configured_compiler).unwrap_err());
        }
        if (configured_c_compiler.is_err()) {
            return Err(rstd::move(configured_c_compiler).unwrap_err());
        }
        if (configured_archiver.is_err()) {
            return Err(rstd::move(configured_archiver).unwrap_err());
        }
        auto compiler_path   = rstd::move(configured_compiler).unwrap();
        auto c_compiler_path = rstd::move(configured_c_compiler).unwrap();
        auto archiver_path   = rstd::move(configured_archiver).unwrap();

        auto compiler_command   = Vec<String>::make();
        auto c_compiler_command = Vec<String>::make();
        auto target_command     = Vec<String>::make();
        auto targets_command    = Vec<String>::make();
        auto resource_command   = Vec<String>::make();
        auto help_command       = Vec<String>::make();
        auto pushed = toolchain::command::push_path(compiler_command, compiler_path.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(compiler_command, toolchain::clang_options::VERSION);
        pushed = toolchain::command::push_path(c_compiler_command, c_compiler_path.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(c_compiler_command, toolchain::clang_options::VERSION);
        const auto query_default_target =
            requested_target == nullptr && specification.target.is_CompilerDefault();
        if (query_default_target) {
            pushed = toolchain::command::push_path(target_command, compiler_path.as_path());
            if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
            toolchain::command::push_option(target_command,
                                            toolchain::clang_options::PRINT_TARGET_TRIPLE);
        }
        pushed = toolchain::command::push_path(targets_command, compiler_path.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(targets_command, toolchain::clang_options::PRINT_TARGETS);
        pushed = toolchain::command::push_path(resource_command, compiler_path.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(resource_command,
                                        toolchain::clang_options::PRINT_RESOURCE_DIR);
        pushed = toolchain::command::push_path(help_command, compiler_path.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(help_command, toolchain::clang_options::HELP);

        auto compiler_version = toolchain::command::tool_output(
            rstd::move(compiler_command), "clang++ --version"_str, environment);
        auto c_compiler_version = toolchain::command::tool_output(
            rstd::move(c_compiler_command), "clang --version"_str, environment);
        auto archiver_identity = probe_archiver(archiver_path.as_path(), environment);
        auto supported_output  = toolchain::command::tool_output(
            rstd::move(targets_command), "clang++ supported target query"_str, environment);
        auto default_target_output = Option<String> {};
        if (query_default_target) {
            auto output = toolchain::command::tool_output(
                rstd::move(target_command), "clang++ default target query"_str, environment);
            if (output.is_err()) return Err(rstd::move(output).unwrap_err());
            default_target_output = Some(rstd::move(output).unwrap());
        }
        auto resource = toolchain::command::tool_output(
            rstd::move(resource_command), "clang++ resource query"_str, environment);
        auto help = toolchain::command::tool_output(
            rstd::move(help_command), "clang++ help query"_str, environment);
        if (compiler_version.is_err()) return Err(rstd::move(compiler_version).unwrap_err());
        if (c_compiler_version.is_err()) {
            return Err(rstd::move(c_compiler_version).unwrap_err());
        }
        if (archiver_identity.is_err()) return Err(rstd::move(archiver_identity).unwrap_err());
        if (supported_output.is_err()) return Err(rstd::move(supported_output).unwrap_err());
        if (resource.is_err()) return Err(rstd::move(resource).unwrap_err());
        if (help.is_err()) return Err(rstd::move(help).unwrap_err());
        if (! compiler_version->as_str().contains("clang version"_str)) {
            return failure<ClangToolchain>("configured compiler is not clang++"_str);
        }
        if (! c_compiler_version->as_str().contains("clang version"_str)) {
            return failure<ClangToolchain>("configured C compiler is not clang"_str);
        }
        auto supported_targets = ClangSupportedTargets::parse(supported_output->as_str());
        if (supported_targets.is_err()) {
            return Err(rstd::move(supported_targets).unwrap_err());
        }
        auto compiler_default = Option<TargetInfo> {};
        if (default_target_output.is_some()) {
            auto parsed = parse_target_info(default_target_output->as_str());
            if (parsed.is_err()) {
                return Err(ToolchainError::Platform(rstd::move(parsed).unwrap_err()));
            }
            compiler_default = Some(rstd::move(parsed).unwrap());
        }
        auto compile_target = Option<CompileTarget> {};
        if (requested_target != nullptr) {
            if (specification.target.is_Config()) {
                auto requested_os = target_operating_system(*requested_target);
                if (requested_os.is_err()) {
                    return Err(ToolchainError::Platform(rstd::move(requested_os).unwrap_err()));
                }
                if (*requested_os != specification.target.as_Config().os ||
                    requested_target->architecture !=
                        specification.target.as_Config().architecture) {
                    return failure<ClangToolchain>(rstd::format(
                        "configured toolchain target '{}-{}' conflicts with SDK target '{}'",
                        architecture_name(specification.target.as_Config().architecture),
                        operating_system_name(specification.target.as_Config().os),
                        requested_target->triple.as_str()));
                }
            }
            auto os = target_operating_system(*requested_target);
            if (os.is_err()) {
                return Err(ToolchainError::Platform(rstd::move(os).unwrap_err()));
            }
            auto selected_standard_library =
                resolve_standard_library_selection(standard_library, *os);
            if (selected_standard_library.is_err()) {
                return Err(rstd::move(selected_standard_library).unwrap_err());
            }
            auto resolved_target =
                resolve_sdk_compile_target(*requested_target, *selected_standard_library);
            if (resolved_target.is_err()) {
                return Err(rstd::move(resolved_target).unwrap_err());
            }
            compile_target = Some(rstd::move(resolved_target).unwrap());
        } else {
            auto input = resolve_toolchain_target_input(
                specification.target,
                compiler_default.is_some() ? rstd::addressof(*compiler_default) : nullptr);
            if (input.is_err()) return Err(rstd::move(input).unwrap_err());
            auto selected_standard_library =
                resolve_standard_library_selection(standard_library, input->os);
            if (selected_standard_library.is_err()) {
                return Err(rstd::move(selected_standard_library).unwrap_err());
            }
            auto resolved_target = resolve_compile_target(*input, *selected_standard_library);
            if (resolved_target.is_err()) {
                return Err(rstd::move(resolved_target).unwrap_err());
            }
            compile_target = Some(rstd::move(resolved_target).unwrap());
        }
        auto target_validation = supported_targets->validate(compile_target->info.architecture,
                                                             compile_target->info.triple.as_str());
        if (target_validation.is_err()) {
            return Err(rstd::move(target_validation).unwrap_err());
        }

        auto linker_path = Option<PathBuf> {};
        if (specification.ld.as_path().is_absolute()) {
            auto configured_linker = resolve(specification.ld.as_path(), "linker"_str);
            if (configured_linker.is_err()) {
                return Err(rstd::move(configured_linker).unwrap_err());
            }
            linker_path = Some(rstd::move(configured_linker).unwrap());
        } else {
            auto configured_name = specification.ld.as_path().to_str();
            if (configured_name.is_none() || *configured_name != "lld"_str) {
                return failure<ClangToolchain>(
                    "configured linker must be 'lld' or an absolute path to LLD"_str);
            }
            auto                        linker_name = lld_executable_name(compile_target->info);
            auto                        requested   = PathBuf::from(linker_name);
            const ref<rstd::path::Path> compilers[] = { compiler_path.as_path(),
                                                        c_compiler_path.as_path() };
            auto located = environment.locate_executable(requested.as_path(), "LLD linker"_str);
            if (located.is_err()) {
                return Err(rstd::into<ToolchainError>(rstd::move(located).unwrap_err()));
            }
            linker_path = rstd::move(located).unwrap();
            if (linker_path.is_none()) {
                for (auto compiler : compilers) {
                    auto parent = compiler.parent();
                    if (parent.is_none()) continue;
                    located = environment.locate_executable_in_directory(
                        *parent, requested.as_path(), "LLD linker"_str);
                    if (located.is_err()) {
                        return Err(rstd::into<ToolchainError>(rstd::move(located).unwrap_err()));
                    }
                    if (located->is_some()) {
                        linker_path = rstd::move(located).unwrap();
                        break;
                    }
                }
            }
            if (linker_path.is_none()) {
                auto       searched         = String::make();
                const auto append_directory = [&](ref<rstd::path::Path> directory) {
                    if (! searched.is_empty()) searched.push_str(", "_str);
                    searched.push_str(rstd::format("'{}'", directory).as_str());
                };
                for (const auto& directory : environment.search_directories()) {
                    append_directory(directory.as_path());
                }
                for (auto compiler : compilers) {
                    auto parent = compiler.parent();
                    if (parent.is_some()) append_directory(*parent);
                }
                return failure<ClangToolchain>(rstd::format(
                    "cannot resolve LLD frontend '{}' for target '{}' (architecture '{}', standard "
                    "library '{}'); searched {}",
                    linker_name,
                    compile_target->info.triple.as_str(),
                    architecture_name(compile_target->info.architecture),
                    lito::config::standard_library_name(compile_target->standard_library),
                    searched.is_empty() ? "<no directories>"_str : searched.as_str()));
            }
        }
        auto linker_identity = probe_linker(linker_path->as_path(), environment);
        if (linker_identity.is_err()) return Err(rstd::move(linker_identity).unwrap_err());
        if (linker_identity->family != LinkerFamily::Lld) {
            return failure<ClangToolchain>(
                rstd::format("configured linker '{}' is unsupported; expected LLD, got {}",
                             linker_identity->executable.as_path(),
                             linker_family_name(linker_identity->family)));
        }

        auto resource_path      = PathBuf::from(resource->as_str());
        auto canonical_resource = toolchain::command::resolve_path(resource_path.as_path(),
                                                                   "Clang resource directory"_str);
        if (canonical_resource.is_err()) {
            return Err(rstd::move(canonical_resource).unwrap_err());
        }

        auto resolved_resource = rstd::move(canonical_resource).unwrap();
        auto compiler_metadata = rstd::fs::metadata(compiler_path.as_path());
        if (compiler_metadata.is_err()) {
            return Err(ToolchainError::Io(String::make("inspect compiler"_str),
                                          compiler_path.clone(),
                                          rstd::move(compiler_metadata).unwrap_err()));
        }
        auto modified = compiler_metadata->modified();
        if (modified.is_err()) {
            return Err(ToolchainError::Io(String::make("read compiler modification time"_str),
                                          compiler_path.clone(),
                                          rstd::move(modified).unwrap_err()));
        }
        auto c_compiler_metadata = rstd::fs::metadata(c_compiler_path.as_path());
        if (c_compiler_metadata.is_err()) {
            return Err(ToolchainError::Io(String::make("inspect C compiler"_str),
                                          c_compiler_path.clone(),
                                          rstd::move(c_compiler_metadata).unwrap_err()));
        }
        auto c_modified = c_compiler_metadata->modified();
        if (c_modified.is_err()) {
            return Err(ToolchainError::Io(String::make("read C compiler modification time"_str),
                                          c_compiler_path.clone(),
                                          rstd::move(c_modified).unwrap_err()));
        }
        auto timestamp       = modified->as_unix_time();
        auto c_timestamp     = c_modified->as_unix_time();
        auto compiler_text   = compiler_path.as_path().to_str();
        auto c_compiler_text = c_compiler_path.as_path().to_str();
        auto resource_text   = resolved_resource.as_path().to_str();
        if (compiler_text.is_none() || c_compiler_text.is_none() || resource_text.is_none()) {
            return failure<ClangToolchain>(
                "Clang compiler or resource path is not valid UTF-8"_str);
        }
        auto build_identity =
            rstd::format("lito-clang-build-v4\n"
                         "cxx:{}\n{}\n{}:{}:{}\n"
                         "cc:{}\n{}\n{}:{}:{}\n"
                         "supported-targets:{}\n"
                         "target:{}\ntarget-source:{}\nstdlib:{}\nresource:{}",
                         *compiler_text,
                         compiler_version->as_str(),
                         compiler_metadata->size(),
                         timestamp.seconds,
                         timestamp.nanoseconds,
                         *c_compiler_text,
                         c_compiler_version->as_str(),
                         c_compiler_metadata->size(),
                         c_timestamp.seconds,
                         c_timestamp.nanoseconds,
                         supported_targets->identity(),
                         compile_target->info.triple.as_str(),
                         compile_target_source_name(compile_target->source),
                         lito::config::standard_library_name(compile_target->standard_library),
                         *resource_text);
        auto identity = CompilerIdentity {
            .path                   = compiler_path.clone(),
            .version                = rstd::move(compiler_version).unwrap(),
            .c_path                 = c_compiler_path.clone(),
            .c_version              = rstd::move(c_compiler_version).unwrap(),
            .target                 = compile_target->info.triple.clone(),
            .resource_directory     = resolved_resource.clone(),
            .build_identity         = build_identity.clone(),
            .size                   = compiler_metadata->size(),
            .modified_seconds       = timestamp.seconds,
            .modified_nanoseconds   = timestamp.nanoseconds,
            .c_size                 = c_compiler_metadata->size(),
            .c_modified_seconds     = c_timestamp.seconds,
            .c_modified_nanoseconds = c_timestamp.nanoseconds,
        };
        auto capabilities = cpp::CppToolchainCapabilities {
            .one_phase_bmi          = help->as_str().contains("-fmodule-output"_str),
            .exact_module_mapping   = help->as_str().contains("-fmodule-file"_str),
            .reduced_bmi            = help->as_str().contains("-fmodules-reduced-bmi"_str),
            .full_bmi_precompile    = help->as_str().contains("--precompile "_str),
            .reduced_bmi_precompile = help->as_str().contains("--precompile-reduced-bmi"_str),
            .source_embedding       = help->as_str().contains("-fmodules-embed-all-files"_str),
        };
        if (! capabilities.one_phase_bmi || ! capabilities.exact_module_mapping ||
            ! capabilities.reduced_bmi) {
            return failure<ClangToolchain>("configured Clang lacks required reduced BMI, one-phase "
                                           "BMI, or exact module mapping "
                                           "support"_str);
        }
        auto format = cpp::BmiFormatIdentity {
            .family               = String::make("clang"_str),
            .compiler_build       = rstd::move(build_identity),
            .target               = identity.target.clone(),
            .resource_environment = String::make(*resource_text),
        };

        return Ok(ClangToolchain { rstd::move(compiler_path),
                                   rstd::move(c_compiler_path),
                                   rstd::move(linker_identity).unwrap(),
                                   rstd::move(archiver_identity).unwrap(),
                                   rstd::move(resolved_resource),
                                   rstd::move(identity),
                                   rstd::move(compile_target).unwrap(),
                                   rstd::move(supported_targets).unwrap(),
                                   rstd::move(format),
                                   capabilities,
                                   rstd::move(argument_parser).unwrap(),
                                   environment.clone() });
    }

public:
    auto compiler_identity() const -> const CompilerIdentity& { return compiler_identity_; }
    auto linker_identity() const -> const LinkerIdentity& { return linker_identity_; }
    auto cxx_path() const -> ref<rstd::path::Path> { return compiler_.as_path(); }
    auto cc_path() const -> ref<rstd::path::Path> { return c_compiler_.as_path(); }
    auto ld_path() const -> ref<rstd::path::Path> { return linker_identity_.executable.as_path(); }
    auto ar_path() const -> ref<rstd::path::Path> {
        return archiver_identity_.executable.as_path();
    }
    auto target() const -> ref<str> { return compiler_identity_.target.as_str(); }
    auto target_info() const -> const TargetInfo& { return compile_target_.info; }
    auto compile_target() const -> const CompileTarget& { return compile_target_; }
    auto supported_targets() const -> const ClangSupportedTargets& { return supported_targets_; }
    auto resource_dir() const -> ref<rstd::path::Path> { return resource_dir_.as_path(); }
    auto bmi_format() const -> const cpp::BmiFormatIdentity& { return bmi_format_; }
    auto bmi_format(const BuildPlatform& platform) const -> cpp::BmiFormatIdentity {
        auto result   = bmi_format_.clone();
        result.target = platform.effective_target.triple.clone();
        if (platform.sdk_identity.is_some()) {
            result.resource_environment.push_str("\nsdk-identity="_str);
            result.resource_environment.push_str(platform.sdk_identity->as_str());
        }
        return result;
    }
    auto capabilities() const noexcept -> const cpp::CppToolchainCapabilities& {
        return capabilities_;
    }
    auto argument_parser() const noexcept -> const cpp::CppArgumentParser& {
        return argument_parser_;
    }

    auto resolve_standard_library(const cpp::CppCompileOptions& options,
                                  const TargetInfo&             target,
                                  const Vec<String>&            linker_options) const
        -> ToolchainResult<cpp::ResolvedStandardLibrary> {
        auto command = Vec<String>::make();
        auto pushed  = toolchain::command::push_path(command, compiler_.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(command, toolchain::clang_options::RESOURCE_DIR);
        pushed = toolchain::command::push_path(command, resource_dir_.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        command.push(rstd::format(
            "{}{}", toolchain::clang_options::STANDARD, options.language.standard.as_str()));
        auto stdlib_option =
            toolchain::clang_options::standard_library(options.abi.standard_library, target);
        if (! stdlib_option.is_empty()) toolchain::command::push_option(command, stdlib_option);
        append_typed_options(command, options, target, true);
        for (const auto& macro : options.preprocessor.macros) {
            command.push(
                rstd::format("{}{}",
                             macro.action == cpp::CppMacroAction::Define ? "-D"_str : "-U"_str,
                             macro.value.as_str()));
        }
        for (const auto& include : options.preprocessor.include_directories) {
            toolchain::command::push_option(command,
                                            include.kind == cpp::CppIncludeDirectoryKind::System
                                                ? "-isystem"_str
                                                : toolchain::clang_options::INCLUDE);
            pushed = toolchain::command::push_path(command, include.path.as_path());
            if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        }
        toolchain::command::push_option(command, "-dM"_str);
        toolchain::command::push_option(command, "-E"_str);
        toolchain::command::push_option(command, "-x"_str);
        toolchain::command::push_option(command, "c++"_str);
        toolchain::command::push_option(command, "-"_str);
        auto queried = run_command_with_input(command, "#include <version>\n"_str, environment_);
        if (queried.is_err()) {
            return Err(rstd::into<ToolchainError>(rstd::move(queried).unwrap_err()));
        }
        if (queried->exit_code != i32 {}) {
            return failure<cpp::ResolvedStandardLibrary>(
                rstd::format("cannot resolve selected C++ standard library headers\n{}\n{}",
                             command_text(command).as_str(),
                             queried->standard_error.as_str()));
        }

        auto detected_family = Option<lito::config::StandardLibrary> {};
        if (queried->standard_output.as_str().contains("_LIBCPP_VERSION"_str)) {
            detected_family = Some(lito::config::StandardLibrary::Libcxx);
        } else if (queried->standard_output.as_str().contains("__GLIBCXX__"_str)) {
            detected_family = Some(lito::config::StandardLibrary::Libstdcxx);
        } else if (queried->standard_output.as_str().contains("_MSVC_STL_VERSION"_str) ||
                   queried->standard_output.as_str().contains("_MSVC_STL_UPDATE"_str)) {
            detected_family = Some(lito::config::StandardLibrary::Msvc);
        }
        if (detected_family.is_none()) {
            return failure<cpp::ResolvedStandardLibrary>(rstd::format(
                "cannot identify selected C++ standard library for target '{}' from <version>",
                target.triple.as_str()));
        }
        if (*detected_family != options.abi.standard_library) {
            return failure<cpp::ResolvedStandardLibrary>(
                rstd::format("configured C++ standard library '{}' resolved to '{}' for target "
                             "'{}'; configure matching headers and libraries instead of relying "
                             "on an ignored driver option",
                             lito::config::standard_library_name(options.abi.standard_library),
                             lito::config::standard_library_name(*detected_family),
                             target.triple.as_str()));
        }

        auto library_name = options.abi.standard_library == lito::config::StandardLibrary::Libcxx
                                ? "libc++.so"_str
                                : "libstdc++.so"_str;
        if (target.environment == TargetEnvironment::Msvc) {
            library_name = options.abi.standard_library == lito::config::StandardLibrary::Msvc
                               ? "msvcprt.lib"_str
                               : "c++.lib"_str;
            if (options.common.microsoft_runtime_library.is_some() &&
                *options.common.microsoft_runtime_library ==
                    lito::compiler::MicrosoftRuntimeLibrary::DynamicDebug &&
                options.abi.standard_library == lito::config::StandardLibrary::Msvc) {
                library_name = "msvcprtd.lib"_str;
            }
        } else if (target.family == TargetFamily::Windows) {
            library_name = options.abi.standard_library == lito::config::StandardLibrary::Libcxx
                               ? "libc++.dll.a"_str
                               : "libstdc++.dll.a"_str;
        } else if (target.os.as_str() == "macos"_str) {
            library_name = options.abi.standard_library == lito::config::StandardLibrary::Libcxx
                               ? "libc++.dylib"_str
                               : "libstdc++.dylib"_str;
        }
        auto library_command = Vec<String>::make();
        pushed               = toolchain::command::push_path(library_command, compiler_.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        auto library_stdlib_option =
            toolchain::clang_options::standard_library(options.abi.standard_library, target);
        if (! library_stdlib_option.is_empty()) {
            toolchain::command::push_option(library_command, library_stdlib_option);
        }
        library_command.push(rstd::format("--target={}", target.triple.as_str()));
        if (options.common.target.sysroot.is_some()) {
            library_command.push(
                rstd::format("--sysroot={}", options.common.target.sysroot->as_str()));
        }
        library_command.push(rstd::format("-print-file-name={}", library_name));
        auto library = run_command(library_command, environment_);
        if (library.is_err()) {
            return Err(rstd::into<ToolchainError>(rstd::move(library).unwrap_err()));
        }
        if (library->exit_code != i32 {}) {
            return failure<cpp::ResolvedStandardLibrary>(
                rstd::format("cannot resolve selected C++ standard library artifact\n{}\n{}",
                             command_text(library_command).as_str(),
                             library->standard_error.as_str()));
        }
        auto binary_path_text = trim_ascii(rstd::move(library->standard_output));
        if (target.environment == TargetEnvironment::Msvc) {
            auto probe_command = Vec<String>::make();
            pushed             = toolchain::command::push_path(probe_command, compiler_.as_path());
            if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
            toolchain::command::push_option(probe_command, toolchain::clang_options::RESOURCE_DIR);
            pushed = toolchain::command::push_path(probe_command, resource_dir_.as_path());
            if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
            probe_command.push(rstd::format(
                "{}{}", toolchain::clang_options::STANDARD, options.language.standard.as_str()));
            if (! library_stdlib_option.is_empty()) {
                toolchain::command::push_option(probe_command, library_stdlib_option);
            }
            append_typed_options(probe_command, options, target, true);
            for (const auto& macro : options.preprocessor.macros) {
                probe_command.push(
                    rstd::format("{}{}",
                                 macro.action == cpp::CppMacroAction::Define ? "-D"_str : "-U"_str,
                                 macro.value.as_str()));
            }
            for (const auto& include : options.preprocessor.include_directories) {
                toolchain::command::push_option(probe_command,
                                                include.kind == cpp::CppIncludeDirectoryKind::System
                                                    ? "-isystem"_str
                                                    : toolchain::clang_options::INCLUDE);
                pushed = toolchain::command::push_path(probe_command, include.path.as_path());
                if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
            }
            pushed = push_clang_lld_selection(probe_command, linker_identity_.executable.as_path());
            if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
            toolchain::command::push_option(probe_command,
                                            toolchain::clang_options::LINKER_ARGUMENT);
            toolchain::command::push_option(probe_command, "/nodefaultlib:libcmt"_str);
            toolchain::command::push_option(probe_command,
                                            toolchain::clang_options::LINKER_ARGUMENT);
            toolchain::command::push_option(probe_command, "/nodefaultlib:libcmtd"_str);
            for (const auto& option : linker_options) probe_command.push(option.clone());
            auto standard_library_linker_option =
                toolchain::clang_options::standard_library_linker_option(
                    options.abi.standard_library, target, true);
            if (! standard_library_linker_option.is_empty()) {
                toolchain::command::push_option(probe_command, standard_library_linker_option);
            }
            toolchain::command::push_option(probe_command,
                                            toolchain::clang_options::LINKER_ARGUMENT);
            toolchain::command::push_option(probe_command, "/verbose"_str);
            toolchain::command::push_option(probe_command, "-x"_str);
            toolchain::command::push_option(probe_command, "c++"_str);
            toolchain::command::push_option(probe_command, "-"_str);
            toolchain::command::push_option(probe_command, "-o"_str);
            auto probe_path = rstd::env::temp_dir().join(
                PathBuf::from(
                    rstd::format("lito-stdlib-probe-{}-{}.exe",
                                 rstd::process::id(),
                                 lito::config::standard_library_name(options.abi.standard_library))
                        .as_str())
                    .as_path());
            pushed = toolchain::command::push_path(probe_command, probe_path.as_path());
            if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
            auto linked = run_command_with_input(
                probe_command,
                "#include <string>\nint main() { std::string value; return int(value.size()); }\n"_str,
                environment_);
            static_cast<void>(rstd::fs::remove_file(probe_path.as_path()));
            if (linked.is_err()) {
                return Err(rstd::into<ToolchainError>(rstd::move(linked).unwrap_err()));
            }
            if (linked->exit_code != i32 {}) {
                return failure<cpp::ResolvedStandardLibrary>(
                    rstd::format("cannot link selected dynamic C++ standard library\n{}\n{}\n{}",
                                 command_text(probe_command).as_str(),
                                 linked->standard_output.as_str(),
                                 linked->standard_error.as_str()));
            }
            auto trace = rstd::move(linked->standard_output);
            trace.push_str(linked->standard_error.as_str());
            const auto resolved_trace_entry = [&trace](ref<str> artifact) -> Option<String> {
                auto remaining = trace.as_str();
                while (auto found = remaining.find(artifact)) {
                    auto before     = remaining.split_at(*found).get<0>();
                    auto line_start = usize {};
                    auto previous   = before.rfind("\n"_str);
                    if (previous.is_some()) line_start = *previous + usize(1);
                    auto line = remaining.split_at(line_start).get<1>();
                    auto end  = line.find("\n"_str);
                    if (end.is_some()) line = line.split_at(*end).get<0>();
                    if (line.contains("Reading "_str) &&
                        (line.contains("\\"_str) || line.contains("/"_str))) {
                        return Some(String::make(line));
                    }
                    remaining = remaining.split_at(*found + artifact.len()).get<1>();
                }
                return None();
            };
            auto selected = resolved_trace_entry(library_name);
            if (selected.is_none()) {
                return failure<cpp::ResolvedStandardLibrary>(rstd::format(
                    "dynamic C++ standard library '{}' was not selected while linking target '{}'",
                    library_name,
                    target.triple.as_str()));
            }
            if (resolved_trace_entry("libcmt.lib"_str).is_some() ||
                resolved_trace_entry("libcmtd.lib"_str).is_some()) {
                return failure<cpp::ResolvedStandardLibrary>(
                    "dynamic Microsoft runtime probe selected a static CRT library"_str);
            }
            if (options.abi.standard_library == lito::config::StandardLibrary::Libcxx &&
                ! trace.as_str().contains("c++.dll"_str)) {
                return failure<cpp::ResolvedStandardLibrary>(
                    "Windows libc++ probe did not select a DLL import library"_str);
            }
            auto selected_path = selected->as_str().split_once("Reading "_str);
            if (selected_path.is_none()) {
                return failure<cpp::ResolvedStandardLibrary>(
                    "cannot decode selected standard library path from linker trace"_str);
            }
            binary_path_text = trim_ascii(String::make(selected_path->get<1>()));
        }
        auto binary_identity  = rstd::format("unresolved:{}", binary_path_text.as_str());
        auto binary_path      = PathBuf::from(binary_path_text.as_str());
        auto binary_metadata  = rstd::fs::metadata(binary_path.as_path());
        auto canonical_binary = binary_path.clone();
        if (binary_metadata.is_ok() && binary_metadata->is_file()) {
            auto modified  = binary_metadata->modified();
            auto timestamp = modified.is_ok() ? modified->as_unix_time() : rstd::time::UnixTime {};
            auto canonical = rstd::fs::canonicalize(binary_path.as_path());
            auto identity_path = canonical.is_ok() ? canonical->as_path() : binary_path.as_path();
            binary_identity    = rstd::format("path={}\nsize={}\nmodified={}:{}",
                                              identity_path,
                                              binary_metadata->size(),
                                              timestamp.seconds,
                                              timestamp.nanoseconds);
            if (canonical.is_ok()) canonical_binary = rstd::move(canonical).unwrap();
        }
        auto thread_backend = String::make("unknown"_str);
        if (queried->standard_output.as_str().contains("_LIBCPP_HAS_THREAD_API_PTHREAD"_str) ||
            queried->standard_output.as_str().contains("_GLIBCXX_HAS_GTHREADS"_str)) {
            thread_backend = String::make("pthread"_str);
        } else if (queried->standard_output.as_str().contains("_LIBCPP_HAS_THREAD_API_WIN32"_str)) {
            thread_backend = String::make("win32"_str);
        } else if (target.environment == TargetEnvironment::Msvc) {
            thread_backend = String::make("win32"_str);
        }
        auto family = lito::config::standard_library_name(*detected_family);
        auto headers_identity =
            rstd::format("clang-stdlib-headers-v3\nfamily={}\ntarget={}\nenvironment={}\n"
                         "runtime=dynamic\nmacros-sha256={}",
                         family,
                         target.triple.as_str(),
                         target.environment_name(),
                         lito::crypto::sha256_hex(queried->standard_output.as_str()).as_str());
        auto identity      = rstd::format("clang-stdlib-v2\n{}\nbinary={}\nthread-backend={}",
                                          headers_identity.as_str(),
                                          binary_identity.as_str(),
                                          thread_backend.as_str());
        auto manifest_name = options.abi.standard_library == lito::config::StandardLibrary::Libcxx
                                 ? "libc++.modules.json"_str
                                 : "libstdc++.modules.json"_str;
        auto manifest_candidates   = Vec<PathBuf>::make();
        const auto append_manifest = [&](ref<rstd::path::Path> artifact) {
            auto parent = artifact.parent();
            if (parent.is_none()) return;
            auto candidate = PathBuf::from(*parent).join(PathBuf::from(manifest_name).as_path());
            for (const auto& existing : manifest_candidates) {
                if (existing.as_path() == candidate.as_path()) return;
            }
            manifest_candidates.push(rstd::move(candidate));
        };
        append_manifest(binary_path.as_path());
        append_manifest(canonical_binary.as_path());
        return Ok(cpp::ResolvedStandardLibrary {
            .family             = *detected_family,
            .target             = target.triple.clone(),
            .artifact           = rstd::move(binary_path),
            .canonical_artifact = rstd::move(canonical_binary),
            .module_manifest =
                cpp::StandardLibraryModuleManifestCandidate {
                    .paths = rstd::move(manifest_candidates),
                },
            .headers_identity = rstd::move(headers_identity),
            .binary_identity  = rstd::move(binary_identity),
            .thread_backend   = rstd::move(thread_backend),
            .identity         = rstd::move(identity),
        });
    }

    auto resolve_standard_library_modules(const cpp::CppCompileOptions& options) const
        -> ToolchainResult<cpp::StandardLibraryModuleCatalog> {
        if (options.abi.resolved_standard_library.is_none()) {
            return failure<cpp::StandardLibraryModuleCatalog>(
                "C++ standard library must be resolved before its modules"_str);
        }
        return read_standard_library_module_catalog(*options.abi.resolved_standard_library);
    }

    auto validate(const cpp::CppCompileOptions& cpp, const cpp::BmiRequest& bmi) const
        -> ToolchainResult<empty> {
        if (! cpp::is_supported_cpp_standard(cpp.language.standard.as_str())) {
            return failure<empty>(
                rstd::format("unsupported C++ language standard '{}'; expected C++20 or later",
                             cpp.language.standard.as_str()));
        }
        if (bmi.representation == cpp::BmiRepresentation::Reduced && ! capabilities_.reduced_bmi) {
            return failure<empty>("configured Clang does not support reduced BMI"_str);
        }
        if (bmi.source_embedding == cpp::BmiSourceEmbeddingPolicy::EmbedAll &&
            ! capabilities_.source_embedding) {
            return failure<empty>(
                "configured Clang does not support embedding BMI source inputs"_str);
        }
        return Ok(empty {});
    }

    auto builtin_context(const cpp::CompileContext& context) const
        -> ToolchainResult<ClangBuiltinContext> {
        return make_builtin_context(context);
    }

    auto statistics() const -> ToolchainStatistics {
        auto result                             = toolchain_statistics_;
        result.target_queries                   = usize(1);
        result.preprocessor_environment_entries = preprocessor_environments_.len();
        result.builtin_snapshots                = builtin_environment_snapshots_.len();
        return result;
    }

    auto prepare(cpp::UnitSpec unit, ref<rstd::path::Path> working_directory) const
        -> ToolchainResult<cpp::PreparedUnit> {
        if (unit.context == nullptr) {
            return failure<cpp::PreparedUnit>("source unit has no compile context"_str);
        }
        if (cpp::source_language(unit) != cpp::compile_language(*unit.context)) {
            return failure<cpp::PreparedUnit>(rstd::format(
                "source '{}' does not match its compile context language", unit.source.as_path()));
        }
        auto object_parent = create_parent(unit.object.as_path());
        if (object_parent.is_err()) return Err(rstd::move(object_parent).unwrap_err());
        return Ok(cpp::PreparedUnit {
            .unit              = rstd::move(unit),
            .working_directory = PathBuf::from(working_directory),
        });
    }

    auto preprocessor_environment_identity(const cpp::CompileContext& compile_context,
                                           ref<rstd::path::Path>      working_directory) const
        -> ToolchainResult<String> {
        auto environment = environment_for(compile_context, working_directory);
        if (environment.is_err()) return Err(rstd::move(environment).unwrap_err());
        return Ok((*environment)->identity.clone());
    }

    auto build_tool_preprocessor_projection(const cpp::CompileContext& compile_context,
                                            ref<rstd::path::Path>      working_directory) const
        -> ToolchainResult<cpp::PreprocessorProjection> {
        auto environment = environment_for(compile_context, working_directory);
        if (environment.is_err()) return Err(rstd::move(environment).unwrap_err());
        auto       projection = cpp::preprocessor_projection(compile_context);
        const auto contains   = [&](ref<str> value) {
            for (const auto& directory : projection.user_include_directories) {
                if (directory.as_str() == value) return true;
            }
            for (const auto& directory : projection.system_include_directories) {
                if (directory.as_str() == value) return true;
            }
            return false;
        };
        for (const auto& entry : (*environment)->include_search) {
            auto directory = entry.directory.as_path().to_string_lossy();
            if (contains(directory.as_str())) continue;
            if (entry.system)
                projection.system_include_directories.push(rstd::move(directory));
            else
                projection.user_include_directories.push(rstd::move(directory));
        }
        projection.identity = lito::crypto::sha256_hex(
            rstd::format(
                "lito-build-tool-preprocessor-projection-v1\nprojection={}\nenvironment={}",
                projection.identity.as_str(),
                (*environment)->identity.as_str())
                .as_str());
        return Ok(rstd::move(projection));
    }

    auto header_roots(const cpp::CompileContext& compile_context,
                      ref<rstd::path::Path>      working_directory) const
        -> ToolchainResult<Vec<cpp::ResolvedHeaderRoot>> {
        auto environment = environment_for(compile_context, working_directory);
        if (environment.is_err()) return Err(rstd::move(environment).unwrap_err());
        auto       projection    = cpp::preprocessor_projection(compile_context);
        const auto is_configured = [&](ref<rstd::path::Path> directory) {
            const auto contains = [&](const Vec<String>& values) {
                for (const auto& value : values) {
                    auto path = PathBuf::from(value.as_str());
                    if (! path.as_path().is_absolute()) {
                        path = PathBuf::from(working_directory).join(path.as_path());
                    }
                    auto canonical = rstd::fs::canonicalize(path.as_path());
                    if (canonical.is_ok() && canonical->as_path() == directory) return true;
                }
                return false;
            };
            return contains(projection.user_include_directories) ||
                   contains(projection.system_include_directories);
        };
        auto roots = Vec<cpp::ResolvedHeaderRoot>::make();
        for (const auto& entry : (*environment)->include_search) {
            if (is_configured(entry.directory.as_path())) continue;
            roots.push(cpp::ResolvedHeaderRoot {
                .root   = entry.directory.clone(),
                .owner  = cpp::HeaderOwner::Toolchain(compiler_identity_.build_identity.clone()),
                .access = cpp::HeaderAccess::Global(),
                .kind =
                    entry.system ? cpp::HeaderIncludeKind::System : cpp::HeaderIncludeKind::User,
                .provenance = String::make("Clang default include search"_str),
            });
        }
        return Ok(rstd::move(roots));
    }

    auto prepare_scan_input(const cpp::CompileContext&         compile_context,
                            const cpp::PackageCompileMetadata& compile_metadata,
                            ref<rstd::path::Path>              working_directory) const
        -> ToolchainResult<toolchain::PreparedScanInput> {
        return prepare_scan_input_with_catalog(
            compile_context,
            toolchain::SharedPackageMacroCatalog::make(
                toolchain::PackageMacroCatalog::make(compile_metadata)),
            working_directory);
    }

    auto prepare_standard_library_scan_input(const cpp::CompileContext& compile_context,
                                             ref<rstd::path::Path>      working_directory) const
        -> ToolchainResult<toolchain::PreparedScanInput> {
        return prepare_scan_input_with_catalog(
            compile_context,
            toolchain::SharedPackageMacroCatalog::make(toolchain::PackageMacroCatalog::system()),
            working_directory);
    }

    auto preprocess(ref<rstd::path::Path>              source,
                    const cpp::CompileContext&         compile_context,
                    const cpp::PackageCompileMetadata& compile_metadata,
                    ref<rstd::path::Path>              working_directory,
                    frontend::FrontendService&         frontend_service) const
        -> ToolchainResult<frontend::UncachedFrontendAnalysis> {
        auto input = prepare_scan_input(compile_context, compile_metadata, working_directory);
        if (input.is_err()) {
            return Err(rstd::move(input).unwrap_err());
        }
        auto observer = preprocessor::IgnorePreprocessorObserver {};
        return preprocess_with_environment(source, *input, frontend_service, observer);
    }

    template<typename Observer>
    auto preprocess_with_environment(ref<rstd::path::Path>               source,
                                     const toolchain::PreparedScanInput& input,
                                     frontend::FrontendService&          frontend_service,
                                     Observer&                           observer) const
        -> ToolchainResult<frontend::UncachedFrontendAnalysis> {
        frontend_service.record_analysis_build();
        auto includes = toolchain::ClangIncludeResolver(*input.environment);
        auto embeds   = toolchain::ClangEmbedResolver(*input.environment);
        auto builtins = toolchain::ClangBuiltinProvider(
            *input.environment, input.environment->key.working_directory.as_path());
        auto pragmas = toolchain::ClangPragmaHandler {};
        auto events  = toolchain::DependencyEvents {};
        if (input.language == toolchain::PreprocessorLanguage::C) {
            auto identifiers = lito::c::CIdentifierTokenMatcher {};
            auto consumer    = frontend::parser::HeaderDependencyConsumer::make();
            auto translation = preprocessor::preprocess_with_embeds_to(
                preprocessor::PreprocessRequest {
                    .source                 = PathBuf::from(source),
                    .environment_identity   = input.environment->identity.clone(),
                    .retain_active_comments = false,
                },
                frontend_service,
                includes,
                embeds,
                builtins,
                *input.external_macros,
                identifiers,
                pragmas,
                events,
                consumer,
                observer);
            if (translation.is_err()) {
                return Err(rstd::into<ToolchainError>(rstd::move(translation).unwrap_err()));
            }
            translation->header_inputs = events.take_headers();
            auto parsed                = consumer.finish(*translation);
            if (parsed.is_err()) {
                return Err(rstd::into<ToolchainError>(rstd::move(parsed).unwrap_err()));
            }
            return Ok(frontend::UncachedFrontendAnalysis {
                .result          = rstd::move(parsed).unwrap(),
                .include_lookups = includes.take_dependencies(),
                .embed_lookups   = embeds.take_dependencies(),
            });
        }
        auto identifiers = cpp::CppIdentifierTokenMatcher {};
        auto consumer    = frontend::parser::ModuleDependencyConsumer::make();
        auto translation = preprocessor::preprocess_with_embeds_to(
            preprocessor::PreprocessRequest {
                .source                 = PathBuf::from(source),
                .environment_identity   = input.environment->identity.clone(),
                .retain_active_comments = false,
            },
            frontend_service,
            includes,
            embeds,
            builtins,
            *input.external_macros,
            identifiers,
            pragmas,
            events,
            consumer,
            observer);
        if (translation.is_err()) {
            return Err(rstd::into<ToolchainError>(rstd::move(translation).unwrap_err()));
        }
        translation->header_inputs = events.take_headers();
        auto parsed                = consumer.finish(*translation);
        if (parsed.is_err()) {
            return Err(rstd::into<ToolchainError>(rstd::move(parsed).unwrap_err()));
        }
        return Ok(frontend::UncachedFrontendAnalysis {
            .result          = rstd::move(parsed).unwrap(),
            .include_lookups = includes.take_dependencies(),
            .embed_lookups   = embeds.take_dependencies(),
        });
    }

private:
    auto prepare_scan_input_with_catalog(const cpp::CompileContext&           compile_context,
                                         toolchain::SharedPackageMacroCatalog external_macros,
                                         ref<rstd::path::Path> working_directory) const
        -> ToolchainResult<toolchain::PreparedScanInput> {
        if (compile_context.language.is_C()) {
            for (const auto& option : compile_context.language.as_C().options.vendor) {
                if (option.native_preprocessor_unsupported) {
                    return failure<toolchain::PreparedScanInput>(rstd::format(
                        "compiler option '{}' is not supported by the native preprocessor",
                        option.value.as_str()));
                }
            }
        } else {
            for (const auto& option : compile_context.language.as_Cpp().options.vendor) {
                if (option.native_preprocessor_unsupported) {
                    return failure<toolchain::PreparedScanInput>(rstd::format(
                        "compiler option '{}' is not supported by the native preprocessor",
                        option.value.as_str()));
                }
            }
        }
        auto environment = environment_for(compile_context, working_directory);
        if (environment.is_err()) return Err(rstd::move(environment).unwrap_err());
        return Ok(toolchain::PreparedScanInput {
            .environment     = rstd::move(environment).unwrap(),
            .external_macros = rstd::move(external_macros),
            .language = compile_context.language.is_C() ? toolchain::PreprocessorLanguage::C
                                                        : toolchain::PreprocessorLanguage::Cpp,
        });
    }

public:
    auto prepare_compile(
        const cpp::PreparedUnit&                  prepared,
        const cpp::ScanResult&                    scan_result,
        const Vec<cpp::ModuleArtifactDependency>& module_dependencies,
        cpp::CppCompileDisposition disposition = cpp::CppCompileDisposition::ObjectOnly) const
        -> ToolchainResult<CompileInvocation> {
        if (prepared.unit.context->language.is_C()) {
            if (! scan_result.language.is_C() || ! module_dependencies.is_empty() ||
                ! prepared.unit.language.is_C() ||
                disposition != cpp::CppCompileDisposition::ObjectOnly) {
                return failure<CompileInvocation>(
                    rstd::format("C source '{}' unexpectedly participates in the C++ module graph",
                                 prepared.unit.source.as_path()));
            }
            const auto& scan          = scan_result.language.as_C().facts.common;
            auto        staged_object = staging_path(prepared.unit.object.as_path());
            if (staged_object.is_err()) return Err(rstd::move(staged_object).unwrap_err());
            auto command = Vec<String>::make();
            auto context = append_compile_context(command, *prepared.unit.context, false);
            if (context.is_err()) return Err(rstd::move(context).unwrap_err());
            for (const auto& macro : scan.external_macros) {
                if (macro.state == frontend::ExternalMacroState::Undefined) continue;
                if (macro.compiler_definition.is_none()) {
                    return failure<CompileInvocation>(
                        "defined external macro has no compiler definition"_str);
                }
                command.push(rstd::format("-D{}", macro.compiler_definition->as_str()));
            }
            toolchain::command::push_option(command, toolchain::clang_options::COMPILE);
            auto pushed = toolchain::command::push_path(command, prepared.unit.source.as_path());
            if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
            toolchain::command::push_option(command, toolchain::clang_options::OUTPUT);
            pushed = toolchain::command::push_path(command, staged_object->as_path());
            if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
            auto identity = invocation_working_directory(prepared.working_directory.as_path());
            if (identity.is_err()) return Err(rstd::move(identity).unwrap_err());
            return Ok(CompileInvocation {
                .arguments                  = rstd::move(command),
                .working_directory          = prepared.working_directory.clone(),
                .identity_working_directory = rstd::move(identity).unwrap(),
                .staged_object              = rstd::move(staged_object).unwrap(),
                .final_object               = Some(prepared.unit.object.clone()),
            });
        }
        if (! scan_result.language.is_Cpp() || ! prepared.unit.language.is_Cpp()) {
            return failure<CompileInvocation>(rstd::format("C++ source '{}' received C scan facts",
                                                           prepared.unit.source.as_path()));
        }
        const auto& cpp_context  = prepared.unit.context->language.as_Cpp();
        const auto& source_unit  = prepared.unit.language.as_Cpp();
        const auto& scan         = scan_result.language.as_Cpp().facts;
        const auto  provides_bmi = scan.provided.is_some();
        if ((provides_bmi && disposition == cpp::CppCompileDisposition::ObjectOnly) ||
            (! provides_bmi && disposition != cpp::CppCompileDisposition::ObjectOnly)) {
            return failure<CompileInvocation>(
                rstd::format("compile output disposition does not match module facts for '{}'",
                             prepared.unit.source.as_path()));
        }
        auto valid = validate(cpp_context.options, cpp_context.bmi);
        if (valid.is_err()) return Err(rstd::move(valid).unwrap_err());
        auto staged_object = staging_path(prepared.unit.object.as_path());
        if (staged_object.is_err()) return Err(rstd::move(staged_object).unwrap_err());
        auto staged_bmi = Option<PathBuf> {};
        auto command    = Vec<String>::make();
        auto context    = append_compile_context(command, *prepared.unit.context, false);
        if (context.is_err()) return Err(rstd::move(context).unwrap_err());
        if (prepared.unit.owner.is_StandardLibrary()) {
            toolchain::command::push_option(command, "-Wno-reserved-module-identifier"_str);
        }
        for (const auto& macro : scan.common.external_macros) {
            if (macro.state == frontend::ExternalMacroState::Undefined) {
                if (macro.compiler_definition.is_some()) {
                    return failure<CompileInvocation>(
                        "undefined external macro has a compiler definition"_str);
                }
                continue;
            }
            if (macro.compiler_definition.is_none()) {
                return failure<CompileInvocation>(
                    "defined external macro has no compiler definition"_str);
            }
            command.push(rstd::format("-D{}", macro.compiler_definition->as_str()));
        }
        if (scan.provided.is_some()) {
            if (source_unit.bmi.is_none()) {
                return failure<CompileInvocation>(rstd::format("module unit has no BMI output: {}",
                                                               prepared.unit.source.as_path()));
            }
            auto parent = create_parent(source_unit.bmi->path.as_path());
            if (parent.is_err()) return Err(rstd::move(parent).unwrap_err());
            auto output = staging_path(source_unit.bmi->path.as_path());
            if (output.is_err()) return Err(rstd::move(output).unwrap_err());
            staged_bmi = Some(rstd::move(output).unwrap());
            toolchain::command::push_option(command, toolchain::clang_options::LANGUAGE);
            toolchain::command::push_option(command, toolchain::clang_options::CXX_MODULE);
            auto pushed = toolchain::command::push_path_option(
                command, toolchain::clang_options::MODULE_OUTPUT, staged_bmi->as_path());
            if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
            if (cpp_context.bmi.source_embedding == cpp::BmiSourceEmbeddingPolicy::EmbedAll) {
                toolchain::command::push_option(command, toolchain::clang_options::EMBED_ALL_FILES);
            }
        } else {
            auto extension      = prepared.unit.source.as_path().extension();
            auto extension_text = extension.is_some() ? (*extension).to_str() : None();
            if (extension_text.is_some() && *extension_text == "cppm"_str) {
                toolchain::command::push_option(command, toolchain::clang_options::LANGUAGE);
                toolchain::command::push_option(command, toolchain::clang_options::CXX_SOURCE);
            }
        }
        for (const auto& dependency : module_dependencies) {
            auto prefix = rstd::format(
                "{}{}=", toolchain::clang_options::MODULE_FILE, dependency.logical_name.as_str());
            auto pushed = toolchain::command::push_path_option(
                command, prefix.as_str(), dependency.path.as_path());
            if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        }
        toolchain::command::push_option(command, toolchain::clang_options::COMPILE);
        auto pushed = toolchain::command::push_path(command, prepared.unit.source.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(command, toolchain::clang_options::OUTPUT);
        pushed = toolchain::command::push_path(command, staged_object->as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());

        auto identity = invocation_working_directory(prepared.working_directory.as_path());
        if (identity.is_err()) return Err(rstd::move(identity).unwrap_err());
        return Ok(CompileInvocation {
            .arguments                  = rstd::move(command),
            .working_directory          = prepared.working_directory.clone(),
            .identity_working_directory = rstd::move(identity).unwrap(),
            .staged_object              = rstd::move(staged_object).unwrap(),
            .final_object               = disposition == cpp::CppCompileDisposition::BmiOnly
                                              ? Option<PathBuf> {}
                                              : Some(prepared.unit.object.clone()),
            .staged_bmi                 = rstd::move(staged_bmi),
            .final_bmi = source_unit.bmi.is_some() ? Some(source_unit.bmi->path.clone())
                                                   : Option<PathBuf> {},
        });
    }

    auto execute_compile(const CompileInvocation& invocation, ref<rstd::path::Path> source) const
        -> ToolchainResult<rstd::time::Duration> {
        auto output = execute_compile_capture(invocation);
        if (output.is_err()) {
            return Err(rstd::move(output).unwrap_err());
        }
        if (output->exit_code != i32 {}) {
            return failure<rstd::time::Duration>(
                rstd::format("clang++ failed for '{}'\n{}\n{}",
                             source,
                             command_text(invocation.arguments).as_str(),
                             output->standard_error.as_str()));
        }
        return Ok(output->elapsed);
    }

    auto compile_executor() const noexcept -> ClangCompileExecutor {
        return ClangCompileExecutor(environment_);
    }

    auto execute_compile_capture(const CompileInvocation& invocation) const
        -> ToolchainResult<CompileCommandResult> {
        return compile_executor().execute(invocation);
    }

    auto prepare_archive(ref<rstd::path::Path> output_path,
                         const Vec<PathBuf>&   objects,
                         ref<rstd::path::Path> working_directory) const
        -> ToolchainResult<ArchiveInvocation> {
        auto command = Vec<String>::make();
        rstd_try(toolchain::command::push_path(command, archiver_identity_.executable.as_path()));
        toolchain::command::push_option(command, toolchain::clang_options::ARCHIVE_CREATE);
        rstd_try(toolchain::command::push_path(command, output_path));
        for (const auto& object : objects) {
            rstd_try(toolchain::command::push_path(command, object.as_path()));
        }
        return Ok(ArchiveInvocation {
            .arguments         = rstd::move(command),
            .working_directory = PathBuf::from(working_directory),
            .output            = PathBuf::from(output_path),
            .archiver_identity = archiver_identity_.build_identity.clone(),
        });
    }

    auto execute_archive(const ArchiveInvocation& invocation) const
        -> ToolchainResult<rstd::time::Duration> {
        auto parent = create_parent(invocation.output.as_path());
        if (parent.is_err()) return Err(rstd::move(parent).unwrap_err());
        auto archive_exists = rstd::fs::exists(invocation.output.as_path());
        if (archive_exists.is_err()) {
            return Err(ToolchainError::Io(String::make("inspect archive"_str),
                                          invocation.output.clone(),
                                          rstd::move(archive_exists).unwrap_err()));
        }
        if (*archive_exists) {
            auto removed = rstd::fs::remove_file(invocation.output.as_path());
            if (removed.is_err()) {
                return Err(ToolchainError::Io(String::make("replace archive"_str),
                                              invocation.output.clone(),
                                              rstd::move(removed).unwrap_err()));
            }
        }
        auto output = run_command(
            invocation.arguments, environment_, Some(invocation.working_directory.as_path()));
        if (output.is_err()) {
            return Err(rstd::into<ToolchainError>(rstd::move(output).unwrap_err()));
        }
        auto command_output = rstd::move(output).unwrap();
        if (command_output.exit_code != i32 {}) {
            return failure<rstd::time::Duration>(
                rstd::format("llvm-ar failed for '{}'\n{}\n{}",
                             invocation.output.as_path(),
                             command_text(invocation.arguments).as_str(),
                             command_output.standard_error.as_str()));
        }
        return Ok(command_output.elapsed);
    }

    auto link_executable(ref<rstd::path::Path>              output_path,
                         const Vec<PathBuf>&                objects,
                         const Vec<ResolvedLinkInput>&      inputs,
                         const LinkTargetContext&           context,
                         const Option<lito::manifest::Lto>& lto,
                         const lito::link::Requirements&    link_requirements,
                         const Vec<String>&                 linker_options,
                         ref<rstd::path::Path>              working_directory) const
        -> ToolchainResult<rstd::time::Duration> {
        const auto& target                    = context.platform.effective_target;
        const auto  language                  = context.language;
        const auto  standard_library          = context.standard_library;
        const auto& microsoft_runtime_library = context.microsoft_runtime_library;
        if (lto.is_some() && *lto != lito::manifest::Lto::Off &&
            ! linker_identity_.capabilities.llvm_lto) {
            return failure<rstd::time::Duration>(
                rstd::format("configured {} does not support LLVM LTO option '{}'",
                             linker_family_name(linker_identity_.family),
                             cpp::cpp_lto_option(*lto)));
        }
        if (linker_identity_.family == LinkerFamily::GnuLd &&
            (target.family != TargetFamily::Unix || target.os.as_str() == "macos"_str ||
             target.triple.as_str() != compiler_identity_.target.as_str())) {
            return failure<rstd::time::Duration>(rstd::format(
                "configured GNU ld is only supported for the host ELF target '{}'; requested '{}'",
                compiler_identity_.target,
                target.triple));
        }
        auto parent = create_parent(output_path);
        if (parent.is_err()) return Err(rstd::move(parent).unwrap_err());
        auto command = Vec<String>::make();
        auto pushed  = toolchain::command::push_path(command,
                                                     language == lito::manifest::PackageLanguage::C
                                                         ? c_compiler_.as_path()
                                                         : compiler_.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        command.push(rstd::format("--target={}", target.triple.as_str()));
        if (context.platform.sysroot.is_some()) {
            pushed = toolchain::command::push_path_option(
                command, "--sysroot="_str, context.platform.sysroot->as_path());
            if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        }
        pushed = push_clang_lld_selection(command, linker_identity_.executable.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        if (context.output == LinkOutputKind::SharedLibrary) {
            if (target.family != TargetFamily::Unix || target.os.as_str() == "macos"_str) {
                return failure<rstd::time::Duration>(
                    rstd::format("ELF shared-library output is unsupported for target '{}'",
                                 target.triple.as_str()));
            }
            if (context.soname.is_none() || context.soname->is_empty() ||
                context.soname->as_str().contains("/"_str) ||
                context.soname->as_str().contains("\\"_str)) {
                return failure<rstd::time::Duration>(
                    "ELF shared-library output requires a safe SONAME"_str);
            }
            toolchain::command::push_option(command, "-shared"_str);
            command.push(rstd::format("-Wl,-soname,{}", context.soname->as_str()));
        }
        if (target.environment == TargetEnvironment::Msvc && microsoft_runtime_library.is_some() &&
            lito::compiler::microsoft_runtime_library_is_dynamic(*microsoft_runtime_library)) {
            // The GNU clang driver injects libcmt for an MSVC target even when all input objects
            // select the dynamic CRT through -fms-runtime-lib. Suppress only the incompatible
            // static CRT defaults; the object directives select msvcrt or msvcrtd.
            toolchain::command::push_option(command, toolchain::clang_options::LINKER_ARGUMENT);
            toolchain::command::push_option(command, "/nodefaultlib:libcmt"_str);
            toolchain::command::push_option(command, toolchain::clang_options::LINKER_ARGUMENT);
            toolchain::command::push_option(command, "/nodefaultlib:libcmtd"_str);
        }
        if (language == lito::manifest::PackageLanguage::Cpp) {
            auto stdlib_link_option = toolchain::clang_options::standard_library_linker_option(
                standard_library, target, context.link_standard_library);
            if (! stdlib_link_option.is_empty()) {
                toolchain::command::push_option(command, stdlib_link_option);
            }
            if (context.link_standard_library && target.os.as_str() == "android"_str &&
                context.standard_library_runtime == lito::config::StandardLibraryRuntime::Static) {
                toolchain::command::push_option(command, "-static-libstdc++"_str);
            }
        }
        auto lto_option = cpp::cpp_lto_option(lto);
        if (! lto_option.is_empty()) toolchain::command::push_option(command, lto_option);
        if (! link_requirements.runtime_search_paths.is_empty()) {
            toolchain::command::push_option(command, "-Wl,--enable-new-dtags"_str);
            for (const auto& requirement : link_requirements.runtime_search_paths) {
                auto option = String::make("-Wl,--rpath,"_str);
                option.push_str(requirement.path.as_str());
                command.push(rstd::move(option));
            }
        }
        for (const auto& option : linker_options) command.push(option.clone());
        for (const auto& object : objects) {
            pushed = toolchain::command::push_path(command, object.as_path());
            if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        }
        for (const auto& input : inputs) {
            if (input.is_External()) {
                for (const auto& token : input.as_External().arguments.tokens) {
                    command.push(token.clone());
                }
                continue;
            }
            if (input.is_SharedLibrary()) {
                pushed = toolchain::command::push_path(command,
                                                       input.as_SharedLibrary().library.as_path());
                if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
                continue;
            }
            const auto& archive = input.as_Archive().archive;
            if (archive.mode == LinkArchiveMode::Normal) {
                pushed = toolchain::command::push_path(command, archive.path.as_path());
                if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
                continue;
            }
            if (target.os.as_str() == "macos"_str) {
                toolchain::command::push_option(command, toolchain::clang_options::LINKER_ARGUMENT);
                toolchain::command::push_option(command, toolchain::clang_options::FORCE_LOAD);
                toolchain::command::push_option(command, toolchain::clang_options::LINKER_ARGUMENT);
                pushed = toolchain::command::push_path(command, archive.path.as_path());
                if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
                continue;
            }
            if (target.family == TargetFamily::Windows) {
                auto text = archive.path.as_path().to_str();
                if (text.is_none()) {
                    return failure<rstd::time::Duration>(rstd::format(
                        "whole-archive path '{}' is not valid UTF-8", archive.path.as_path()));
                }
                auto option = String::make("/WHOLEARCHIVE:"_str);
                option.push_str(*text);
                toolchain::command::push_option(command, toolchain::clang_options::LINKER_ARGUMENT);
                command.push(rstd::move(option));
                continue;
            }
            if (target.family != TargetFamily::Unix) {
                return failure<rstd::time::Duration>(
                    rstd::format("whole-archive linking is unsupported for target '{}'",
                                 target.triple.as_str()));
            }
            toolchain::command::push_option(command, toolchain::clang_options::WHOLE_ARCHIVE);
            pushed = toolchain::command::push_path(command, archive.path.as_path());
            if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
            toolchain::command::push_option(command, toolchain::clang_options::NO_WHOLE_ARCHIVE);
        }
        if (target.family == TargetFamily::Windows) {
            auto runtime_name =
                rstd::format("clang_rt.builtins-{}.lib", architecture_name(target.architecture));
            auto runtime_directory = resource_dir_.join(PathBuf::from("lib/windows"_str).as_path());
            auto runtime = runtime_directory.join(PathBuf::from(runtime_name.as_str()).as_path());
            pushed       = toolchain::command::push_path(command, runtime.as_path());
            if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        }
        if (link_requirements.posix_threads) {
            toolchain::command::push_option(command, "-pthread"_str);
        }
        for (const auto& requirement : link_requirements.system_libraries) {
            if (requirement.name.as_str() == "dl"_str && target.family == TargetFamily::Windows) {
                return failure<rstd::time::Duration>(rstd::format(
                    "system library 'dl' required by {} is unsupported for target '{}'",
                    requirement.source.as_str(),
                    target.triple.as_str()));
            }
            if (requirement.name.as_str() == "dl"_str && target.os.as_str() == "macos"_str)
                continue;
            auto option = target.family == TargetFamily::Windows ? requirement.name.clone()
                                                                 : String::make("-l"_str);
            option.push_str(target.family == TargetFamily::Windows ? ".lib"_str
                                                                   : requirement.name.as_str());
            command.push(rstd::move(option));
        }
        toolchain::command::push_option(command, toolchain::clang_options::OUTPUT);
        pushed = toolchain::command::push_path(command, output_path);
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());

        auto output = run_command(command, environment_, Some(working_directory));
        if (output.is_err()) {
            return Err(rstd::into<ToolchainError>(rstd::move(output).unwrap_err()));
        }
        auto command_output = rstd::move(output).unwrap();
        if (command_output.exit_code != i32 {}) {
            return failure<rstd::time::Duration>(rstd::format(
                "{} failed to link '{}'\n{}\n{}",
                language == lito::manifest::PackageLanguage::C ? "clang"_str : "clang++"_str,
                output_path,
                command_text(command).as_str(),
                command_output.standard_error.as_str()));
        }
        return Ok(command_output.elapsed);
    }

    auto link_shared_library(ref<rstd::path::Path>              output_path,
                             const Vec<PathBuf>&                objects,
                             const Vec<ResolvedLinkInput>&      inputs,
                             LinkTargetContext                  context,
                             const Option<lito::manifest::Lto>& lto,
                             const lito::link::Requirements&    link_requirements,
                             const Vec<String>&                 linker_options,
                             ref<rstd::path::Path>              working_directory) const
        -> ToolchainResult<rstd::time::Duration> {
        context.output = LinkOutputKind::SharedLibrary;
        return link_executable(output_path,
                               objects,
                               inputs,
                               context,
                               lto,
                               link_requirements,
                               linker_options,
                               working_directory);
    }

    auto link_elf_shared_library(const ElfSharedLibraryLinkRequest& request) const
        -> ToolchainResult<ElfSharedLibraryArtifact> {
        if (! linker_identity_.capabilities.elf_shared_library) {
            return failure<ElfSharedLibraryArtifact>(
                rstd::format("configured {} cannot link ELF shared libraries",
                             linker_family_name(linker_identity_.family)));
        }
        if (compile_target_.info.family != TargetFamily::Unix ||
            compile_target_.info.os.as_str() == "macos"_str) {
            return failure<ElfSharedLibraryArtifact>(rstd::format(
                "ELF shared-library linking requires a host ELF target; compiler target is '{}'",
                compile_target_.info.triple.as_str()));
        }
        if (request.soname.is_empty() || request.soname.as_str().contains("/"_str) ||
            request.soname.as_str().contains("\\"_str)) {
            return failure<ElfSharedLibraryArtifact>(
                rstd::format("ELF SONAME '{}' must be a file name", request.soname.as_str()));
        }
        auto archive_metadata = rstd::fs::metadata(request.archive.path.as_path());
        if (archive_metadata.is_err()) {
            return Err(ToolchainError::Io(String::make("inspect ELF shared-library archive"_str),
                                          request.archive.path.clone(),
                                          rstd::move(archive_metadata).unwrap_err()));
        }
        if (! archive_metadata->is_file()) {
            return failure<ElfSharedLibraryArtifact>(rstd::format(
                "ELF shared-library archive '{}' must be a file", request.archive.path.as_path()));
        }
        auto archive_contents = rstd::fs::read(request.archive.path.as_path());
        if (archive_contents.is_err()) {
            return Err(ToolchainError::Io(String::make("read ELF shared-library archive"_str),
                                          request.archive.path.clone(),
                                          rstd::move(archive_contents).unwrap_err()));
        }
        auto version_script = rstd::fs::read(request.version_script.as_path());
        if (version_script.is_err()) {
            return Err(
                ToolchainError::Io(String::make("read ELF shared-library version script"_str),
                                   request.version_script.clone(),
                                   rstd::move(version_script).unwrap_err()));
        }
        auto host = detect_host_info();
        if (host.is_err()) {
            return Err(ToolchainError::Platform(rstd::move(host).unwrap_err()));
        }
        auto platform = resolve_build_platform(*host, compile_target_.info, None());
        if (platform.is_err()) {
            return Err(ToolchainError::Platform(rstd::move(platform).unwrap_err()));
        }
        auto version_script_text = request.version_script.as_path().to_str();
        if (version_script_text.is_none()) {
            return failure<ElfSharedLibraryArtifact>(
                rstd::format("ELF version script path '{}' is not valid UTF-8",
                             request.version_script.as_path()));
        }
        auto inputs = Vec<ResolvedLinkInput>::make();
        inputs.push(ResolvedLinkInput::Archive(LinkArchive {
            .path = request.archive.path.clone(),
            .mode = request.archive.mode,
        }));
        auto linker_options = Vec<String>::make();
        linker_options.push(rstd::format("-Wl,--version-script={}", *version_script_text));
        linker_options.push(String::make("-Wl,--gc-sections"_str));
        linker_options.push(String::make("-Wl,--no-undefined"_str));
        auto linked = link_shared_library(request.output.as_path(),
                                          Vec<PathBuf>::make(),
                                          inputs,
                                          LinkTargetContext {
                                              .platform = rstd::move(platform).unwrap(),
                                              .language = lito::manifest::PackageLanguage::C,
                                              .link_standard_library = false,
                                              .soname                = Some(request.soname.clone()),
                                          },
                                          None(),
                                          lito::link::Requirements {},
                                          linker_options,
                                          request.working_directory.as_path());
        if (linked.is_err()) return Err(rstd::move(linked).unwrap_err());
        auto link_identity = rstd::format(
            "lito-elf-shared-link-v2\ncompiler:{}\nlinker:{}\narchive:{}:{}:{}\narchive-mode:{}\n"
            "soname:{}\nversion-script:{}\npolicy:gc-sections,no-undefined",
            compiler_identity_.build_identity.as_str(),
            linker_identity_.build_identity.as_str(),
            request.archive.path.as_path(),
            archive_metadata->size(),
            lito::crypto::sha256_hex(archive_contents->as_slice()).as_str(),
            request.archive.mode == LinkArchiveMode::Whole ? "whole"_str : "normal"_str,
            request.soname.as_str(),
            lito::crypto::sha256_hex(version_script->as_slice()).as_str());
        return Ok(ElfSharedLibraryArtifact {
            .file          = request.output.clone(),
            .soname        = request.soname.clone(),
            .link_identity = rstd::move(link_identity),
            .elapsed       = *linked,
        });
    }

    auto strip_artifact(ref<rstd::path::Path>     output_path,
                        ref<rstd::path::Path>     stripper,
                        lito::artifact::StripMode mode,
                        ref<rstd::path::Path>     working_directory) const
        -> ToolchainResult<rstd::time::Duration> {
        if (mode == lito::artifact::StripMode::None) return Ok(rstd::time::Duration {});
        auto staged = staging_path(output_path);
        if (staged.is_err()) return Err(rstd::move(staged).unwrap_err());
        rstd_try(clear_staged_output(staged->as_path()));
        auto provider = LlvmStrip(PathBuf::from(stripper), environment_);
        auto output   = provider.strip_to(output_path, staged->as_path(), mode, working_directory);
        if (output.is_err()) {
            static_cast<void>(rstd::fs::remove_file(staged->as_path()));
            return Err(rstd::move(output).unwrap_err());
        }
        rstd_try(publish_output(staged->as_path(), output_path));
        return Ok(*output);
    }

private:
    ClangToolchain(PathBuf                       compiler,
                   PathBuf                       c_compiler,
                   LinkerIdentity                linker_identity,
                   ArchiverIdentity              archiver_identity,
                   PathBuf                       resource_dir,
                   CompilerIdentity              identity,
                   CompileTarget                 compile_target,
                   ClangSupportedTargets         supported_targets,
                   cpp::BmiFormatIdentity        format,
                   cpp::CppToolchainCapabilities capabilities,
                   cpp::CppArgumentParser        argument_parser,
                   ResolvedProcessEnvironment    environment)
        : compiler_(rstd::move(compiler)),
          c_compiler_(rstd::move(c_compiler)),
          linker_identity_(rstd::move(linker_identity)),
          archiver_identity_(rstd::move(archiver_identity)),
          resource_dir_(rstd::move(resource_dir)),
          compiler_identity_(rstd::move(identity)),
          compile_target_(rstd::move(compile_target)),
          supported_targets_(rstd::move(supported_targets)),
          bmi_format_(rstd::move(format)),
          capabilities_(capabilities),
          argument_parser_(rstd::move(argument_parser)),
          environment_(rstd::move(environment)) {}

    auto environment_for(const cpp::CompileContext& compile_context,
                         ref<rstd::path::Path>      working_directory) const
        -> ToolchainResult<toolchain::SharedPreprocessorEnvironment> {
        auto working_text = working_directory.to_str();
        if (working_text.is_none()) {
            return failure<toolchain::SharedPreprocessorEnvironment>(rstd::format(
                "preprocessor working directory '{}' is not valid UTF-8", working_directory));
        }
        for (const auto& existing : preprocessor_environments_) {
            if (existing->key.matches(compile_context.scan_id.as_str(), working_directory)) {
                ++toolchain_statistics_.preprocessor_environment_hits;
                return Ok(existing.clone());
            }
        }

        auto builtin_context = make_builtin_context(compile_context);
        if (builtin_context.is_err()) {
            return Err(rstd::move(builtin_context).unwrap_err());
        }
        auto builtin_values  = rstd::move(builtin_context).unwrap();
        auto builtin_command = rstd::move(builtin_values.query_command);
        auto builtin_key     = rstd::move(builtin_values.key);
        toolchain_statistics_.ignored_builtin_options += builtin_values.ignored_options;
        auto builtin_environment = Option<toolchain::SharedClangBuiltinEnvironmentSnapshot> {};
        for (const auto& existing : builtin_environment_snapshots_) {
            if (existing->key.as_str() == builtin_key.as_str()) {
                builtin_environment = Some(existing.clone());
                ++toolchain_statistics_.builtin_hits;
                break;
            }
        }
        if (builtin_environment.is_none()) {
            auto queried =
                toolchain::query_clang_builtin_environment_snapshot(builtin_command,
                                                                    builtin_key.as_str(),
                                                                    builtin_values.semantic,
                                                                    builtin_values.language,
                                                                    working_directory,
                                                                    environment_);
            if (queried.is_err()) return Err(rstd::move(queried).unwrap_err());
            ++toolchain_statistics_.builtin_refreshes;
            ++toolchain_statistics_.builtin_macro_processes;
            ++toolchain_statistics_.builtin_capability_processes;
            toolchain_statistics_.clang_macros += (*queried)->clang_macro_count;
            toolchain_statistics_.native_macro_owners += (*queried)->native_macro_count;
            toolchain_statistics_.clang_capabilities += (*queried)->clang_capability_count;
            toolchain_statistics_.native_capabilities += (*queried)->native_capability_count;
            toolchain_statistics_.builtin_macro_output_bytes += (*queried)->macro_output_bytes;
            toolchain_statistics_.builtin_capability_input_bytes +=
                (*queried)->capability_input_bytes;
            toolchain_statistics_.builtin_capability_output_bytes +=
                (*queried)->capability_output_bytes;
            builtin_environment_snapshots_.push(queried->clone());
            builtin_environment = Some(rstd::move(queried).unwrap());
        }
        auto command = Vec<String>::make();
        auto context = append_compile_context(command, compile_context, true);
        if (context.is_err()) return Err(rstd::move(context).unwrap_err());
        ++toolchain_statistics_.preprocessor_environment_queries;
        auto macros = Vec<toolchain::PreprocessorMacroDirective>::make();
        if (compile_context.language.is_C()) {
            for (const auto& macro : compile_context.language.as_C().options.macros) {
                macros.push(toolchain::PreprocessorMacroDirective {
                    .defined = macro.action == lito::c::CMacroAction::Define,
                    .value   = macro.value.clone(),
                });
            }
        } else {
            for (const auto& macro :
                 compile_context.language.as_Cpp().options.preprocessor.macros) {
                macros.push(toolchain::PreprocessorMacroDirective {
                    .defined = macro.action == cpp::CppMacroAction::Define,
                    .value   = macro.value.clone(),
                });
            }
        }
        auto queried = toolchain::query_preprocessor_environment(
            command,
            toolchain::PreprocessorEnvironmentKey::make(compile_context.scan_id.as_str(),
                                                        working_directory),
            rstd::move(builtin_environment).unwrap(),
            rstd::move(builtin_values.semantic),
            builtin_values.language,
            macros,
            environment_);
        if (queried.is_err()) return Err(rstd::move(queried).unwrap_err());
        auto environment =
            rstd::sync::Arc<toolchain::PreprocessorEnvironment>::make(rstd::move(queried).unwrap());
        preprocessor_environments_.push(environment.clone());
        return Ok(rstd::move(environment));
    }

    auto make_builtin_context(const cpp::CompileContext& context) const
        -> ToolchainResult<ClangBuiltinContext> {
        if (context.language.is_C()) {
            const auto& c       = context.language.as_C().options;
            auto        command = Vec<String>::make();
            auto        pushed  = toolchain::command::push_path(command, c_compiler_.as_path());
            if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
            toolchain::command::push_option(command, toolchain::clang_options::RESOURCE_DIR);
            pushed = toolchain::command::push_path(command, resource_dir_.as_path());
            if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
            command.push(rstd::format("{}{}",
                                      toolchain::clang_options::STANDARD,
                                      lito::manifest::c_standard_name(c.standard)));
            append_c_typed_options(command, c, compile_target_.info, true);
            auto key = argument_identity("lito-clang-c-builtin-context-v1"_str, command);
            key.push_ascii('\n');
            key.push_str(compiler_identity_.c_version.as_str());
            key.push_ascii('\n');
            key.push_str(rstd::format("{}:{}:{}",
                                      compiler_identity_.c_size,
                                      compiler_identity_.c_modified_seconds,
                                      compiler_identity_.c_modified_nanoseconds)
                             .as_str());
            return Ok(ClangBuiltinContext {
                .query_command = rstd::move(command),
                .semantic =
                    toolchain::BuiltinSemanticContext {
                        .language_standard =
                            String::make(lito::manifest::c_standard_name(c.standard)),
                    },
                .key             = rstd::move(key),
                .language        = toolchain::PreprocessorLanguage::C,
                .ignored_options = usize(3),
            });
        }
        const auto& cpp_options = context.language.as_Cpp().options;
        if (! cpp::is_supported_cpp_standard(cpp_options.language.standard.as_str())) {
            return failure<ClangBuiltinContext>(
                rstd::format("unsupported C++ language standard '{}'; expected C++20 or later",
                             cpp_options.language.standard.as_str()));
        }
        auto command = Vec<String>::make();
        auto pushed  = toolchain::command::push_path(command, compiler_.as_path());
        if (pushed.is_err()) {
            return Err(rstd::move(pushed).unwrap_err());
        }
        toolchain::command::push_option(command, toolchain::clang_options::RESOURCE_DIR);
        pushed = toolchain::command::push_path(command, resource_dir_.as_path());
        if (pushed.is_err()) {
            return Err(rstd::move(pushed).unwrap_err());
        }
        command.push(rstd::format(
            "{}{}", toolchain::clang_options::STANDARD, cpp_options.language.standard.as_str()));
        auto stdlib_option = toolchain::clang_options::standard_library(
            cpp_options.abi.standard_library, compile_target_.info);
        if (! stdlib_option.is_empty()) toolchain::command::push_option(command, stdlib_option);
        append_typed_options(command, cpp_options, compile_target_.info, true);
        auto key = argument_identity("lito-clang-builtin-context-v4"_str, command);
        key.push_str(toolchain::CLANG_STANDARD_LIBRARY_CAPABILITY_ID);
        key.push_ascii('\n');
        key.push_str(compiler_identity_.version.as_str());
        key.push_ascii('\n');
        key.push_str(rstd::format("{}:{}:{}",
                                  compiler_identity_.size,
                                  compiler_identity_.modified_seconds,
                                  compiler_identity_.modified_nanoseconds)
                         .as_str());
        return Ok(ClangBuiltinContext {
            .query_command = rstd::move(command),
            .semantic =
                toolchain::BuiltinSemanticContext {
                    .language_standard = cpp_options.language.standard.clone(),
                    .rtti              = cpp_options.language.rtti,
                    .exceptions        = cpp_options.language.exceptions,
                },
            .key             = rstd::move(key),
            .language        = toolchain::PreprocessorLanguage::Cpp,
            .ignored_options = cpp_options.diagnostics.warnings.len() +
                               cpp_options.diagnostics.options.len() +
                               cpp_options.preprocessor.macros.len(),
        });
    }

    auto append_compile_context(Vec<String>&               command,
                                const cpp::CompileContext& context,
                                bool semantic_only) const -> ToolchainResult<empty> {
        if (context.language.is_C()) {
            const auto& c      = context.language.as_C().options;
            auto        pushed = toolchain::command::push_path(command, c_compiler_.as_path());
            if (pushed.is_err()) return pushed;
            toolchain::command::push_option(command, toolchain::clang_options::RESOURCE_DIR);
            pushed = toolchain::command::push_path(command, resource_dir_.as_path());
            if (pushed.is_err()) return pushed;
            command.push(rstd::format("{}{}",
                                      toolchain::clang_options::STANDARD,
                                      lito::manifest::c_standard_name(c.standard)));
            append_c_typed_options(command, c, compile_target_.info, semantic_only);
            for (const auto& macro : c.macros) {
                command.push(rstd::format("{}{}",
                                          macro.action == lito::c::CMacroAction::Define ? "-D"_str
                                                                                        : "-U"_str,
                                          macro.value.as_str()));
            }
            for (const auto& include : c.include_directories) {
                toolchain::command::push_option(command,
                                                include.kind ==
                                                        lito::c::CIncludeDirectoryKind::System
                                                    ? "-isystem"_str
                                                    : toolchain::clang_options::INCLUDE);
                pushed = toolchain::command::push_path(command, include.path.as_path());
                if (pushed.is_err()) return pushed;
            }
            return Ok(empty {});
        }
        const auto& cpp_context = context.language.as_Cpp();
        const auto& cpp_options = cpp_context.options;
        auto        pushed      = toolchain::command::push_path(command, compiler_.as_path());
        if (pushed.is_err()) return pushed;
        toolchain::command::push_option(command, toolchain::clang_options::RESOURCE_DIR);
        pushed = toolchain::command::push_path(command, resource_dir_.as_path());
        if (pushed.is_err()) return pushed;
        command.push(rstd::format(
            "{}{}", toolchain::clang_options::STANDARD, cpp_options.language.standard.as_str()));
        auto stdlib_option = toolchain::clang_options::standard_library(
            cpp_options.abi.standard_library, compile_target_.info);
        if (! stdlib_option.is_empty()) toolchain::command::push_option(command, stdlib_option);
        toolchain::command::push_option(
            command, toolchain::clang_options::bmi(cpp_context.bmi.representation));
        append_typed_options(command, cpp_options, compile_target_.info, semantic_only);
        for (const auto& macro : cpp_options.preprocessor.macros) {
            command.push(
                rstd::format("{}{}",
                             macro.action == cpp::CppMacroAction::Define ? "-D"_str : "-U"_str,
                             macro.value.as_str()));
        }
        for (const auto& include : cpp_options.preprocessor.include_directories) {
            toolchain::command::push_option(command,
                                            include.kind == cpp::CppIncludeDirectoryKind::System
                                                ? "-isystem"_str
                                                : toolchain::clang_options::INCLUDE);
            pushed = toolchain::command::push_path(command, include.path.as_path());
            if (pushed.is_err()) return pushed;
        }
        return Ok(empty {});
    }

    PathBuf                                                       compiler_;
    PathBuf                                                       c_compiler_;
    LinkerIdentity                                                linker_identity_;
    ArchiverIdentity                                              archiver_identity_;
    PathBuf                                                       resource_dir_;
    CompilerIdentity                                              compiler_identity_;
    CompileTarget                                                 compile_target_;
    ClangSupportedTargets                                         supported_targets_;
    cpp::BmiFormatIdentity                                        bmi_format_;
    cpp::CppToolchainCapabilities                                 capabilities_;
    cpp::CppArgumentParser                                        argument_parser_;
    ResolvedProcessEnvironment                                    environment_;
    mutable ToolchainStatistics                                   toolchain_statistics_;
    mutable Vec<toolchain::SharedClangBuiltinEnvironmentSnapshot> builtin_environment_snapshots_;
    mutable Vec<toolchain::SharedPreprocessorEnvironment>         preprocessor_environments_;
};

} // namespace lito
