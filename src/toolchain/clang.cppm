export module tenon.toolchain.clang;

import rstd;
import tenon.model;
import tenon.process;
import tenon.toolchain.clang_options;
import tenon.toolchain.clang_scan_deps;
import tenon.toolchain.command;

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

} // namespace tenon

export namespace tenon
{

class ClangToolchain {
public:
    static auto create(const ToolchainSpec& specification) -> Result<ClangToolchain> {
        auto compiler =
            toolchain::command::resolve_tool(specification.compiler.as_path(), "clang++"_str);
        auto scanner = toolchain::ClangScanDeps::create(specification.scanner.as_path());
        auto archiver =
            toolchain::command::resolve_tool(specification.archiver.as_path(), "llvm-ar"_str);
        if (compiler.is_err()) return Err(rstd::move(compiler).unwrap_err());
        if (scanner.is_err()) return Err(rstd::move(scanner).unwrap_err());
        if (archiver.is_err()) return Err(rstd::move(archiver).unwrap_err());
        auto compiler_path = rstd::move(compiler).unwrap();
        auto scanner_tool  = rstd::move(scanner).unwrap();
        auto archiver_path = rstd::move(archiver).unwrap();

        auto compiler_command = Vec<String>::make();
        auto archiver_command = Vec<String>::make();
        auto target_command   = Vec<String>::make();
        auto resource_command = Vec<String>::make();
        auto pushed = toolchain::command::push_path(compiler_command, compiler_path.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(compiler_command, toolchain::clang_options::VERSION);
        pushed = toolchain::command::push_path(archiver_command, archiver_path.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(archiver_command, toolchain::clang_options::VERSION);
        pushed = toolchain::command::push_path(target_command, compiler_path.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(
            target_command, toolchain::clang_options::PRINT_TARGET_TRIPLE);
        pushed = toolchain::command::push_path(resource_command, compiler_path.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(
            resource_command, toolchain::clang_options::PRINT_RESOURCE_DIR);

        auto compiler_version = toolchain::command::tool_output(
            rstd::move(compiler_command), "clang++ --version"_str);
        auto archiver_version = toolchain::command::tool_output(
            rstd::move(archiver_command), "llvm-ar --version"_str);
        auto target = toolchain::command::tool_output(
            rstd::move(target_command), "clang++ target query"_str);
        auto resource = toolchain::command::tool_output(
            rstd::move(resource_command), "clang++ resource query"_str);
        if (compiler_version.is_err()) return Err(rstd::move(compiler_version).unwrap_err());
        if (archiver_version.is_err()) return Err(rstd::move(archiver_version).unwrap_err());
        if (target.is_err()) return Err(rstd::move(target).unwrap_err());
        if (resource.is_err()) return Err(rstd::move(resource).unwrap_err());
        if (! compiler_version->as_str().contains("clang version"_str)) {
            return failure<ClangToolchain>("configured compiler is not clang++"_str);
        }

        auto resource_path = PathBuf::from(resource->as_str());
        auto canonical_resource = toolchain::command::resolve_tool(
            resource_path.as_path(), "Clang resource directory"_str);
        if (canonical_resource.is_err()) {
            return Err(rstd::move(canonical_resource).unwrap_err());
        }

        auto identity = String::make("tenon-clang-recipe-v4\n"_str);
        auto append_identity_path = [&](ref<rstd::path::Path> value) {
            auto text = value.to_str();
            if (text.is_some()) identity.push_str(*text);
            identity.push_ascii('\n');
        };
        append_identity_path(compiler_path.as_path());
        identity.push_str(compiler_version->as_str());
        identity.push_ascii('\n');
        append_identity_path(scanner_tool.path());
        identity.push_str(scanner_tool.version());
        identity.push_ascii('\n');
        append_identity_path(archiver_path.as_path());
        identity.push_str(archiver_version->as_str());
        identity.push_ascii('\n');
        identity.push_str(target->as_str());
        identity.push_ascii('\n');
        auto resolved_resource = rstd::move(canonical_resource).unwrap();
        append_identity_path(resolved_resource.as_path());

        return Ok(ClangToolchain {
            rstd::move(compiler_path),
            rstd::move(scanner_tool),
            rstd::move(archiver_path),
            rstd::move(resolved_resource),
            rstd::move(target).unwrap(),
            rstd::move(identity),
        });
    }

    auto identity() const -> ref<str> { return identity_.as_str(); }
    auto target() const -> ref<str> { return target_.as_str(); }
    auto resource_dir() const -> ref<rstd::path::Path> { return resource_dir_.as_path(); }

    auto prepare(UnitSpec unit, ref<rstd::path::Path> working_directory) const
        -> Result<PreparedUnit> {
        auto object_parent = create_parent(unit.object.as_path());
        if (object_parent.is_err()) return Err(rstd::move(object_parent).unwrap_err());
        auto dep_parent = create_parent(unit.depfile.as_path());
        if (dep_parent.is_err()) return Err(rstd::move(dep_parent).unwrap_err());
        return Ok(PreparedUnit {
            .unit = rstd::move(unit),
            .working_directory = PathBuf::from(working_directory),
        });
    }

    auto scan(const PreparedUnit& prepared) const -> Result<ScanResult> {
        auto compiler_arguments = Vec<String>::make();
        auto context = append_compile_context(compiler_arguments, *prepared.unit.context);
        if (context.is_err()) return Err(rstd::move(context).unwrap_err());
        return scanner_.scan(prepared, rstd::move(compiler_arguments));
    }

    auto compile(const PreparedUnit& prepared,
                 const ScanResult& scan_result,
                 const Vec<ResolvedModuleArtifact>& module_inputs) const -> Result<empty> {

        auto command = Vec<String>::make();
        auto context = append_compile_context(command, *prepared.unit.context);
        if (context.is_err()) return context;
        if (scan_result.provided.is_some()) {
            if (prepared.unit.bmi.is_none()) {
                return failure<empty>(rstd::format(
                    "module unit has no BMI output: {}", prepared.unit.source.as_path()));
            }
            auto parent = create_parent((*prepared.unit.bmi).as_path());
            if (parent.is_err()) return parent;
            toolchain::command::push_option(command, toolchain::clang_options::LANGUAGE);
            toolchain::command::push_option(command, toolchain::clang_options::CXX_MODULE);
            command.push(rstd::format(
                "{}{}", toolchain::clang_options::MODULE_OUTPUT, (*prepared.unit.bmi).as_path()));
        }
        for (const auto& input : module_inputs) {
            command.push(rstd::format("{}{}={}",
                                      toolchain::clang_options::MODULE_FILE,
                                      input.logical_name.as_str(),
                                      input.bmi.as_path()));
        }
        toolchain::command::push_option(command, toolchain::clang_options::COMPILE);
        auto pushed = toolchain::command::push_path(command, prepared.unit.source.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
        toolchain::command::push_option(command, toolchain::clang_options::OUTPUT);
        pushed = toolchain::command::push_path(command, prepared.unit.object.as_path());
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());

        auto output = run_command(command, Some(prepared.working_directory.as_path()));
        if (output.is_err()) return Err(rstd::move(output).unwrap_err());
        auto command_output = rstd::move(output).unwrap();
        if (command_output.exit_code != i32 {}) {
            return failure<empty>(rstd::format(
                "clang++ failed for '{}'\n{}\n{}",
                prepared.unit.source.as_path(),
                command_text(command).as_str(),
                command_output.standard_error.as_str()));
        }
        return Ok(empty {});
    }

    auto archive(ref<rstd::path::Path> output_path,
                 const Vec<PathBuf>& objects,
                 ref<rstd::path::Path> working_directory) const -> Result<empty> {

        auto parent = create_parent(output_path);
        if (parent.is_err()) return parent;
        auto archive_exists = rstd::fs::exists(output_path);
        if (archive_exists.is_err()) {
            return failure<empty>(rstd::format(
                "cannot inspect archive '{}': {}",
                output_path,
                rstd::move(archive_exists).unwrap_err()));
        }
        if (*archive_exists) {
            auto removed = rstd::fs::remove_file(output_path);
            if (removed.is_err()) {
                return failure<empty>(rstd::format(
                    "cannot replace archive '{}': {}",
                    output_path,
                    rstd::move(removed).unwrap_err()));
            }
        }
        auto command = Vec<String>::make();
        auto pushed = toolchain::command::push_path(command, archiver_.as_path());
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
            return failure<empty>(rstd::format(
                "llvm-ar failed for '{}'\n{}\n{}",
                output_path,
                command_text(command).as_str(),
                command_output.standard_error.as_str()));
        }
        return Ok(empty {});
    }

    auto link_executable(ref<rstd::path::Path> output_path,
                         const Vec<PathBuf>& objects,
                         const Vec<PathBuf>& archives,
                         StandardLibrary standard_library,
                         const Vec<String>& linker_options,
                         ref<rstd::path::Path> working_directory) const
        -> Result<empty> {

        auto parent = create_parent(output_path);
        if (parent.is_err()) return parent;
        auto command = Vec<String>::make();
        auto pushed = toolchain::command::push_path(command, compiler_.as_path());
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
            return failure<empty>(rstd::format(
                "clang++ failed to link '{}'\n{}\n{}",
                output_path,
                command_text(command).as_str(),
                command_output.standard_error.as_str()));
        }
        return Ok(empty {});
    }

private:
    ClangToolchain(PathBuf compiler,
                   toolchain::ClangScanDeps scanner,
                   PathBuf archiver,
                   PathBuf resource_dir,
                   String target,
                   String identity)
        : compiler_(rstd::move(compiler)),
          scanner_(rstd::move(scanner)),
          archiver_(rstd::move(archiver)),
          resource_dir_(rstd::move(resource_dir)),
          target_(rstd::move(target)),
          identity_(rstd::move(identity)) {}

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
            command.push(rstd::format(
                "{}{}", toolchain::clang_options::DEFINE, definition.as_str()));
        }
        for (const auto& include : context.include_directories) {
            toolchain::command::push_option(command, toolchain::clang_options::INCLUDE);
            pushed = toolchain::command::push_path(command, include.as_path());
            if (pushed.is_err()) return pushed;
        }
        return Ok(empty {});
    }

    PathBuf compiler_;
    toolchain::ClangScanDeps scanner_;
    PathBuf archiver_;
    PathBuf resource_dir_;
    String  target_;
    String  identity_;
};

} // namespace tenon
