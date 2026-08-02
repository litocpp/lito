export module tenon.toolchain:clang;

import rstd;
import tenon.model;
import tenon.process;
import tenon.frontend;
import tenon.modules;
import tenon.build_profile;
import :clang_options;
import :clang_preprocessor_environment;
import :command;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace tenon {
namespace preprocessor = frontend::preprocessor;
}

namespace tenon
{

template<typename T>
auto failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Toolchain, rstd::move(message)));
}

template<typename T>
auto failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Toolchain, message));
}

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

auto invocation_identity(const Vec<String>& arguments, ref<rstd::path::Path> working_directory)
    -> Result<String> {
    auto working = working_directory.to_str();
    if (working.is_none()) {
        return failure<String>(
            rstd::format("compile working directory '{}' is not valid UTF-8", working_directory));
    }
    auto identity = String::make("tenon-clang-compile-invocation-v1\n"_str);
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

auto unsupported_native_preprocessor_option(ref<str> option) -> bool {
    return option == "-include"_str || option.starts_with("-include="_str) ||
           option == "-imacros"_str || option.starts_with("-imacros="_str) ||
           option == "-include-pch"_str || option.starts_with("-include-pch="_str) ||
           option == "-fmodule-map-file"_str ||
           option.starts_with("-fmodule-map-file="_str) ||
           option.starts_with("-fmodule-file="_str) ||
           option.starts_with("-fmodules-cache-path="_str);
}

auto optimization_option(ref<str> option) -> bool {
    return option == "-O"_str || option == "-O0"_str || option == "-O1"_str ||
           option == "-O2"_str || option == "-O3"_str || option == "-O4"_str ||
           option == "-Og"_str || option == "-Os"_str || option == "-Oz"_str ||
           option == "-Ofast"_str;
}

struct ClangBuiltinOptionSet {
    Option<String> target;
    Option<String> sysroot;
    Option<String> optimization;
    rstd::collections::BTreeMap<String, String> last_options;
    Vec<String> ordered_options;
    usize ignored_options {};
};

auto attached_option(ref<str> option, ref<str> prefix) -> Option<ref<str>> {
    if (! option.starts_with(prefix) || option.len() <= prefix.len()) return None();
    return option.get(prefix.len(), option.len());
}

auto toggle_family(ref<str> option) -> Option<ref<str>> {
    if (option == "-fPIC"_str || option == "-fpic"_str || option == "-fPIE"_str ||
        option == "-fpie"_str || option == "-fno-PIC"_str || option == "-fno-pic"_str ||
        option == "-fno-PIE"_str || option == "-fno-pie"_str) {
        return Some("pic"_str);
    }
    if (option == "-fblocks"_str || option == "-fno-blocks"_str) {
        return Some("blocks"_str);
    }
    if (option == "-fcoroutines"_str || option == "-fno-coroutines"_str) {
        return Some("coroutines"_str);
    }
    if (option == "-fsized-deallocation"_str || option == "-fno-sized-deallocation"_str) {
        return Some("sized-deallocation"_str);
    }
    if (option == "-ffreestanding"_str || option == "-fhosted"_str) {
        return Some("hosted"_str);
    }
    if (option == "-fsigned-char"_str || option == "-funsigned-char"_str) {
        return Some("char-signedness"_str);
    }
    if (option == "-fshort-enums"_str || option == "-fno-short-enums"_str) {
        return Some("short-enums"_str);
    }
    if (option == "-fshort-wchar"_str || option == "-fno-short-wchar"_str) {
        return Some("short-wchar"_str);
    }
    if (option == "-fchar8_t"_str || option == "-fno-char8_t"_str) {
        return Some("char8-t"_str);
    }
    return None();
}

auto target_option_family(ref<str> option) -> String {
    auto begin = option.starts_with("-mno-"_str) ? usize(5) : usize(2);
    auto end = begin;
    while (end < option.len() && option.as_bytes()[end] != u8('=')) ++end;
    auto name = option.get(begin, end);
    return name.is_some() ? String::make(*name) : String::make(option);
}

auto normalize_builtin_options(const Vec<String>& options) -> Result<ClangBuiltinOptionSet> {
    auto result = ClangBuiltinOptionSet {};
    for (auto index = usize {}; index < options.len(); ++index) {
        auto option = options[index].as_str();
        auto take_value = [&](ref<str> canonical) -> Result<String> {
            if (index + usize(1) >= options.len()) {
                return failure<String>(
                    rstd::format("compiler option '{}' requires a value", option));
            }
            ++index;
            return Ok(rstd::format("{}{}", canonical, options[index].as_str()));
        };
        if (option == "-D"_str || option == "-U"_str || option == "-mllvm"_str) {
            if (index + usize(1) >= options.len()) {
                return failure<ClangBuiltinOptionSet>(
                    rstd::format("compiler option '{}' requires a value", option));
            }
            ++index;
            result.ignored_options += usize(2);
            continue;
        }
        if (option.starts_with("-D"_str) || option.starts_with("-U"_str)) {
            ++result.ignored_options;
            continue;
        }
        if (option.starts_with("-mllvm="_str)) {
            ++result.ignored_options;
            continue;
        }
        if (option == "-target"_str || option == "--target"_str) {
            auto value = take_value("--target="_str);
            if (value.is_err()) return Err(rstd::move(value).unwrap_err());
            result.target = Some(rstd::move(value).unwrap());
            continue;
        }
        auto target = attached_option(option, "--target="_str);
        if (target.is_none()) target = attached_option(option, "-target="_str);
        if (target.is_some()) {
            result.target = Some(rstd::format("--target={}", *target));
            continue;
        }
        if (option == "--sysroot"_str || option == "-isysroot"_str) {
            auto value = take_value("--sysroot="_str);
            if (value.is_err()) return Err(rstd::move(value).unwrap_err());
            result.sysroot = Some(rstd::move(value).unwrap());
            continue;
        }
        auto sysroot = attached_option(option, "--sysroot="_str);
        if (sysroot.is_none()) sysroot = attached_option(option, "-isysroot="_str);
        if (sysroot.is_some()) {
            result.sysroot = Some(rstd::format("--sysroot={}", *sysroot));
            continue;
        }
        if (option == "-march"_str || option == "-mcpu"_str ||
            option == "-mtune"_str || option == "-mabi"_str) {
            auto canonical = rstd::format("{}=", option);
            auto value = take_value(canonical.as_str());
            if (value.is_err()) return Err(rstd::move(value).unwrap_err());
            auto normalized = rstd::move(value).unwrap();
            result.last_options.insert(target_option_family(normalized.as_str()),
                                       rstd::move(normalized));
            continue;
        }
        if (optimization_option(option)) {
            result.optimization = Some(options[index].clone());
            continue;
        }
        auto toggle = toggle_family(option);
        if (toggle.is_some()) {
            result.last_options.insert(String::make(*toggle), options[index].clone());
            continue;
        }
        if (option.starts_with("-m"_str)) {
            result.last_options.insert(target_option_family(option), options[index].clone());
            continue;
        }
        if (option.starts_with("-fsanitize="_str) ||
            option.starts_with("-fno-sanitize="_str) ||
            option.starts_with("-fsanitize-trap="_str) ||
            option.starts_with("-fno-sanitize-trap="_str) ||
            option.starts_with("-fms-"_str) || option.starts_with("-fno-ms-"_str) ||
            option.starts_with("-fptrauth"_str) ||
            option.starts_with("-fno-ptrauth"_str) ||
            option.starts_with("-fopenmp"_str) ||
            option.starts_with("-fno-openmp"_str) || option == "-pedantic-errors"_str ||
            option == "-Werror"_str || option.starts_with("-Werror="_str) ||
            option == "-Wno-error"_str || option.starts_with("-Wno-error="_str)) {
            result.ordered_options.push(options[index].clone());
            continue;
        }
        ++result.ignored_options;
    }
    return Ok(rstd::move(result));
}

} // namespace tenon

export namespace tenon
{

struct ClangBuiltinContext {
    Vec<String>                       query_command;
    toolchain::BuiltinSemanticContext semantic;
    String                            key;
    usize                             ignored_options {};
};

class ClangToolchain {
public:
    static auto create(const ToolchainSpec& specification) -> Result<ClangToolchain> {
        auto configured_compiler =
            toolchain::command::resolve_tool(specification.compiler.as_path(), "clang++"_str);
        auto archiver =
            toolchain::command::resolve_tool(specification.archiver.as_path(), "llvm-ar"_str);
        if (configured_compiler.is_err()) {
            return Err(rstd::move(configured_compiler).unwrap_err());
        }
        if (archiver.is_err()) return Err(rstd::move(archiver).unwrap_err());
        auto compiler_path = rstd::move(configured_compiler).unwrap();
        auto archiver_path = rstd::move(archiver).unwrap();

        if (toolchain::command::is_searchable_tool_name(specification.compiler.as_path())) {
            auto path_command = Vec<String>::make();
            auto pushed = toolchain::command::push_path(path_command, compiler_path.as_path());
            if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
            toolchain::command::push_option(path_command,
                                            toolchain::clang_options::PRINT_COMPILER_PATH);
            auto queried = toolchain::command::tool_output(rstd::move(path_command),
                                                           "clang++ executable query"_str);
            if (queried.is_err()) return Err(rstd::move(queried).unwrap_err());
            auto queried_path = PathBuf::from(queried->as_str());
            auto resolved = toolchain::command::resolve_tool(queried_path.as_path(), "clang++"_str);
            if (resolved.is_err()) return Err(rstd::move(resolved).unwrap_err());
            compiler_path = rstd::move(resolved).unwrap();
        }

        auto compiler_command = Vec<String>::make();
        auto target_command   = Vec<String>::make();
        auto resource_command = Vec<String>::make();
        auto pushed = toolchain::command::push_path(compiler_command, compiler_path.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(compiler_command, toolchain::clang_options::VERSION);
        pushed = toolchain::command::push_path(target_command, compiler_path.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(target_command,
                                        toolchain::clang_options::PRINT_TARGET_TRIPLE);
        pushed = toolchain::command::push_path(resource_command, compiler_path.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(resource_command,
                                        toolchain::clang_options::PRINT_RESOURCE_DIR);

        auto compiler_version =
            toolchain::command::tool_output(rstd::move(compiler_command), "clang++ --version"_str);
        auto target =
            toolchain::command::tool_output(rstd::move(target_command), "clang++ target query"_str);
        auto resource = toolchain::command::tool_output(rstd::move(resource_command),
                                                        "clang++ resource query"_str);
        if (compiler_version.is_err()) return Err(rstd::move(compiler_version).unwrap_err());
        if (target.is_err()) return Err(rstd::move(target).unwrap_err());
        if (resource.is_err()) return Err(rstd::move(resource).unwrap_err());
        if (! compiler_version->as_str().contains("clang version"_str)) {
            return failure<ClangToolchain>("configured compiler is not clang++"_str);
        }

        auto resource_path      = PathBuf::from(resource->as_str());
        auto canonical_resource = toolchain::command::resolve_tool(resource_path.as_path(),
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
        auto timestamp = modified->as_unix_time();
        auto identity  = CompilerIdentity {
            .path                 = compiler_path.clone(),
            .version              = rstd::move(compiler_version).unwrap(),
            .target               = rstd::move(target).unwrap(),
            .resource_directory   = resolved_resource.clone(),
            .size                 = compiler_metadata->size(),
            .modified_seconds     = timestamp.seconds,
            .modified_nanoseconds = timestamp.nanoseconds,
        };

        return Ok(ClangToolchain {
            rstd::move(compiler_path),
            rstd::move(archiver_path),
            rstd::move(resolved_resource),
            rstd::move(identity),
        });
    }

    auto compiler_identity() const -> const CompilerIdentity& { return compiler_identity_; }
    auto target() const -> ref<str> { return compiler_identity_.target.as_str(); }
    auto resource_dir() const -> ref<rstd::path::Path> { return resource_dir_.as_path(); }

    auto builtin_context(const CompileContext& context) const -> Result<ClangBuiltinContext> {
        return make_builtin_context(context);
    }

    auto statistics() const -> ToolchainStatistics {
        auto result                   = toolchain_statistics_;
        result.builtin_snapshots      = builtin_environment_snapshots_.len();
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

    auto scan(const PreparedUnit& prepared,
              frontend::FrontendService& frontend_service) const
        -> Result<ScanResult> {
        if (prepared.frontend_result != nullptr) {
            frontend_service.record_analysis_hit();
            return Ok(modules::scan_from_frontend(*prepared.frontend_result,
                                                  prepared.unit.id));
        }
        auto facts = preprocess(prepared.unit.source.as_path(),
                                *prepared.unit.context,
                                prepared.working_directory.as_path(),
                                frontend_service);
        if (facts.is_err()) return Err(rstd::move(facts).unwrap_err());
        return Ok(modules::scan_from_frontend(*facts, prepared.unit.id));
    }

    auto preprocess(ref<rstd::path::Path> source,
                    const CompileContext& compile_context,
                    ref<rstd::path::Path> working_directory,
                    frontend::FrontendService& frontend_service) const
        -> Result<FrontendResult> {
        frontend_service.record_analysis_build();
        for (const auto& option : compile_context.options) {
            if (unsupported_native_preprocessor_option(option.as_str())) {
                return failure<FrontendResult>(rstd::format(
                    "compiler option '{}' is not supported by the native preprocessor",
                    option.as_str()));
            }
        }
        toolchain::PreprocessorEnvironment* environment = nullptr;
        auto working_text = working_directory.to_str();
        if (working_text.is_none()) {
            return failure<FrontendResult>(rstd::format(
                "preprocessor working directory '{}' is not valid UTF-8", working_directory));
        }
        for (auto& existing : preprocessor_environments_) {
            if (existing.context_id.as_str() == compile_context.id.as_str() &&
                existing.working_directory.as_path() == working_directory) {
                environment = rstd::addressof(existing);
                break;
            }
        }
        if (environment == nullptr) {
            auto builtin_context = make_builtin_context(compile_context);
            if (builtin_context.is_err()) {
                return Err(rstd::move(builtin_context).unwrap_err());
            }
            auto builtin_values  = rstd::move(builtin_context).unwrap();
            auto builtin_command = rstd::move(builtin_values.query_command);
            auto builtin_key     = rstd::move(builtin_values.key);
            toolchain_statistics_.ignored_builtin_options +=
                builtin_values.ignored_options;
            auto builtin_environment =
                Option<toolchain::SharedClangBuiltinEnvironmentSnapshot> {};
            for (const auto& existing : builtin_environment_snapshots_) {
                if (existing.get()->key.as_str() == builtin_key.as_str()) {
                    builtin_environment = Some(existing.clone());
                    ++toolchain_statistics_.builtin_hits;
                    break;
                }
            }
            if (builtin_environment.is_none()) {
                auto queried = toolchain::query_clang_builtin_environment_snapshot(
                    builtin_command,
                    builtin_key.as_str(),
                    builtin_values.semantic,
                    working_directory);
                if (queried.is_err()) return Err(rstd::move(queried).unwrap_err());
                ++toolchain_statistics_.builtin_refreshes;
                ++toolchain_statistics_.builtin_macro_processes;
                ++toolchain_statistics_.builtin_capability_processes;
                toolchain_statistics_.clang_macros += queried->get()->clang_macro_count;
                toolchain_statistics_.native_macro_owners +=
                    queried->get()->native_macro_count;
                toolchain_statistics_.clang_capabilities +=
                    queried->get()->clang_capability_count;
                toolchain_statistics_.native_capabilities +=
                    queried->get()->native_capability_count;
                toolchain_statistics_.builtin_macro_output_bytes +=
                    queried->get()->macro_output_bytes;
                toolchain_statistics_.builtin_capability_input_bytes +=
                    queried->get()->capability_input_bytes;
                toolchain_statistics_.builtin_capability_output_bytes +=
                    queried->get()->capability_output_bytes;
                builtin_environment_snapshots_.push(queried->clone());
                builtin_environment = Some(rstd::move(queried).unwrap());
            }
            auto command = Vec<String>::make();
            auto context = append_compile_context(command, compile_context);
            if (context.is_err()) return Err(rstd::move(context).unwrap_err());
            auto queried = toolchain::query_preprocessor_environment(
                command,
                compile_context.id.as_str(),
                working_directory,
                rstd::move(builtin_environment).unwrap(),
                rstd::move(builtin_values.semantic),
                compile_context.options,
                compile_context.definitions);
            if (queried.is_err()) return Err(rstd::move(queried).unwrap_err());
            queried->working_directory = PathBuf::from(working_directory);
            preprocessor_environments_.push(rstd::move(queried).unwrap());
            environment = rstd::addressof(
                preprocessor_environments_[preprocessor_environments_.len() - usize(1)]);
        }
        auto includes = toolchain::ClangIncludeResolver(*environment);
        auto builtins = toolchain::ClangBuiltinProvider(*environment, working_directory);
        auto pragmas  = toolchain::ClangPragmaHandler {};
        auto events   = toolchain::DependencyEvents {};
        auto consumer = frontend::parser::ModuleDependencyConsumer::make();
        auto translation = preprocessor::preprocess_to(
            preprocessor::PreprocessRequest {
                .source = PathBuf::from(source),
                .language_standard = compile_context.language_standard.clone(),
                .environment_identity = environment->identity.clone(),
            },
            frontend_service,
            includes,
            builtins,
            pragmas,
            events,
            consumer);
        if (translation.is_err()) {
            auto error = rstd::move(translation).unwrap_err();
            if (error.path.is_some() && error.location.is_some()) {
                return failure<FrontendResult>(rstd::format(
                    "native preprocessing failed at {}:{}:{}: {}",
                    error.path->as_path(),
                    error.location->line,
                    error.location->column,
                    error.message.as_str()));
            }
            return failure<FrontendResult>(rstd::format(
                "native preprocessing failed for '{}': {}", source, error.message.as_str()));
        }
        translation->header_inputs = events.take_headers();
        auto parsed = consumer.finish(*translation);
        if (parsed.is_err()) {
            return failure<FrontendResult>(
                rstd::move(parsed).unwrap_err().message.clone());
        }
        return Ok(rstd::move(parsed).unwrap());
    }

    auto prepare_compile(const PreparedUnit&           prepared,
                         const ScanResult&             scan_result,
                         Option<ref<rstd::path::Path>> prebuilt_module_path) const
        -> Result<CompileInvocation> {
        auto command = Vec<String>::make();
        auto context = append_compile_context(command, *prepared.unit.context);
        if (context.is_err()) return Err(rstd::move(context).unwrap_err());
        if (scan_result.provided.is_some()) {
            if (prepared.unit.bmi.is_none()) {
                return failure<CompileInvocation>(rstd::format("module unit has no BMI output: {}",
                                                               prepared.unit.source.as_path()));
            }
            auto parent = create_parent((*prepared.unit.bmi).as_path());
            if (parent.is_err()) return Err(rstd::move(parent).unwrap_err());
            toolchain::command::push_option(command, toolchain::clang_options::LANGUAGE);
            toolchain::command::push_option(command, toolchain::clang_options::CXX_MODULE);
            auto pushed = toolchain::command::push_path_option(
                command, toolchain::clang_options::MODULE_OUTPUT, (*prepared.unit.bmi).as_path());
            if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        } else {
            auto extension      = prepared.unit.source.as_path().extension();
            auto extension_text = extension.is_some() ? (*extension).to_str() : None();
            if (extension_text.is_some() && *extension_text == "cppm"_str) {
                toolchain::command::push_option(command, toolchain::clang_options::LANGUAGE);
                toolchain::command::push_option(command, toolchain::clang_options::CXX_SOURCE);
            }
        }
        if (prebuilt_module_path.is_some()) {
            auto pushed = toolchain::command::push_path_option(
                command, toolchain::clang_options::PREBUILT_MODULE_PATH, *prebuilt_module_path);
            if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        }
        toolchain::command::push_option(command, toolchain::clang_options::COMPILE);
        auto pushed = toolchain::command::push_path(command, prepared.unit.source.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(command, toolchain::clang_options::OUTPUT);
        pushed = toolchain::command::push_path(command, prepared.unit.object.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());

        auto identity = invocation_identity(command, prepared.working_directory.as_path());
        if (identity.is_err()) return Err(rstd::move(identity).unwrap_err());
        return Ok(CompileInvocation {
            .arguments         = rstd::move(command),
            .working_directory = prepared.working_directory.clone(),
            .identity          = rstd::move(identity).unwrap(),
        });
    }

    auto execute_compile(const CompileInvocation& invocation, ref<rstd::path::Path> source) const
        -> Result<empty> {
        auto output =
            run_command(invocation.arguments, Some(invocation.working_directory.as_path()));
        if (output.is_err()) return Err(rstd::move(output).unwrap_err());
        auto command_output = rstd::move(output).unwrap();
        if (command_output.exit_code != i32 {}) {
            return failure<empty>(rstd::format("clang++ failed for '{}'\n{}\n{}",
                                               source,
                                               command_text(invocation.arguments).as_str(),
                                               command_output.standard_error.as_str()));
        }
        return Ok(empty {});
    }

    auto archive(ref<rstd::path::Path> output_path,
                 const Vec<PathBuf>&   objects,
                 ref<rstd::path::Path> working_directory) const -> Result<empty> {
        auto parent = create_parent(output_path);
        if (parent.is_err()) return parent;
        auto archive_exists = rstd::fs::exists(output_path);
        if (archive_exists.is_err()) {
            return failure<empty>(rstd::format("cannot inspect archive '{}': {}",
                                               output_path,
                                               rstd::move(archive_exists).unwrap_err()));
        }
        if (*archive_exists) {
            auto removed = rstd::fs::remove_file(output_path);
            if (removed.is_err()) {
                return failure<empty>(rstd::format("cannot replace archive '{}': {}",
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
        auto output = run_command(command, Some(working_directory));
        if (output.is_err()) return Err(rstd::move(output).unwrap_err());
        auto command_output = rstd::move(output).unwrap();
        if (command_output.exit_code != i32 {}) {
            return failure<empty>(rstd::format("llvm-ar failed for '{}'\n{}\n{}",
                                               output_path,
                                               command_text(command).as_str(),
                                               command_output.standard_error.as_str()));
        }
        return Ok(empty {});
    }

    auto link_executable(ref<rstd::path::Path> output_path,
                         const Vec<PathBuf>&   objects,
                         const Vec<PathBuf>&   archives,
                         StandardLibrary       standard_library,
                         const Vec<String>&    linker_options,
                         ref<rstd::path::Path> working_directory) const -> Result<empty> {
        auto parent = create_parent(output_path);
        if (parent.is_err()) return parent;
        auto command = Vec<String>::make();
        auto pushed  = toolchain::command::push_path(command, compiler_.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(
            command, toolchain::clang_options::standard_library(standard_library));
        for (const auto& option : linker_options) command.push(option.clone());
        for (const auto& object : objects) {
            pushed = toolchain::command::push_path(command, object.as_path());
            if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        }
        for (const auto& archive : archives) {
            pushed = toolchain::command::push_path(command, archive.as_path());
            if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        }
        toolchain::command::push_option(command, toolchain::clang_options::OUTPUT);
        pushed = toolchain::command::push_path(command, output_path);
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());

        auto output = run_command(command, Some(working_directory));
        if (output.is_err()) return Err(rstd::move(output).unwrap_err());
        auto command_output = rstd::move(output).unwrap();
        if (command_output.exit_code != i32 {}) {
            return failure<empty>(rstd::format("clang++ failed to link '{}'\n{}\n{}",
                                               output_path,
                                               command_text(command).as_str(),
                                               command_output.standard_error.as_str()));
        }
        return Ok(empty {});
    }

private:
    ClangToolchain(PathBuf          compiler,
                   PathBuf          archiver,
                   PathBuf          resource_dir,
                   CompilerIdentity identity)
        : compiler_(rstd::move(compiler)),
          archiver_(rstd::move(archiver)),
          resource_dir_(rstd::move(resource_dir)),
          compiler_identity_(rstd::move(identity)) {}

    auto make_builtin_context(const CompileContext& context) const
        -> Result<ClangBuiltinContext> {
        if (!is_supported_cpp_standard(context.language_standard.as_str())) {
            return failure<ClangBuiltinContext>(rstd::format(
                "unsupported C++ language standard '{}'; expected C++20 or later",
                context.language_standard.as_str()));
        }
        auto command = Vec<String>::make();
        auto pushed = toolchain::command::push_path(command, compiler_.as_path());
        if (pushed.is_err()) {
            return Err(rstd::move(pushed).unwrap_err());
        }
        toolchain::command::push_option(command, toolchain::clang_options::RESOURCE_DIR);
        pushed = toolchain::command::push_path(command, resource_dir_.as_path());
        if (pushed.is_err()) {
            return Err(rstd::move(pushed).unwrap_err());
        }
        command.push(rstd::format(
            "{}{}", toolchain::clang_options::STANDARD, context.language_standard.as_str()));
        auto normalized = normalize_builtin_options(context.options);
        if (normalized.is_err()) return Err(rstd::move(normalized).unwrap_err());
        auto options = rstd::move(normalized).unwrap();
        auto ignored_options = options.ignored_options;
        if (options.target.is_some()) command.push(rstd::move(options.target).unwrap());
        if (options.sysroot.is_some()) command.push(rstd::move(options.sysroot).unwrap());
        if (options.optimization.is_some()) {
            command.push(rstd::move(options.optimization).unwrap());
        }
        auto last_options = options.last_options.into_iter();
        while (auto option = last_options.next()) {
            command.push(rstd::move((*option).template get<1>()));
        }
        for (auto& option : options.ordered_options) command.push(rstd::move(option));
        toolchain::command::push_option(command, toolchain::clang_options::NO_RTTI);
        toolchain::command::push_option(command, toolchain::clang_options::NO_EXCEPTIONS);
        auto key = argument_identity("tenon-clang-builtin-context-v3"_str, command);
        key.push_str(toolchain::CLANG_STANDARD_LIBRARY_CAPABILITY_ID);
        key.push_ascii('\n');
        key.push_str(compiler_identity_.version.as_str());
        key.push_ascii('\n');
        key.push_str(rstd::format("{}:{}:{}", compiler_identity_.size,
                                  compiler_identity_.modified_seconds,
                                  compiler_identity_.modified_nanoseconds)
                         .as_str());
        return Ok(ClangBuiltinContext {
            .query_command  = rstd::move(command),
            .semantic       = toolchain::BuiltinSemanticContext {
                .language_standard = context.language_standard.clone(),
                .rtti              = false,
                .exceptions        = false,
            },
            .key             = rstd::move(key),
            .ignored_options = ignored_options,
        });
    }

    auto append_compile_context(Vec<String>& command, const CompileContext& context) const
        -> Result<empty> {
        auto pushed = toolchain::command::push_path(command, compiler_.as_path());
        if (pushed.is_err()) return pushed;
        toolchain::command::push_option(command, toolchain::clang_options::RESOURCE_DIR);
        pushed = toolchain::command::push_path(command, resource_dir_.as_path());
        if (pushed.is_err()) return pushed;
        command.push(rstd::format(
            "{}{}", toolchain::clang_options::STANDARD, context.language_standard.as_str()));
        toolchain::command::push_option(
            command, toolchain::clang_options::standard_library(context.standard_library));
        toolchain::command::push_option(command, toolchain::clang_options::bmi(context.bmi_mode));
        for (const auto& option : context.options) command.push(option.clone());
        toolchain::command::push_option(command, toolchain::clang_options::NO_RTTI);
        toolchain::command::push_option(command, toolchain::clang_options::NO_EXCEPTIONS);
        for (const auto& definition : context.definitions) {
            command.push(
                rstd::format("{}{}", toolchain::clang_options::DEFINE, definition.as_str()));
        }
        for (const auto& include : context.include_directories) {
            toolchain::command::push_option(command, toolchain::clang_options::INCLUDE);
            pushed = toolchain::command::push_path(command, include.as_path());
            if (pushed.is_err()) return pushed;
        }
        return Ok(empty {});
    }

    PathBuf          compiler_;
    PathBuf          archiver_;
    PathBuf          resource_dir_;
    CompilerIdentity compiler_identity_;
    mutable ToolchainStatistics toolchain_statistics_;
    mutable Vec<toolchain::SharedClangBuiltinEnvironmentSnapshot>
        builtin_environment_snapshots_;
    mutable Vec<toolchain::PreprocessorEnvironment> preprocessor_environments_;
};

} // namespace tenon
