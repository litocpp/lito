module;
#include <rstd/macro.hpp>

export module lito.toolchain.clang:toolchain;

import rstd;
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
        auto resolver = ToolResolver(*environment);
        return create(specification, resolver, *environment);
    }

    static auto create(const lito::config::ToolchainSpec& specification,
                       ToolResolver&                      resolver,
                       const ResolvedProcessEnvironment&  environment)
        -> ToolchainResult<ClangToolchain> {
        auto argument_parser = make_clang_cpp_argument_parser();
        if (argument_parser.is_err()) {
            return Err(ToolchainError::Cpp(rstd::move(argument_parser).unwrap_err()));
        }
        auto configured_compiler   = resolver.resolve(specification.cxx.as_path(), "clang++"_str);
        auto configured_c_compiler = resolver.resolve(specification.cc.as_path(), "clang"_str);
        auto configured_linker     = resolver.resolve(specification.ld.as_path(), "LLD linker"_str);
        auto configured_archiver   = resolver.resolve(specification.ar.as_path(), "llvm-ar"_str);
        if (configured_compiler.is_err()) {
            return Err(rstd::into<ToolchainError>(rstd::move(configured_compiler).unwrap_err()));
        }
        if (configured_c_compiler.is_err()) {
            return Err(rstd::into<ToolchainError>(rstd::move(configured_c_compiler).unwrap_err()));
        }
        if (configured_linker.is_err()) {
            return Err(rstd::into<ToolchainError>(rstd::move(configured_linker).unwrap_err()));
        }
        if (configured_archiver.is_err()) {
            return Err(rstd::into<ToolchainError>(rstd::move(configured_archiver).unwrap_err()));
        }
        auto compiler_path   = rstd::move(configured_compiler).unwrap().executable;
        auto c_compiler_path = rstd::move(configured_c_compiler).unwrap().executable;
        auto linker_path     = rstd::move(configured_linker).unwrap().executable;
        auto archiver_path   = rstd::move(configured_archiver).unwrap().executable;

        auto compiler_command   = Vec<String>::make();
        auto c_compiler_command = Vec<String>::make();
        auto linker_command     = Vec<String>::make();
        auto target_command     = Vec<String>::make();
        auto resource_command   = Vec<String>::make();
        auto help_command       = Vec<String>::make();
        auto pushed = toolchain::command::push_path(compiler_command, compiler_path.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(compiler_command, toolchain::clang_options::VERSION);
        pushed = toolchain::command::push_path(c_compiler_command, c_compiler_path.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(c_compiler_command, toolchain::clang_options::VERSION);
        pushed = toolchain::command::push_path(linker_command, linker_path.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(linker_command, toolchain::clang_options::VERSION);
        pushed = toolchain::command::push_path(target_command, compiler_path.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(target_command,
                                        toolchain::clang_options::PRINT_TARGET_TRIPLE);
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
        auto linker_version = toolchain::command::tool_output(
            rstd::move(linker_command), "LLD --version"_str, environment);
        auto target = toolchain::command::tool_output(
            rstd::move(target_command), "clang++ target query"_str, environment);
        auto resource = toolchain::command::tool_output(
            rstd::move(resource_command), "clang++ resource query"_str, environment);
        auto help = toolchain::command::tool_output(
            rstd::move(help_command), "clang++ help query"_str, environment);
        if (compiler_version.is_err()) return Err(rstd::move(compiler_version).unwrap_err());
        if (c_compiler_version.is_err()) {
            return Err(rstd::move(c_compiler_version).unwrap_err());
        }
        if (linker_version.is_err()) return Err(rstd::move(linker_version).unwrap_err());
        if (target.is_err()) return Err(rstd::move(target).unwrap_err());
        if (resource.is_err()) return Err(rstd::move(resource).unwrap_err());
        if (help.is_err()) return Err(rstd::move(help).unwrap_err());
        if (! compiler_version->as_str().contains("clang version"_str)) {
            return failure<ClangToolchain>("configured compiler is not clang++"_str);
        }
        if (! c_compiler_version->as_str().contains("clang version"_str)) {
            return failure<ClangToolchain>("configured C compiler is not clang"_str);
        }
        if (! linker_version->as_str().contains("LLD"_str)) {
            return failure<ClangToolchain>("configured linker is not LLD"_str);
        }
        auto target_info = parse_target_info(target->as_str());
        if (target_info.is_err()) {
            return Err(ToolchainError::Platform(rstd::move(target_info).unwrap_err()));
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
        auto timestamp     = modified->as_unix_time();
        auto compiler_text = compiler_path.as_path().to_str();
        auto resource_text = resolved_resource.as_path().to_str();
        if (compiler_text.is_none() || resource_text.is_none()) {
            return failure<ClangToolchain>(
                "Clang compiler or resource path is not valid UTF-8"_str);
        }
        auto build_identity = rstd::format("lito-clang-build-v1\n{}\n{}\n{}\n{}\n{}:{}:{}",
                                           *compiler_text,
                                           compiler_version->as_str(),
                                           target->as_str(),
                                           *resource_text,
                                           compiler_metadata->size(),
                                           timestamp.seconds,
                                           timestamp.nanoseconds);
        auto identity       = CompilerIdentity {
            .path                 = compiler_path.clone(),
            .version              = rstd::move(compiler_version).unwrap(),
            .target               = rstd::move(target).unwrap(),
            .resource_directory   = resolved_resource.clone(),
            .build_identity       = build_identity.clone(),
            .size                 = compiler_metadata->size(),
            .modified_seconds     = timestamp.seconds,
            .modified_nanoseconds = timestamp.nanoseconds,
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
                                   rstd::move(linker_path),
                                   rstd::move(archiver_path),
                                   rstd::move(resolved_resource),
                                   rstd::move(identity),
                                   rstd::move(target_info).unwrap(),
                                   rstd::move(format),
                                   capabilities,
                                   rstd::move(argument_parser).unwrap(),
                                   environment.clone() });
    }

    auto compiler_identity() const -> const CompilerIdentity& { return compiler_identity_; }
    auto cxx_path() const -> ref<rstd::path::Path> { return compiler_.as_path(); }
    auto cc_path() const -> ref<rstd::path::Path> { return c_compiler_.as_path(); }
    auto ld_path() const -> ref<rstd::path::Path> { return linker_.as_path(); }
    auto ar_path() const -> ref<rstd::path::Path> { return archiver_.as_path(); }
    auto target() const -> ref<str> { return compiler_identity_.target.as_str(); }
    auto target_info() const -> const TargetInfo& { return target_info_; }
    auto resource_dir() const -> ref<rstd::path::Path> { return resource_dir_.as_path(); }
    auto bmi_format() const -> const cpp::BmiFormatIdentity& { return bmi_format_; }
    auto capabilities() const noexcept -> const cpp::CppToolchainCapabilities& {
        return capabilities_;
    }
    auto argument_parser() const noexcept -> const cpp::CppArgumentParser& {
        return argument_parser_;
    }

    auto resolve_standard_library(const cpp::CppCompileOptions& options,
                                  const TargetInfo&             target) const
        -> ToolchainResult<cpp::ResolvedStandardLibrary> {
        auto command = Vec<String>::make();
        auto pushed  = toolchain::command::push_path(command, compiler_.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(command, toolchain::clang_options::RESOURCE_DIR);
        pushed = toolchain::command::push_path(command, resource_dir_.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        command.push(rstd::format(
            "{}{}", toolchain::clang_options::STANDARD, options.language.standard.as_str()));
        toolchain::command::push_option(
            command, toolchain::clang_options::standard_library(options.abi.standard_library));
        if (options.target.target.is_some()) {
            command.push(rstd::format("--target={}", options.target.target->as_str()));
        }
        if (options.target.sysroot.is_some()) {
            command.push(rstd::format("--sysroot={}", options.target.sysroot->as_str()));
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

        auto library_name = options.abi.standard_library == lito::config::StandardLibrary::Libcxx
                                ? "libc++.so"_str
                                : "libstdc++.so"_str;
        if (target.family == TargetFamily::Windows) {
            library_name = options.abi.standard_library == lito::config::StandardLibrary::Libcxx
                               ? "libc++.a"_str
                               : "libstdc++.a"_str;
        } else if (target.os.as_str() == "macos"_str) {
            library_name = options.abi.standard_library == lito::config::StandardLibrary::Libcxx
                               ? "libc++.dylib"_str
                               : "libstdc++.dylib"_str;
        }
        auto library_command = Vec<String>::make();
        pushed               = toolchain::command::push_path(library_command, compiler_.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(
            library_command,
            toolchain::clang_options::standard_library(options.abi.standard_library));
        if (options.target.target.is_some()) {
            library_command.push(rstd::format("--target={}", options.target.target->as_str()));
        }
        if (options.target.sysroot.is_some()) {
            library_command.push(rstd::format("--sysroot={}", options.target.sysroot->as_str()));
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
        auto binary_identity  = rstd::format("unresolved:{}", binary_path_text.as_str());
        auto binary_path      = PathBuf::from(binary_path_text.as_str());
        auto binary_metadata  = rstd::fs::metadata(binary_path.as_path());
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
        }
        auto thread_backend = String::make("unknown"_str);
        if (queried->standard_output.as_str().contains("_LIBCPP_HAS_THREAD_API_PTHREAD"_str) ||
            queried->standard_output.as_str().contains("_GLIBCXX_HAS_GTHREADS"_str)) {
            thread_backend = String::make("pthread"_str);
        } else if (queried->standard_output.as_str().contains("_LIBCPP_HAS_THREAD_API_WIN32"_str)) {
            thread_backend = String::make("win32"_str);
        }
        auto family = options.abi.standard_library == lito::config::StandardLibrary::Libcxx
                          ? "libc++"_str
                          : "libstdc++"_str;
        auto headers_identity =
            rstd::format("clang-stdlib-headers-v2\nfamily={}\ntarget={}\nmacros-sha256={}",
                         family,
                         target.triple.as_str(),
                         rstd::crypto::sha256_hex(queried->standard_output.as_str()).as_str());
        auto identity = rstd::format("clang-stdlib-v1\n{}\nbinary={}\nthread-backend={}",
                                     headers_identity.as_str(),
                                     binary_identity.as_str(),
                                     thread_backend.as_str());
        return Ok(cpp::ResolvedStandardLibrary {
            .family           = options.abi.standard_library,
            .headers_identity = rstd::move(headers_identity),
            .binary_identity  = rstd::move(binary_identity),
            .thread_backend   = rstd::move(thread_backend),
            .identity         = rstd::move(identity),
        });
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
        auto object_parent = create_parent(unit.object.as_path());
        if (object_parent.is_err()) return Err(rstd::move(object_parent).unwrap_err());
        return Ok(cpp::PreparedUnit {
            .unit              = rstd::move(unit),
            .working_directory = PathBuf::from(working_directory),
        });
    }

    auto scan(const cpp::PreparedUnit& prepared) const -> ToolchainResult<cpp::ScanResult> {
        if (prepared.frontend_analysis.is_none()) {
            return failure<cpp::ScanResult>(rstd::format("source '{}' has no frontend analysis",
                                                         prepared.unit.source.as_path()));
        }
        return Ok(cpp::scan_from_frontend(prepared.frontend_analysis->result, prepared.unit.id));
    }

    auto preprocessor_environment_identity(const cpp::CompileContext& compile_context,
                                           ref<rstd::path::Path>      working_directory) const
        -> ToolchainResult<String> {
        auto environment = environment_for(compile_context, working_directory);
        if (environment.is_err()) return Err(rstd::move(environment).unwrap_err());
        return Ok((*environment)->identity.clone());
    }

    auto prepare_scan_input(const cpp::CompileContext&        compile_context,
                            const cpp::PackageCompileMetadata& compile_metadata,
                            ref<rstd::path::Path>              working_directory) const
        -> ToolchainResult<toolchain::PreparedScanInput> {
        for (const auto& option : compile_context.cpp.vendor) {
            if (option.native_preprocessor_unsupported) {
                return failure<toolchain::PreparedScanInput>(
                    rstd::format("compiler option '{}' is not supported by the native preprocessor",
                                 option.value.as_str()));
            }
        }
        auto environment = environment_for(compile_context, working_directory);
        if (environment.is_err()) return Err(rstd::move(environment).unwrap_err());
        return Ok(toolchain::PreparedScanInput {
            .environment = rstd::move(environment).unwrap(),
            .external_macros = toolchain::SharedPackageMacroCatalog::make(
                toolchain::PackageMacroCatalog::make(compile_metadata)),
        });
    }

    auto preprocess(ref<rstd::path::Path>      source,
                    const cpp::CompileContext& compile_context,
                    const cpp::PackageCompileMetadata& compile_metadata,
                    ref<rstd::path::Path>      working_directory,
                    frontend::FrontendService& frontend_service) const
        -> ToolchainResult<frontend::UncachedFrontendAnalysis> {
        auto input = prepare_scan_input(compile_context, compile_metadata, working_directory);
        if (input.is_err()) {
            return Err(rstd::move(input).unwrap_err());
        }
        auto observer = preprocessor::IgnorePreprocessorObserver {};
        return preprocess_with_environment(source, *input, frontend_service, observer);
    }

    template<typename Observer>
    auto preprocess_with_environment(ref<rstd::path::Path>                           source,
                                     const toolchain::PreparedScanInput&              input,
                                     frontend::FrontendService& frontend_service,
                                     Observer&                  observer) const
        -> ToolchainResult<frontend::UncachedFrontendAnalysis> {
        frontend_service.record_analysis_build();
        auto includes = toolchain::ClangIncludeResolver(*input.environment);
        auto builtins = toolchain::ClangBuiltinProvider(
            *input.environment, input.environment->key.working_directory.as_path());
        auto identifiers = cpp::CppIdentifierTokenMatcher {};
        auto pragmas     = toolchain::ClangPragmaHandler {};
        auto events      = toolchain::DependencyEvents {};
        auto consumer    = frontend::parser::ModuleDependencyConsumer::make();
        auto translation = preprocessor::preprocess_to(
            preprocessor::PreprocessRequest {
                .source               = PathBuf::from(source),
                .environment_identity = input.environment->identity.clone(),
            },
            frontend_service,
            includes,
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
        });
    }

    auto prepare_compile(const cpp::PreparedUnit&                  prepared,
                         const cpp::ScanResult&                    scan_result,
                         const Vec<cpp::ModuleArtifactDependency>& module_dependencies) const
        -> ToolchainResult<CompileInvocation> {
        auto valid = validate(prepared.unit.context->cpp, prepared.unit.context->bmi);
        if (valid.is_err()) return Err(rstd::move(valid).unwrap_err());
        auto staged_object = staging_path(prepared.unit.object.as_path());
        if (staged_object.is_err()) return Err(rstd::move(staged_object).unwrap_err());
        auto staged_bmi = Option<PathBuf> {};
        auto command    = Vec<String>::make();
        auto context    = append_compile_context(command, *prepared.unit.context, false);
        if (context.is_err()) return Err(rstd::move(context).unwrap_err());
        for (const auto& macro : scan_result.external_macros) {
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
        if (scan_result.provided.is_some()) {
            if (prepared.unit.bmi.is_none()) {
                return failure<CompileInvocation>(rstd::format("module unit has no BMI output: {}",
                                                               prepared.unit.source.as_path()));
            }
            auto parent = create_parent(prepared.unit.bmi->path.as_path());
            if (parent.is_err()) return Err(rstd::move(parent).unwrap_err());
            auto output = staging_path(prepared.unit.bmi->path.as_path());
            if (output.is_err()) return Err(rstd::move(output).unwrap_err());
            staged_bmi = Some(rstd::move(output).unwrap());
            toolchain::command::push_option(command, toolchain::clang_options::LANGUAGE);
            toolchain::command::push_option(command, toolchain::clang_options::CXX_MODULE);
            auto pushed = toolchain::command::push_path_option(
                command, toolchain::clang_options::MODULE_OUTPUT, staged_bmi->as_path());
            if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
            if (prepared.unit.context->bmi.source_embedding ==
                cpp::BmiSourceEmbeddingPolicy::EmbedAll) {
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

        auto identity = invocation_identity(command, prepared.working_directory.as_path());
        if (identity.is_err()) return Err(rstd::move(identity).unwrap_err());
        return Ok(CompileInvocation {
            .arguments         = rstd::move(command),
            .working_directory = prepared.working_directory.clone(),
            .identity          = rstd::move(identity).unwrap(),
            .staged_object     = rstd::move(staged_object).unwrap(),
            .final_object      = prepared.unit.object.clone(),
            .staged_bmi        = rstd::move(staged_bmi),
            .final_bmi         = prepared.unit.bmi.is_some() ? Some(prepared.unit.bmi->path.clone())
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

    auto archive(ref<rstd::path::Path> output_path,
                 const Vec<PathBuf>&   objects,
                 ref<rstd::path::Path> working_directory) const
        -> ToolchainResult<rstd::time::Duration> {
        auto parent = create_parent(output_path);
        if (parent.is_err()) return Err(rstd::move(parent).unwrap_err());
        auto archive_exists = rstd::fs::exists(output_path);
        if (archive_exists.is_err()) {
            return Err(ToolchainError::Io(String::make("inspect archive"_str),
                                          PathBuf::from(output_path),
                                          rstd::move(archive_exists).unwrap_err()));
        }
        if (*archive_exists) {
            auto removed = rstd::fs::remove_file(output_path);
            if (removed.is_err()) {
                return Err(ToolchainError::Io(String::make("replace archive"_str),
                                              PathBuf::from(output_path),
                                              rstd::move(removed).unwrap_err()));
            }
        }
        auto command = Vec<String>::make();
        auto pushed  = toolchain::command::push_path(command, archiver_.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(command, toolchain::clang_options::ARCHIVE_CREATE);
        pushed = toolchain::command::push_path(command, output_path);
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        for (const auto& object : objects) {
            pushed = toolchain::command::push_path(command, object.as_path());
            if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        }
        auto output = run_command(command, environment_, Some(working_directory));
        if (output.is_err()) {
            return Err(rstd::into<ToolchainError>(rstd::move(output).unwrap_err()));
        }
        auto command_output = rstd::move(output).unwrap();
        if (command_output.exit_code != i32 {}) {
            return failure<rstd::time::Duration>(
                rstd::format("llvm-ar failed for '{}'\n{}\n{}",
                             output_path,
                             command_text(command).as_str(),
                             command_output.standard_error.as_str()));
        }
        return Ok(command_output.elapsed);
    }

    auto link_executable(ref<rstd::path::Path>           output_path,
                         const Vec<PathBuf>&             objects,
                         const Vec<ResolvedLinkInput>&   inputs,
                         lito::config::StandardLibrary   standard_library,
                         const TargetInfo&               target,
                         bool                            link_stdlib,
                         lito::manifest::CppLto          lto,
                         const cpp::CppLinkRequirements& link_requirements,
                         const Vec<String>&              linker_options,
                         ref<rstd::path::Path>           working_directory) const
        -> ToolchainResult<rstd::time::Duration> {
        auto parent = create_parent(output_path);
        if (parent.is_err()) return Err(rstd::move(parent).unwrap_err());
        auto command = Vec<String>::make();
        auto pushed  = toolchain::command::push_path(command, compiler_.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        if (target.family == TargetFamily::Windows) {
            toolchain::command::push_option(command, "-fuse-ld=lld"_str);
        } else {
            pushed =
                toolchain::command::push_path_option(command, "-fuse-ld="_str, linker_.as_path());
            if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        }
        toolchain::command::push_option(command,
                                        toolchain::clang_options::standard_library_linker_option(
                                            standard_library, link_stdlib));
        toolchain::command::push_option(command, cpp::cpp_lto_option(lto));
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
                rstd::format("clang_rt.builtins-{}.lib", target.architecture.as_str());
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
            return failure<rstd::time::Duration>(
                rstd::format("clang++ failed to link '{}'\n{}\n{}",
                             output_path,
                             command_text(command).as_str(),
                             command_output.standard_error.as_str()));
        }
        return Ok(command_output.elapsed);
    }

    auto strip_artifact(ref<rstd::path::Path>     output_path,
                        ref<rstd::path::Path>     stripper,
                        lito::manifest::StripMode mode,
                        ref<rstd::path::Path>     working_directory) const
        -> ToolchainResult<rstd::time::Duration> {
        if (mode == lito::manifest::StripMode::None) return Ok(rstd::time::Duration {});
        auto staged = staging_path(output_path);
        if (staged.is_err()) return Err(rstd::move(staged).unwrap_err());
        rstd_try(clear_staged_output(staged->as_path()));
        auto command = Vec<String>::make();
        rstd_try(toolchain::command::push_path(command, stripper));
        toolchain::command::push_option(
            command,
            mode == lito::manifest::StripMode::DebugInfo ? "--strip-debug"_str : "--strip-all"_str);
        toolchain::command::push_option(command, "-o"_str);
        rstd_try(toolchain::command::push_path(command, staged->as_path()));
        rstd_try(toolchain::command::push_path(command, output_path));
        auto output = run_command(command, environment_, Some(working_directory));
        if (output.is_err()) {
            return Err(rstd::into<ToolchainError>(rstd::move(output).unwrap_err()));
        }
        auto command_output = rstd::move(output).unwrap();
        if (command_output.exit_code != i32 {}) {
            static_cast<void>(rstd::fs::remove_file(staged->as_path()));
            return failure<rstd::time::Duration>(
                rstd::format("llvm-strip failed for '{}'\n{}\n{}",
                             output_path,
                             command_text(command).as_str(),
                             command_output.standard_error.as_str()));
        }
        rstd_try(publish_output(staged->as_path(), output_path));
        return Ok(command_output.elapsed);
    }

private:
    ClangToolchain(PathBuf                       compiler,
                   PathBuf                       c_compiler,
                   PathBuf                       linker,
                   PathBuf                       archiver,
                   PathBuf                       resource_dir,
                   CompilerIdentity              identity,
                   TargetInfo                    target_info,
                   cpp::BmiFormatIdentity        format,
                   cpp::CppToolchainCapabilities capabilities,
                   cpp::CppArgumentParser        argument_parser,
                   ResolvedProcessEnvironment    environment)
        : compiler_(rstd::move(compiler)),
          c_compiler_(rstd::move(c_compiler)),
          linker_(rstd::move(linker)),
          archiver_(rstd::move(archiver)),
          resource_dir_(rstd::move(resource_dir)),
          compiler_identity_(rstd::move(identity)),
          target_info_(rstd::move(target_info)),
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
        auto queried = toolchain::query_preprocessor_environment(
            command,
            toolchain::PreprocessorEnvironmentKey::make(compile_context.scan_id.as_str(),
                                                        working_directory),
            rstd::move(builtin_environment).unwrap(),
            rstd::move(builtin_values.semantic),
            compile_context.cpp.preprocessor.macros,
            environment_);
        if (queried.is_err()) return Err(rstd::move(queried).unwrap_err());
        auto environment =
            rstd::sync::Arc<toolchain::PreprocessorEnvironment>::make(rstd::move(queried).unwrap());
        preprocessor_environments_.push(environment.clone());
        return Ok(rstd::move(environment));
    }

    auto make_builtin_context(const cpp::CompileContext& context) const
        -> ToolchainResult<ClangBuiltinContext> {
        if (! cpp::is_supported_cpp_standard(context.cpp.language.standard.as_str())) {
            return failure<ClangBuiltinContext>(
                rstd::format("unsupported C++ language standard '{}'; expected C++20 or later",
                             context.cpp.language.standard.as_str()));
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
            "{}{}", toolchain::clang_options::STANDARD, context.cpp.language.standard.as_str()));
        toolchain::command::push_option(
            command, toolchain::clang_options::standard_library(context.cpp.abi.standard_library));
        append_typed_options(command, context.cpp, target_info_.family, true);
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
                    .language_standard = context.cpp.language.standard.clone(),
                    .rtti              = context.cpp.language.rtti,
                    .exceptions        = context.cpp.language.exceptions,
                },
            .key             = rstd::move(key),
            .ignored_options = context.cpp.diagnostics.warnings.len() +
                               context.cpp.diagnostics.options.len() +
                               context.cpp.preprocessor.macros.len(),
        });
    }

    auto append_compile_context(Vec<String>&               command,
                                const cpp::CompileContext& context,
                                bool semantic_only) const -> ToolchainResult<empty> {
        auto pushed = toolchain::command::push_path(command, compiler_.as_path());
        if (pushed.is_err()) return pushed;
        toolchain::command::push_option(command, toolchain::clang_options::RESOURCE_DIR);
        pushed = toolchain::command::push_path(command, resource_dir_.as_path());
        if (pushed.is_err()) return pushed;
        command.push(rstd::format(
            "{}{}", toolchain::clang_options::STANDARD, context.cpp.language.standard.as_str()));
        toolchain::command::push_option(
            command, toolchain::clang_options::standard_library(context.cpp.abi.standard_library));
        toolchain::command::push_option(command,
                                        toolchain::clang_options::bmi(context.bmi.representation));
        append_typed_options(command, context.cpp, target_info_.family, semantic_only);
        for (const auto& macro : context.cpp.preprocessor.macros) {
            command.push(
                rstd::format("{}{}",
                             macro.action == cpp::CppMacroAction::Define ? "-D"_str : "-U"_str,
                             macro.value.as_str()));
        }
        for (const auto& include : context.cpp.preprocessor.include_directories) {
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
    PathBuf                                                       linker_;
    PathBuf                                                       archiver_;
    PathBuf                                                       resource_dir_;
    CompilerIdentity                                              compiler_identity_;
    TargetInfo                                                    target_info_;
    cpp::BmiFormatIdentity                                        bmi_format_;
    cpp::CppToolchainCapabilities                                 capabilities_;
    cpp::CppArgumentParser                                        argument_parser_;
    ResolvedProcessEnvironment                                    environment_;
    mutable ToolchainStatistics                                   toolchain_statistics_;
    mutable Vec<toolchain::SharedClangBuiltinEnvironmentSnapshot> builtin_environment_snapshots_;
    mutable Vec<toolchain::SharedPreprocessorEnvironment>         preprocessor_environments_;
};

} // namespace lito
