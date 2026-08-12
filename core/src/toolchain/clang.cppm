module;
#include <rstd/macro.hpp>

export module lito.toolchain:clang;

import rstd;
import lito.model;
import lito.platform;
import lito.process;
import lito.environment;
import lito.frontend;
import lito.profiling;
import lito.modules;
import lito.build_profile;
import :clang_arguments;
import :clang_options;
import :clang_preprocessor_environment;
import :command;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{
namespace preprocessor = frontend::preprocessor;
}

namespace lito
{

template<typename T>
auto failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Toolchain, rstd::move(message)));
}

template<typename T>
auto failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Toolchain, message));
}

auto preprocessor_probe(preprocessor::PreprocessorActivity activity) noexcept -> ScanProbe {
    switch (activity) {
    case preprocessor::PreprocessorActivity::PredefinedMacros: return ScanProbe::PredefinedMacros;
    case preprocessor::PreprocessorActivity::TranslationUnit: return ScanProbe::TranslationUnit;
    }
    return ScanProbe::Preprocessor;
}

template<typename Profiler>
class PreprocessorProfileObserver {
    struct ActiveActivity {
        preprocessor::PreprocessorActivity activity;
        ScanSpanGuard                      span;
    };

public:
    explicit PreprocessorProfileObserver(Profiler& profiler)
        : profiler_(&profiler), active_(Vec<ActiveActivity>::make()) {}

    auto begin(preprocessor::PreprocessorActivity activity) -> void {
        active_.push(ActiveActivity {
            .activity = activity,
            .span     = profiler_->span(preprocessor_probe(activity)),
        });
    }

    auto end(preprocessor::PreprocessorActivity activity) -> void {
        auto current = active_.pop();
        if (current.is_none()) {
            remember(String::make("preprocessor profiling activity stack is empty"_str));
            return;
        }
        auto value = rstd::move(current).unwrap_unchecked();
        if (value.activity != activity) {
            remember(String::make("preprocessor profiling activities ended out of order"_str));
        }
        auto completed = profiler_->complete(value.span);
        if (completed.is_err()) {
            remember(rstd::move(completed).unwrap_err_unchecked());
        }
    }

    auto record(const preprocessor::PreprocessorStatistics& statistics) -> void {
        statistics_ = statistics;
        profiler_->record_preprocessor_statistics(statistics);
    }

    auto finish() -> rstd::Result<empty, String> {
        if (! active_.is_empty()) {
            return Err(String::make("preprocessor profiling activities remain active"_str));
        }
        if (error_.is_some()) return Err(rstd::move(error_).unwrap_unchecked());
        return Ok(empty {});
    }

    auto statistics() const noexcept -> const preprocessor::PreprocessorStatistics& {
        return statistics_;
    }

private:
    auto remember(String error) -> void {
        if (error_.is_none()) error_ = Some(rstd::move(error));
    }

    Profiler*                            profiler_ {};
    Vec<ActiveActivity>                  active_;
    preprocessor::PreprocessorStatistics statistics_;
    Option<String>                       error_;
};

auto create_parent(ref<rstd::path::Path> path) -> Result<empty> {
    auto parent = path.parent();
    if (parent.is_none()) {
        return failure<empty>(rstd::format("output path '{}' has no parent", path));
    }
    auto created = rstd::fs::create_dir_all(*parent);
    if (created.is_err()) {
        return failure<empty>(rstd::format(
            "cannot create directory '{}': {}", *parent, rstd::move(created).unwrap_err()));
    }
    return Ok(empty {});
}

auto staging_path(ref<rstd::path::Path> output) -> Result<PathBuf> {
    auto text = output.to_str();
    if (text.is_none()) {
        return failure<PathBuf>(rstd::format("output path '{}' is not valid UTF-8", output));
    }
    auto value = String::make(*text);
    value.push_str(".lito-building"_str);
    return Ok(PathBuf::from(rstd::move(value)));
}

auto clear_staged_output(ref<rstd::path::Path> path) -> Result<empty> {
    auto exists = rstd::fs::exists(path);
    if (exists.is_err()) {
        return failure<empty>(
            rstd::format("cannot inspect staged output '{}': {}", path, exists.unwrap_err()));
    }
    if (! *exists) return Ok(empty {});
    auto removed = rstd::fs::remove_file(path);
    if (removed.is_err()) {
        return failure<empty>(
            rstd::format("cannot remove stale staged output '{}': {}", path, removed.unwrap_err()));
    }
    return Ok(empty {});
}

auto publish_output(ref<rstd::path::Path> staged, ref<rstd::path::Path> final) -> Result<empty> {
    auto exists = rstd::fs::exists(staged);
    if (exists.is_err()) {
        return failure<empty>(
            rstd::format("cannot inspect staged output '{}': {}", staged, exists.unwrap_err()));
    }
    if (! *exists) {
        return failure<empty>(rstd::format("compiler did not produce staged output '{}'", staged));
    }
    auto published = rstd::fs::rename(staged, final);
    if (published.is_err()) {
        return failure<empty>(rstd::format("cannot publish compiler output '{}' as '{}': {}",
                                           staged,
                                           final,
                                           published.unwrap_err()));
    }
    return Ok(empty {});
}

auto verify_staged_output(ref<rstd::path::Path> path) -> Result<empty> {
    auto exists = rstd::fs::exists(path);
    if (exists.is_err()) {
        return failure<empty>(
            rstd::format("cannot inspect staged output '{}': {}", path, exists.unwrap_err()));
    }
    if (! *exists) {
        return failure<empty>(rstd::format("compiler did not produce staged output '{}'", path));
    }
    return Ok(empty {});
}

auto invocation_identity(const Vec<String>& arguments, ref<rstd::path::Path> working_directory)
    -> Result<String> {
    auto working = working_directory.to_str();
    if (working.is_none()) {
        return failure<String>(
            rstd::format("compile working directory '{}' is not valid UTF-8", working_directory));
    }
    auto identity = String::make("lito-clang-compile-invocation-v1\n"_str);
    identity.push_str(rstd::format("{}:{}\n", working->size(), *working).as_str());
    for (const auto& argument : arguments) {
        identity.push_str(rstd::format("{}:{}\n", argument.size(), argument.as_str()).as_str());
    }
    return Ok(rstd::move(identity));
}

auto argument_identity(ref<str> recipe, const Vec<String>& arguments) -> String {
    auto identity = String::make(recipe);
    identity.push_ascii('\n');
    for (const auto& argument : arguments) {
        identity.push_str(rstd::format("{}:{}\n", argument.size(), argument.as_str()).as_str());
    }
    return identity;
}

auto clang_warning_option(CppWarningOption option) noexcept -> ref<str> {
    switch (option.warning) {
    case CppWarning::All: return option.enabled ? "-Wall"_str : "-Wno-all"_str;
    case CppWarning::Pedantic: return option.enabled ? "-Wpedantic"_str : "-Wno-pedantic"_str;
    case CppWarning::GnuStatementExpression:
        return option.enabled ? "-Wgnu-statement-expression"_str
                              : "-Wno-gnu-statement-expression"_str;
    case CppWarning::DeprecatedDeclarations:
        return option.enabled ? "-Wdeprecated-declarations"_str
                              : "-Wno-deprecated-declarations"_str;
    case CppWarning::UnknownAttributes:
        return option.enabled ? "-Wunknown-attributes"_str : "-Wno-unknown-attributes"_str;
    }
    return {};
}

auto append_typed_options(Vec<String>&             command,
                          const CppCompileOptions& options,
                          bool                     semantic_only) -> void {
    if (options.target.target.is_some()) {
        command.push(rstd::format("--target={}", options.target.target->as_str()));
    }
    if (options.target.sysroot.is_some()) {
        command.push(rstd::format("--sysroot={}", options.target.sysroot->as_str()));
    }
    auto optimization = cpp_optimization_option(options.codegen.optimization);
    if (! optimization.is_empty()) toolchain::command::push_option(command, optimization);
    auto debug = cpp_debug_option(options.codegen.debug_info);
    if (! semantic_only && ! debug.is_empty()) toolchain::command::push_option(command, debug);
    auto lto = cpp_lto_option(options.codegen.lto);
    if (! semantic_only) toolchain::command::push_option(command, lto);
    toolchain::command::push_option(
        command, options.codegen.position_independent_code ? "-fPIC"_str : "-fno-PIC"_str);
    switch (options.language.sized_deallocation) {
    case CppSizedDeallocation::Auto: break;
    case CppSizedDeallocation::Enabled:
        toolchain::command::push_option(command, "-fsized-deallocation"_str);
        break;
    case CppSizedDeallocation::Disabled:
        toolchain::command::push_option(command, "-fno-sized-deallocation"_str);
        break;
    }
    for (const auto& option : options.language.modes) command.push(option.value.clone());
    for (const auto& option : options.abi.modes) command.push(option.value.clone());
    for (const auto& option : options.target.features) command.push(option.value.clone());
    for (const auto& option : options.codegen.modes) command.push(option.value.clone());
    for (const auto& option : options.codegen.instrumentation) command.push(option.clone());
    toolchain::command::push_option(command,
                                    options.language.rtti ? toolchain::clang_options::RTTI
                                                          : toolchain::clang_options::NO_RTTI);
    toolchain::command::push_option(command,
                                    options.language.exceptions
                                        ? toolchain::clang_options::EXCEPTIONS
                                        : toolchain::clang_options::NO_EXCEPTIONS);
    for (const auto& option : options.vendor) {
        if (semantic_only && (option.effect == CppVendorOptionEffect::Codegen ||
                              option.effect == CppVendorOptionEffect::Diagnostic)) {
            continue;
        }
        if (option.preserve_raw_tokens) {
            for (const auto& token : option.raw_tokens) command.push(token.clone());
        } else {
            command.push(option.value.clone());
        }
    }
    if (! semantic_only) {
        for (const auto& warning : options.diagnostics.warnings) {
            toolchain::command::push_option(command, clang_warning_option(warning));
        }
        for (const auto& option : options.diagnostics.options) command.push(option.clone());
    }
}

} // namespace lito

export namespace lito
{

struct ClangBuiltinContext {
    Vec<String>                       query_command;
    toolchain::BuiltinSemanticContext semantic;
    String                            key;
    usize                             ignored_options {};
};

class ClangCompileExecutor {
public:
    explicit ClangCompileExecutor(const ResolvedProcessEnvironment& environment)
        : environment_(rstd::addressof(environment)) {}

    auto execute(const CompileInvocation& invocation) const -> Result<CompileCommandResult> {
        auto cleared = clear_staged_output(invocation.staged_object.as_path());
        if (cleared.is_err()) return Err(rstd::move(cleared).unwrap_err());
        if (invocation.staged_bmi.is_some()) {
            cleared = clear_staged_output(invocation.staged_bmi->as_path());
            if (cleared.is_err()) return Err(rstd::move(cleared).unwrap_err());
        }
        auto output = run_command(
            invocation.arguments, *environment_, Some(invocation.working_directory.as_path()));
        if (output.is_err()) return Err(rstd::move(output).unwrap_err());
        auto command_output = rstd::move(output).unwrap();
        if (command_output.exit_code == i32 {}) {
            auto verified = verify_staged_output(invocation.staged_object.as_path());
            if (verified.is_err()) return Err(rstd::move(verified).unwrap_err());
            if (invocation.staged_bmi.is_some()) {
                if (invocation.final_bmi.is_none()) {
                    return failure<CompileCommandResult>(
                        "compile invocation has a staged BMI without a final output"_str);
                }
                verified = verify_staged_output(invocation.staged_bmi->as_path());
                if (verified.is_err()) return Err(rstd::move(verified).unwrap_err());
            }
            if (invocation.staged_bmi.is_some() && invocation.final_bmi.is_some()) {
                auto published = publish_output(invocation.staged_bmi->as_path(),
                                                invocation.final_bmi->as_path());
                if (published.is_err()) return Err(rstd::move(published).unwrap_err());
            }
            auto published = publish_output(invocation.staged_object.as_path(),
                                            invocation.final_object.as_path());
            if (published.is_err()) return Err(rstd::move(published).unwrap_err());
        }
        return Ok(CompileCommandResult {
            .exit_code       = command_output.exit_code,
            .standard_output = rstd::move(command_output.standard_output),
            .standard_error  = rstd::move(command_output.standard_error),
            .elapsed         = command_output.elapsed,
        });
    }

private:
    const ResolvedProcessEnvironment* environment_ {};
};

class ClangToolchain {
public:
    static auto create(const ToolchainSpec& specification) -> Result<ClangToolchain> {
        auto environment = ResolvedProcessEnvironment::resolve(ProcessEnvironmentSpec {});
        if (environment.is_err()) return Err(rstd::move(environment).unwrap_err());
        auto resolver = ToolResolver(*environment);
        return create(specification, resolver, *environment);
    }

    static auto create(const ToolchainSpec&              specification,
                       ToolResolver&                     resolver,
                       const ResolvedProcessEnvironment& environment) -> Result<ClangToolchain> {
        auto argument_parser = make_clang_cpp_argument_parser();
        if (argument_parser.is_err()) {
            return failure<ClangToolchain>(rstd::move(argument_parser).unwrap_err());
        }
        auto configured_compiler =
            resolver.resolve(specification.compiler.as_path(), "clang++"_str);
        auto configured_c_compiler =
            resolver.resolve(specification.c_compiler.as_path(), "clang"_str);
        auto configured_linker = resolver.resolve(specification.linker.as_path(), "LLD linker"_str);
        auto configured_archiver =
            resolver.resolve(specification.archiver.as_path(), "llvm-ar"_str);
        if (configured_compiler.is_err()) {
            return Err(rstd::move(configured_compiler).unwrap_err());
        }
        if (configured_c_compiler.is_err()) {
            return Err(rstd::move(configured_c_compiler).unwrap_err());
        }
        if (configured_linker.is_err()) {
            return Err(rstd::move(configured_linker).unwrap_err());
        }
        if (configured_archiver.is_err()) {
            return Err(rstd::move(configured_archiver).unwrap_err());
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
        if (target_info.is_err()) return Err(rstd::move(target_info).unwrap_err());

        auto resource_path      = PathBuf::from(resource->as_str());
        auto canonical_resource = toolchain::command::resolve_path(resource_path.as_path(),
                                                                   "Clang resource directory"_str);
        if (canonical_resource.is_err()) {
            return Err(rstd::move(canonical_resource).unwrap_err());
        }

        auto resolved_resource = rstd::move(canonical_resource).unwrap();
        auto compiler_metadata = rstd::fs::metadata(compiler_path.as_path());
        if (compiler_metadata.is_err()) {
            return failure<ClangToolchain>(
                rstd::format("cannot inspect compiler '{}': {}",
                             compiler_path.as_path(),
                             rstd::move(compiler_metadata).unwrap_err()));
        }
        auto modified = compiler_metadata->modified();
        if (modified.is_err()) {
            return failure<ClangToolchain>(
                rstd::format("cannot read compiler modification time '{}': {}",
                             compiler_path.as_path(),
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
        auto capabilities = CppToolchainCapabilities {
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
        auto format = BmiFormatIdentity {
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
    auto compiler_path() const -> ref<rstd::path::Path> { return compiler_.as_path(); }
    auto c_compiler_path() const -> ref<rstd::path::Path> { return c_compiler_.as_path(); }
    auto linker_path() const -> ref<rstd::path::Path> { return linker_.as_path(); }
    auto archiver_path() const -> ref<rstd::path::Path> { return archiver_.as_path(); }
    auto target() const -> ref<str> { return compiler_identity_.target.as_str(); }
    auto target_info() const -> const TargetInfo& { return target_info_; }
    auto resource_dir() const -> ref<rstd::path::Path> { return resource_dir_.as_path(); }
    auto bmi_format() const -> const BmiFormatIdentity& { return bmi_format_; }
    auto capabilities() const noexcept -> const CppToolchainCapabilities& { return capabilities_; }
    auto argument_parser() const noexcept -> const CppArgumentParser& { return argument_parser_; }

    auto validate(const CppCompileOptions& cpp, const BmiRequest& bmi) const -> Result<empty> {
        if (! is_supported_cpp_standard(cpp.language.standard.as_str())) {
            return failure<empty>(
                rstd::format("unsupported C++ language standard '{}'; expected C++20 or later",
                             cpp.language.standard.as_str()));
        }
        if (bmi.representation == BmiRepresentation::Reduced && ! capabilities_.reduced_bmi) {
            return failure<empty>("configured Clang does not support reduced BMI"_str);
        }
        if (bmi.source_embedding == BmiSourceEmbeddingPolicy::EmbedAll &&
            ! capabilities_.source_embedding) {
            return failure<empty>(
                "configured Clang does not support embedding BMI source inputs"_str);
        }
        return Ok(empty {});
    }

    auto builtin_context(const CompileContext& context) const -> Result<ClangBuiltinContext> {
        return make_builtin_context(context);
    }

    auto statistics() const -> ToolchainStatistics {
        auto result                             = toolchain_statistics_;
        result.target_queries                   = usize(1);
        result.preprocessor_environment_entries = preprocessor_environments_.len();
        result.builtin_snapshots                = builtin_environment_snapshots_.len();
        return result;
    }

    auto prepare(UnitSpec unit, ref<rstd::path::Path> working_directory) const
        -> Result<PreparedUnit> {
        auto object_parent = create_parent(unit.object.as_path());
        if (object_parent.is_err()) return Err(rstd::move(object_parent).unwrap_err());
        return Ok(PreparedUnit {
            .unit              = rstd::move(unit),
            .working_directory = PathBuf::from(working_directory),
        });
    }

    auto scan(const PreparedUnit& prepared) const -> Result<ScanResult> {
        if (prepared.frontend_analysis.is_none()) {
            return failure<ScanResult>(rstd::format("source '{}' has no frontend analysis",
                                                    prepared.unit.source.as_path()));
        }
        return Ok(
            modules::scan_from_frontend(prepared.frontend_analysis->result, prepared.unit.id));
    }

    auto preprocessor_environment_identity(const CompileContext& compile_context,
                                           ref<rstd::path::Path> working_directory) const
        -> Result<String> {
        auto environment = environment_for(compile_context, working_directory);
        if (environment.is_err()) return Err(rstd::move(environment).unwrap_err());
        return Ok((*environment)->identity.clone());
    }

    auto prepare_scan_environment(const CompileContext& compile_context,
                                  ref<rstd::path::Path> working_directory) const
        -> Result<toolchain::SharedPreprocessorEnvironment> {
        for (const auto& option : compile_context.cpp.vendor) {
            if (option.native_preprocessor_unsupported) {
                return failure<toolchain::SharedPreprocessorEnvironment>(
                    rstd::format("compiler option '{}' is not supported by the native preprocessor",
                                 option.value.as_str()));
            }
        }
        return environment_for(compile_context, working_directory);
    }

    auto preprocess(ref<rstd::path::Path>      source,
                    const CompileContext&      compile_context,
                    ref<rstd::path::Path>      working_directory,
                    frontend::FrontendService& frontend_service,
                    ScanProfiler& profiler) const -> Result<frontend::UncachedFrontendAnalysis> {
        auto environment_span     = profiler.span(ScanProbe::Environment);
        auto selected_environment = prepare_scan_environment(compile_context, working_directory);
        if (selected_environment.is_err()) {
            return Err(rstd::move(selected_environment).unwrap_err());
        }
        auto environment          = rstd::move(selected_environment).unwrap();
        auto environment_finished = profiler.complete(environment_span);
        if (environment_finished.is_err()) {
            return failure<frontend::UncachedFrontendAnalysis>(
                rstd::move(environment_finished).unwrap_err_unchecked());
        }
        return preprocess_with_environment(source, environment, frontend_service, profiler);
    }

    template<typename Profiler>
    auto preprocess_with_environment(ref<rstd::path::Path>                           source,
                                     const toolchain::SharedPreprocessorEnvironment& environment,
                                     frontend::FrontendService& frontend_service,
                                     Profiler&                  profiler) const
        -> Result<frontend::UncachedFrontendAnalysis> {
        auto frontend_span = profiler.span(ScanProbe::Frontend);
        frontend_service.record_analysis_build();
        auto includes = toolchain::ClangIncludeResolver(*environment);
        auto builtins = toolchain::ClangBuiltinProvider(
            *environment, environment->key.working_directory.as_path());
        auto identifiers       = CppIdentifierTokenMatcher {};
        auto pragmas           = toolchain::ClangPragmaHandler {};
        auto events            = toolchain::DependencyEvents {};
        auto consumer          = frontend::parser::ModuleDependencyConsumer::make();
        auto observer          = PreprocessorProfileObserver(profiler);
        auto translation       = profiler.measure(ScanProbe::Preprocessor, [&] {
            return preprocessor::preprocess_to(
                preprocessor::PreprocessRequest {
                    .source               = PathBuf::from(source),
                    .environment_identity = environment->identity.clone(),
                },
                frontend_service,
                includes,
                builtins,
                identifiers,
                pragmas,
                events,
                consumer,
                observer);
        });
        auto observer_finished = observer.finish();
        if (observer_finished.is_err()) {
            return failure<frontend::UncachedFrontendAnalysis>(
                rstd::move(observer_finished).unwrap_err_unchecked());
        }
        frontend_service.record_preprocessor_statistics(observer.statistics());
        if (translation.is_err()) {
            auto error = rstd::move(translation).unwrap_err();
            if (error.path.is_some() && error.location.is_some()) {
                return failure<frontend::UncachedFrontendAnalysis>(
                    rstd::format("native preprocessing failed at {}:{}:{}: {}",
                                 error.path->as_path(),
                                 error.location->line,
                                 error.location->column,
                                 error.message.as_str()));
            }
            return failure<frontend::UncachedFrontendAnalysis>(rstd::format(
                "native preprocessing failed for '{}': {}", source, error.message.as_str()));
        }
        translation->header_inputs = events.take_headers();
        auto parsed                = consumer.finish(*translation);
        if (parsed.is_err()) {
            return failure<frontend::UncachedFrontendAnalysis>(
                rstd::move(parsed).unwrap_err().message.clone());
        }
        return Ok(frontend::UncachedFrontendAnalysis {
            .result          = rstd::move(parsed).unwrap(),
            .include_lookups = includes.take_dependencies(),
        });
    }

    auto prepare_compile(const PreparedUnit&                  prepared,
                         const ScanResult&                    scan_result,
                         const Vec<ModuleArtifactDependency>& module_dependencies) const
        -> Result<CompileInvocation> {
        auto valid = validate(prepared.unit.context->cpp, prepared.unit.context->bmi);
        if (valid.is_err()) return Err(rstd::move(valid).unwrap_err());
        auto staged_object = staging_path(prepared.unit.object.as_path());
        if (staged_object.is_err()) return Err(rstd::move(staged_object).unwrap_err());
        auto staged_bmi = Option<PathBuf> {};
        auto command    = Vec<String>::make();
        auto context    = append_compile_context(command, *prepared.unit.context, false);
        if (context.is_err()) return Err(rstd::move(context).unwrap_err());
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
            if (prepared.unit.context->bmi.source_embedding == BmiSourceEmbeddingPolicy::EmbedAll) {
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
        -> Result<rstd::time::Duration> {
        auto output = execute_compile_capture(invocation);
        if (output.is_err()) return Err(rstd::move(output).unwrap_err());
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
        -> Result<CompileCommandResult> {
        return compile_executor().execute(invocation);
    }

    auto archive(ref<rstd::path::Path> output_path,
                 const Vec<PathBuf>&   objects,
                 ref<rstd::path::Path> working_directory) const -> Result<rstd::time::Duration> {
        auto parent = create_parent(output_path);
        if (parent.is_err()) return Err(rstd::move(parent).unwrap_err());
        auto archive_exists = rstd::fs::exists(output_path);
        if (archive_exists.is_err()) {
            return failure<rstd::time::Duration>(
                rstd::format("cannot inspect archive '{}': {}",
                             output_path,
                             rstd::move(archive_exists).unwrap_err()));
        }
        if (*archive_exists) {
            auto removed = rstd::fs::remove_file(output_path);
            if (removed.is_err()) {
                return failure<rstd::time::Duration>(
                    rstd::format("cannot replace archive '{}': {}",
                                 output_path,
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
        if (output.is_err()) return Err(rstd::move(output).unwrap_err());
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

    auto link_executable(ref<rstd::path::Path>         output_path,
                         const Vec<PathBuf>&           objects,
                         const Vec<ResolvedLinkInput>& inputs,
                         StandardLibrary               standard_library,
                         bool                          link_stdlib,
                         CppLto                        lto,
                         const Vec<String>&            linker_options,
                         ref<rstd::path::Path>         working_directory) const
        -> Result<rstd::time::Duration> {
        auto parent = create_parent(output_path);
        if (parent.is_err()) return Err(rstd::move(parent).unwrap_err());
        auto command = Vec<String>::make();
        auto pushed  = toolchain::command::push_path(command, compiler_.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        pushed = toolchain::command::push_path_option(command, "-fuse-ld="_str, linker_.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(command,
                                        toolchain::clang_options::standard_library_linker_option(
                                            standard_library, link_stdlib));
        toolchain::command::push_option(command, cpp_lto_option(lto));
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
            if (target_info_.os.as_str() == "macos"_str) {
                toolchain::command::push_option(command, toolchain::clang_options::LINKER_ARGUMENT);
                toolchain::command::push_option(command, toolchain::clang_options::FORCE_LOAD);
                toolchain::command::push_option(command, toolchain::clang_options::LINKER_ARGUMENT);
                pushed = toolchain::command::push_path(command, archive.path.as_path());
                if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
                continue;
            }
            if (target_info_.family == TargetFamily::Windows) {
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
            if (target_info_.family != TargetFamily::Unix) {
                return failure<rstd::time::Duration>(
                    rstd::format("whole-archive linking is unsupported for target '{}'",
                                 target_info_.triple.as_str()));
            }
            toolchain::command::push_option(command, toolchain::clang_options::WHOLE_ARCHIVE);
            pushed = toolchain::command::push_path(command, archive.path.as_path());
            if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
            toolchain::command::push_option(command, toolchain::clang_options::NO_WHOLE_ARCHIVE);
        }
        toolchain::command::push_option(command, toolchain::clang_options::OUTPUT);
        pushed = toolchain::command::push_path(command, output_path);
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());

        auto output = run_command(command, environment_, Some(working_directory));
        if (output.is_err()) return Err(rstd::move(output).unwrap_err());
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

    auto strip_artifact(ref<rstd::path::Path> output_path,
                        ref<rstd::path::Path> stripper,
                        StripMode             mode,
                        ref<rstd::path::Path> working_directory) const
        -> Result<rstd::time::Duration> {
        if (mode == StripMode::None) return Ok(rstd::time::Duration {});
        auto staged = staging_path(output_path);
        if (staged.is_err()) return Err(rstd::move(staged).unwrap_err());
        rstd_try(clear_staged_output(staged->as_path()));
        auto command = Vec<String>::make();
        rstd_try(toolchain::command::push_path(command, stripper));
        toolchain::command::push_option(
            command, mode == StripMode::DebugInfo ? "--strip-debug"_str : "--strip-all"_str);
        toolchain::command::push_option(command, "-o"_str);
        rstd_try(toolchain::command::push_path(command, staged->as_path()));
        rstd_try(toolchain::command::push_path(command, output_path));
        auto output = run_command(command, environment_, Some(working_directory));
        if (output.is_err()) return Err(rstd::move(output).unwrap_err());
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
    ClangToolchain(PathBuf                    compiler,
                   PathBuf                    c_compiler,
                   PathBuf                    linker,
                   PathBuf                    archiver,
                   PathBuf                    resource_dir,
                   CompilerIdentity           identity,
                   TargetInfo                 target_info,
                   BmiFormatIdentity          format,
                   CppToolchainCapabilities   capabilities,
                   CppArgumentParser          argument_parser,
                   ResolvedProcessEnvironment environment)
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

    auto environment_for(const CompileContext& compile_context,
                         ref<rstd::path::Path> working_directory) const
        -> Result<toolchain::SharedPreprocessorEnvironment> {
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

    auto make_builtin_context(const CompileContext& context) const -> Result<ClangBuiltinContext> {
        if (! is_supported_cpp_standard(context.cpp.language.standard.as_str())) {
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
        append_typed_options(command, context.cpp, true);
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

    auto append_compile_context(Vec<String>&          command,
                                const CompileContext& context,
                                bool                  semantic_only) const -> Result<empty> {
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
        append_typed_options(command, context.cpp, semantic_only);
        for (const auto& macro : context.cpp.preprocessor.macros) {
            command.push(rstd::format("{}{}",
                                      macro.action == CppMacroAction::Define ? "-D"_str : "-U"_str,
                                      macro.value.as_str()));
        }
        for (const auto& include : context.cpp.preprocessor.include_directories) {
            toolchain::command::push_option(command,
                                            include.kind == CppIncludeDirectoryKind::System
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
    BmiFormatIdentity                                             bmi_format_;
    CppToolchainCapabilities                                      capabilities_;
    CppArgumentParser                                             argument_parser_;
    ResolvedProcessEnvironment                                    environment_;
    mutable ToolchainStatistics                                   toolchain_statistics_;
    mutable Vec<toolchain::SharedClangBuiltinEnvironmentSnapshot> builtin_environment_snapshots_;
    mutable Vec<toolchain::SharedPreprocessorEnvironment>         preprocessor_environments_;
};

} // namespace lito
