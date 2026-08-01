export module tenon.toolchain:clang;

import rstd;
import tenon.model;
import tenon.process;
import :clang_options;
import :clang_depfile;
import :clang_preprocessor;
import :command;

using namespace rstd::prelude;
using namespace rstd::literals;

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

auto cleanup_scratch(ref<rstd::path::Path> path) noexcept -> void {
    auto removed = rstd::fs::remove_file(path);
    (void)removed;
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

} // namespace tenon

export namespace tenon
{

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

    auto prepare(UnitSpec unit, ref<rstd::path::Path> working_directory) const
        -> Result<PreparedUnit> {
        auto object_parent = create_parent(unit.object.as_path());
        if (object_parent.is_err()) return Err(rstd::move(object_parent).unwrap_err());
        auto dep_parent = create_parent(unit.scan_depfile.as_path());
        if (dep_parent.is_err()) return Err(rstd::move(dep_parent).unwrap_err());
        return Ok(PreparedUnit {
            .unit              = rstd::move(unit),
            .working_directory = PathBuf::from(working_directory),
        });
    }

    auto scan(const PreparedUnit& prepared) const -> Result<ScanResult> {
        if (prepared.preprocessed != nullptr) {
            return Ok(toolchain::scan_from_preprocessed(*prepared.preprocessed, prepared.unit.id));
        }
        auto facts = preprocess(prepared.unit.source.as_path(),
                                prepared.unit.object.as_path(),
                                prepared.unit.scan_depfile.as_path(),
                                *prepared.unit.context,
                                prepared.working_directory.as_path());
        if (facts.is_err()) return Err(rstd::move(facts).unwrap_err());
        return Ok(toolchain::scan_from_preprocessed(*facts, prepared.unit.id));
    }

    auto preprocess(ref<rstd::path::Path> source,
                    ref<rstd::path::Path> dependency_target,
                    ref<rstd::path::Path> depfile,
                    const CompileContext& compile_context,
                    ref<rstd::path::Path> working_directory) const
        -> Result<PreprocessedModuleFacts> {
        auto parent = create_parent(depfile);
        if (parent.is_err()) return Err(rstd::move(parent).unwrap_err());
        auto command = Vec<String>::make();
        auto context = append_compile_context(command, compile_context);
        if (context.is_err()) return Err(rstd::move(context).unwrap_err());
        toolchain::command::push_option(command, toolchain::clang_options::PREPROCESS);
        toolchain::command::push_option(command, toolchain::clang_options::DEPENDENCIES);
        toolchain::command::push_option(command, toolchain::clang_options::DEPENDENCY_TARGET);
        auto pushed = toolchain::command::push_path(command, dependency_target);
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(command, toolchain::clang_options::DEPENDENCY_FILE);
        pushed = toolchain::command::push_path(command, depfile);
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        pushed = toolchain::command::push_path(command, source);
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());

        auto output = run_command(command, Some(working_directory));
        if (output.is_err()) {
            cleanup_scratch(depfile);
            return Err(rstd::move(output).unwrap_err());
        }
        auto command_output = rstd::move(output).unwrap();
        if (command_output.exit_code != i32 {}) {
            cleanup_scratch(depfile);
            return failure<PreprocessedModuleFacts>(
                rstd::format("clang++ -E failed for '{}'\n{}\n{}",
                             source,
                             command_text(command).as_str(),
                             command_output.standard_error.as_str()));
        }
        auto facts =
            toolchain::parse_preprocessed_module(command_output.standard_output.as_str(), source);
        if (facts.is_err()) {
            cleanup_scratch(depfile);
            return facts;
        }
        auto headers = toolchain::parse_depfile(depfile, working_directory);
        cleanup_scratch(depfile);
        if (headers.is_err()) return Err(rstd::move(headers).unwrap_err());
        facts->header_inputs = rstd::move(headers).unwrap();
        return facts;
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
};

} // namespace tenon
