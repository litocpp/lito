export module tenon.clang_toolchain;

import rstd;
import rstd.json;
import tenon.model;
import tenon.process;

namespace tenon::clang_detail
{

using namespace rstd::literals;

using Json      = rstd::json::Value;
using StringSet = rstd::collections::BTreeMap<String, rstd::empty>;

template<typename T>
auto failure(String message) -> Result<T> {
    return rstd::Err(Error::make(ErrorKind::Toolchain, rstd::move(message)));
}

template<typename T>
auto failure(rstd::ref<rstd::str> message) -> Result<T> {
    return rstd::Err(Error::make(ErrorKind::Toolchain, message));
}

auto member(const Json& value, rstd::ref<rstd::str> key) -> rstd::Option<rstd::ref<Json>> {
    return value.get(key);
}

auto json_string(const Json& value, rstd::ref<rstd::str> context) -> Result<String> {
    auto text = value.as_str();
    if (text.is_none()) return failure<String>(rstd::format("{} must be a string", context));
    return rstd::Ok(String::make(*text));
}

auto required_json_string(const Json& object,
                          rstd::ref<rstd::str> key,
                          rstd::ref<rstd::str> context) -> Result<String> {
    auto value = member(object, key);
    if (value.is_none()) {
        return failure<String>(rstd::format("{} is missing '{}'", context, key));
    }
    return json_string(**value, rstd::format("{}.{}", context, key).as_str());
}

auto canonical_tool(rstd::ref<rstd::path::Path> path, rstd::ref<rstd::str> name)
    -> Result<PathBuf> {
    auto canonical = rstd::fs::canonicalize(path);
    if (canonical.is_err()) {
        return failure<PathBuf>(rstd::format(
            "cannot resolve {} '{}': {}", name, path, rstd::move(canonical).unwrap_err()));
    }
    return rstd::Ok(rstd::move(canonical).unwrap());
}

auto path_argument(rstd::ref<rstd::path::Path> path) -> Result<String> {
    auto text = path.to_str();
    if (text.is_none()) {
        return failure<String>(rstd::format("tool path '{}' is not valid UTF-8", path));
    }
    return rstd::Ok(String::make(*text));
}

auto push_path(Vec<String>& command, rstd::ref<rstd::path::Path> path) -> Result<rstd::empty> {
    auto argument = path_argument(path);
    if (argument.is_err()) return rstd::Err(rstd::move(argument).unwrap_err());
    command.push(rstd::move(argument).unwrap());
    return rstd::Ok(rstd::empty {});
}

auto tool_output(Vec<String> command, rstd::ref<rstd::str> description) -> Result<String> {
    auto output = run_command(command);
    if (output.is_err()) return rstd::Err(rstd::move(output).unwrap_err());
    auto value = rstd::move(output).unwrap();
    if (value.exit_code != rstd::i32 {}) {
        return failure<String>(rstd::format(
            "{} failed with exit code {}:\n{}",
            description,
            value.exit_code,
            value.standard_error.as_str()));
    }
    return rstd::Ok(trim_ascii(rstd::move(value.standard_output)));
}

auto create_parent(rstd::ref<rstd::path::Path> path) -> Result<rstd::empty> {
    auto parent = path.parent();
    if (parent.is_none()) {
        return failure<rstd::empty>(rstd::format("output path '{}' has no parent", path));
    }
    auto created = rstd::fs::create_dir_all(*parent);
    if (created.is_err()) {
        return failure<rstd::empty>(rstd::format(
            "cannot create directory '{}': {}", *parent, rstd::move(created).unwrap_err()));
    }
    return rstd::Ok(rstd::empty {});
}

auto read_file(rstd::ref<rstd::path::Path> path) -> Result<String> {
    auto contents = rstd::fs::read_to_string(path);
    if (contents.is_err()) {
        return failure<String>(
            rstd::format("cannot read '{}': {}", path, rstd::move(contents).unwrap_err()));
    }
    return rstd::Ok(rstd::move(contents).unwrap());
}

auto parse_depfile(rstd::ref<rstd::path::Path> depfile,
                   rstd::ref<rstd::path::Path> working_directory) -> Result<Vec<PathBuf>> {
    auto contents_result = read_file(depfile);
    if (contents_result.is_err()) return rstd::Err(rstd::move(contents_result).unwrap_err());
    auto contents = rstd::move(contents_result).unwrap();
    auto bytes    = contents.as_str().as_bytes();

    auto colon   = rstd::Option<rstd::usize> {};
    auto escaped = false;
    for (auto index = rstd::usize {}; index < contents.len(); ++index) {
        const auto value = bytes[index];
        if (! escaped && value == rstd::u8(':')) {
            colon = rstd::Some(index);
            break;
        }
        if (! escaped && value == rstd::u8('\\')) {
            escaped = true;
        } else {
            escaped = false;
        }
    }
    if (colon.is_none()) {
        return failure<Vec<PathBuf>>(
            rstd::format("invalid dependency file '{}': missing ':'", depfile));
    }

    auto tokens  = Vec<String>::make();
    auto current = String::make();
    for (auto index = *colon + rstd::usize(1); index < contents.len(); ++index) {
        const auto value = bytes[index];
        if (value == rstd::u8('\\') && index + rstd::usize(1) < contents.len()) {
            const auto next = bytes[index + rstd::usize(1)];
            if (next == rstd::u8('\n')) {
                ++index;
                continue;
            }
            if (next == rstd::u8('\r') && index + rstd::usize(2) < contents.len() &&
                bytes[index + rstd::usize(2)] == rstd::u8('\n')) {
                index += rstd::usize(2);
                continue;
            }
            current.push_ascii(next);
            ++index;
            continue;
        }
        if (value == rstd::u8(' ') || value == rstd::u8('\t') || value == rstd::u8('\r') ||
            value == rstd::u8('\n')) {
            if (! current.is_empty()) {
                tokens.push(rstd::move(current));
                current = String::make();
            }
            continue;
        }
        current.push_ascii(value);
    }
    if (! current.is_empty()) tokens.push(rstd::move(current));

    auto result = Vec<PathBuf>::make();
    auto seen   = StringSet::make();
    for (const auto& token : tokens) {
        auto path = PathBuf::from(token.as_str());
        if (path.as_path().is_relative()) {
            path = PathBuf::from(working_directory).join(path.as_path());
        }
        auto canonical = rstd::fs::canonicalize(path.as_path());
        if (canonical.is_err()) {
            return failure<Vec<PathBuf>>(rstd::format(
                "cannot resolve dependency input '{}': {}",
                path.as_path(),
                rstd::move(canonical).unwrap_err()));
        }
        auto resolved = rstd::move(canonical).unwrap();
        auto text = resolved.as_path().to_str();
        if (text.is_none()) {
            return failure<Vec<PathBuf>>(
                rstd::format("dependency input '{}' is not valid UTF-8", resolved.as_path()));
        }
        if (! seen.contains_key(*text)) {
            seen.insert(String::make(*text), rstd::empty {});
            result.push(rstd::move(resolved));
        }
    }
    return rstd::Ok(rstd::move(result));
}

auto parse_scan_json(rstd::ref<rstd::str> output, UnitId unit) -> Result<ScanResult> {
    auto parsed = rstd::json::from_str(output);
    if (parsed.is_err()) {
        return failure<ScanResult>(rstd::format(
            "cannot parse clang-scan-deps P1689 output: {}", rstd::move(parsed).unwrap_err()));
    }
    auto document = rstd::move(parsed).unwrap();
    auto rules    = member(document, "rules"_str);
    if (rules.is_none()) {
        return failure<ScanResult>("P1689 output must contain exactly one rule"_str);
    }
    auto rules_array = (**rules).as_array();
    if (rules_array.is_none() || (**rules_array).len() != rstd::usize(1)) {
        return failure<ScanResult>("P1689 output must contain exactly one rule"_str);
    }
    const auto& rule = (**rules_array)[rstd::usize {}];
    auto result      = ScanResult { .unit = unit };

    auto provides = member(rule, "provides"_str);
    if (provides.is_some()) {
        auto array = (**provides).as_array();
        if (array.is_none() || (**array).len() != rstd::usize(1)) {
            return failure<ScanResult>("P1689 provides must contain exactly one module"_str);
        }
        const auto& provided = (**array)[rstd::usize {}];
        auto logical_name =
            required_json_string(provided, "logical-name"_str, "P1689 provides"_str);
        if (logical_name.is_err()) return rstd::Err(rstd::move(logical_name).unwrap_err());
        auto interface_value = member(provided, "is-interface"_str);
        if (interface_value.is_none()) {
            return failure<ScanResult>("P1689 provides.is-interface must be a bool"_str);
        }
        auto interface_flag = (**interface_value).as_bool();
        if (interface_flag.is_none()) {
            return failure<ScanResult>("P1689 provides.is-interface must be a bool"_str);
        }
        result.provided = rstd::Some(ProvidedModule {
            .logical_name = rstd::move(logical_name).unwrap(),
            .is_interface = *interface_flag,
        });
    }

    auto requirements = member(rule, "requires"_str);
    if (requirements.is_some()) {
        auto array = (**requirements).as_array();
        if (array.is_none()) {
            return failure<ScanResult>("P1689 requires must be an array"_str);
        }
        auto names = StringSet::make();
        for (const auto& required : **array) {
            auto logical_name =
                required_json_string(required, "logical-name"_str, "P1689 requires"_str);
            if (logical_name.is_err()) return rstd::Err(rstd::move(logical_name).unwrap_err());
            auto name = rstd::move(logical_name).unwrap();
            if (! names.contains_key(name.as_str())) {
                names.insert(name.clone(), rstd::empty {});
                result.required_modules.push(rstd::move(name));
            }
        }
    }
    return rstd::Ok(rstd::move(result));
}

auto standard_library_flag(StandardLibrary value) -> String {
    return String::make(value == StandardLibrary::Libstdcxx ? "-stdlib=libstdc++"_str
                                                             : "-stdlib=libc++"_str);
}

auto bmi_flag(BmiMode value) -> String {
    return String::make(value == BmiMode::Reduced ? "-fmodules-reduced-bmi"_str
                                                   : "-fno-modules-reduced-bmi"_str);
}

} // namespace tenon::clang_detail

export namespace tenon
{

class ClangToolchain {
public:
    static auto create(const ToolchainSpec& specification) -> Result<ClangToolchain> {
        using namespace clang_detail;
        using namespace rstd::literals;

        auto compiler = canonical_tool(specification.compiler.as_path(), "clang++"_str);
        auto scanner  = canonical_tool(specification.scanner.as_path(), "clang-scan-deps"_str);
        auto archiver = canonical_tool(specification.archiver.as_path(), "llvm-ar"_str);
        if (compiler.is_err()) return rstd::Err(rstd::move(compiler).unwrap_err());
        if (scanner.is_err()) return rstd::Err(rstd::move(scanner).unwrap_err());
        if (archiver.is_err()) return rstd::Err(rstd::move(archiver).unwrap_err());
        auto compiler_path = rstd::move(compiler).unwrap();
        auto scanner_path  = rstd::move(scanner).unwrap();
        auto archiver_path = rstd::move(archiver).unwrap();

        auto compiler_command = Vec<String>::make();
        auto scanner_command  = Vec<String>::make();
        auto archiver_command = Vec<String>::make();
        auto target_command   = Vec<String>::make();
        auto resource_command = Vec<String>::make();
        auto pushed = push_path(compiler_command, compiler_path.as_path());
        if (pushed.is_err()) return rstd::Err(rstd::move(pushed).unwrap_err());
        compiler_command.push(String::make("--version"_str));
        pushed = push_path(scanner_command, scanner_path.as_path());
        if (pushed.is_err()) return rstd::Err(rstd::move(pushed).unwrap_err());
        scanner_command.push(String::make("--version"_str));
        pushed = push_path(archiver_command, archiver_path.as_path());
        if (pushed.is_err()) return rstd::Err(rstd::move(pushed).unwrap_err());
        archiver_command.push(String::make("--version"_str));
        pushed = push_path(target_command, compiler_path.as_path());
        if (pushed.is_err()) return rstd::Err(rstd::move(pushed).unwrap_err());
        target_command.push(String::make("-print-target-triple"_str));
        pushed = push_path(resource_command, compiler_path.as_path());
        if (pushed.is_err()) return rstd::Err(rstd::move(pushed).unwrap_err());
        resource_command.push(String::make("-print-resource-dir"_str));

        auto compiler_version =
            tool_output(rstd::move(compiler_command), "clang++ --version"_str);
        auto scanner_version =
            tool_output(rstd::move(scanner_command), "clang-scan-deps --version"_str);
        auto archiver_version =
            tool_output(rstd::move(archiver_command), "llvm-ar --version"_str);
        auto target = tool_output(rstd::move(target_command), "clang++ target query"_str);
        auto resource = tool_output(rstd::move(resource_command), "clang++ resource query"_str);
        if (compiler_version.is_err()) return rstd::Err(rstd::move(compiler_version).unwrap_err());
        if (scanner_version.is_err()) return rstd::Err(rstd::move(scanner_version).unwrap_err());
        if (archiver_version.is_err()) return rstd::Err(rstd::move(archiver_version).unwrap_err());
        if (target.is_err()) return rstd::Err(rstd::move(target).unwrap_err());
        if (resource.is_err()) return rstd::Err(rstd::move(resource).unwrap_err());
        if (! compiler_version->as_str().contains("clang version"_str)) {
            return failure<ClangToolchain>("configured compiler is not clang++"_str);
        }

        auto resource_path = PathBuf::from(resource->as_str());
        auto canonical_resource =
            canonical_tool(resource_path.as_path(), "Clang resource directory"_str);
        if (canonical_resource.is_err()) {
            return rstd::Err(rstd::move(canonical_resource).unwrap_err());
        }

        auto identity = String::make("tenon-clang-recipe-v1\n"_str);
        auto append_identity_path = [&](rstd::ref<rstd::path::Path> value) {
            auto text = value.to_str();
            if (text.is_some()) identity.push_str(*text);
            identity.push_ascii('\n');
        };
        append_identity_path(compiler_path.as_path());
        identity.push_str(compiler_version->as_str());
        identity.push_ascii('\n');
        append_identity_path(scanner_path.as_path());
        identity.push_str(scanner_version->as_str());
        identity.push_ascii('\n');
        append_identity_path(archiver_path.as_path());
        identity.push_str(archiver_version->as_str());
        identity.push_ascii('\n');
        identity.push_str(target->as_str());
        identity.push_ascii('\n');
        auto resolved_resource = rstd::move(canonical_resource).unwrap();
        append_identity_path(resolved_resource.as_path());

        return rstd::Ok(ClangToolchain {
            rstd::move(compiler_path),
            rstd::move(scanner_path),
            rstd::move(archiver_path),
            rstd::move(resolved_resource),
            rstd::move(target).unwrap(),
            rstd::move(identity),
        });
    }

    auto identity() const -> rstd::ref<rstd::str> { return identity_.as_str(); }
    auto target() const -> rstd::ref<rstd::str> { return target_.as_str(); }
    auto resource_dir() const -> rstd::ref<rstd::path::Path> { return resource_dir_.as_path(); }

    auto prepare(UnitSpec unit, rstd::ref<rstd::path::Path> working_directory) const
        -> Result<PreparedUnit> {
        auto object_parent = clang_detail::create_parent(unit.object.as_path());
        if (object_parent.is_err()) return rstd::Err(rstd::move(object_parent).unwrap_err());
        auto dep_parent = clang_detail::create_parent(unit.depfile.as_path());
        if (dep_parent.is_err()) return rstd::Err(rstd::move(dep_parent).unwrap_err());
        return rstd::Ok(PreparedUnit {
            .unit = rstd::move(unit),
            .working_directory = PathBuf::from(working_directory),
        });
    }

    auto scan(const PreparedUnit& prepared) const -> Result<ScanResult> {
        using namespace rstd::literals;

        auto command = Vec<String>::make();
        auto pushed = clang_detail::push_path(command, scanner_.as_path());
        if (pushed.is_err()) return rstd::Err(rstd::move(pushed).unwrap_err());
        command.push(String::make("-format=p1689"_str));
        command.push(String::make("--"_str));
        auto context = append_compile_context(command, *prepared.unit.context);
        if (context.is_err()) return rstd::Err(rstd::move(context).unwrap_err());
        command.push(String::make("-MD"_str));
        command.push(String::make("-MT"_str));
        pushed = clang_detail::push_path(command, prepared.unit.object.as_path());
        if (pushed.is_err()) return rstd::Err(rstd::move(pushed).unwrap_err());
        command.push(String::make("-MF"_str));
        pushed = clang_detail::push_path(command, prepared.unit.depfile.as_path());
        if (pushed.is_err()) return rstd::Err(rstd::move(pushed).unwrap_err());
        command.push(String::make("-c"_str));
        pushed = clang_detail::push_path(command, prepared.unit.source.as_path());
        if (pushed.is_err()) return rstd::Err(rstd::move(pushed).unwrap_err());
        command.push(String::make("-o"_str));
        pushed = clang_detail::push_path(command, prepared.unit.object.as_path());
        if (pushed.is_err()) return rstd::Err(rstd::move(pushed).unwrap_err());

        auto output = run_command(command, rstd::Some(prepared.working_directory.as_path()));
        if (output.is_err()) return rstd::Err(rstd::move(output).unwrap_err());
        auto command_output = rstd::move(output).unwrap();
        if (command_output.exit_code != rstd::i32 {}) {
            return clang_detail::failure<ScanResult>(rstd::format(
                "clang-scan-deps failed for '{}'\n{}\n{}",
                prepared.unit.source.as_path(),
                command_text(command).as_str(),
                command_output.standard_error.as_str()));
        }
        auto scan_result = clang_detail::parse_scan_json(
            command_output.standard_output.as_str(), prepared.unit.id);
        if (scan_result.is_err()) return scan_result;
        auto headers = clang_detail::parse_depfile(
            prepared.unit.depfile.as_path(), prepared.working_directory.as_path());
        if (headers.is_err()) return rstd::Err(rstd::move(headers).unwrap_err());
        scan_result->header_inputs = rstd::move(headers).unwrap();
        return scan_result;
    }

    auto compile(const PreparedUnit& prepared,
                 const ScanResult& scan_result,
                 const Vec<ResolvedModuleArtifact>& module_inputs) const -> Result<rstd::empty> {
        using namespace rstd::literals;

        auto command = Vec<String>::make();
        auto context = append_compile_context(command, *prepared.unit.context);
        if (context.is_err()) return context;
        if (scan_result.provided.is_some()) {
            if (prepared.unit.bmi.is_none()) {
                return clang_detail::failure<rstd::empty>(rstd::format(
                    "module unit has no BMI output: {}", prepared.unit.source.as_path()));
            }
            auto parent = clang_detail::create_parent((*prepared.unit.bmi).as_path());
            if (parent.is_err()) return parent;
            command.push(String::make("-x"_str));
            command.push(String::make("c++-module"_str));
            command.push(rstd::format("-fmodule-output={}", (*prepared.unit.bmi).as_path()));
        }
        for (const auto& input : module_inputs) {
            command.push(rstd::format(
                "-fmodule-file={}={}", input.logical_name.as_str(), input.bmi.as_path()));
        }
        command.push(String::make("-c"_str));
        auto pushed = clang_detail::push_path(command, prepared.unit.source.as_path());
        if (pushed.is_err()) return rstd::Err(rstd::move(pushed).unwrap_err());
        command.push(String::make("-o"_str));
        pushed = clang_detail::push_path(command, prepared.unit.object.as_path());
        if (pushed.is_err()) return rstd::Err(rstd::move(pushed).unwrap_err());

        auto output = run_command(command, rstd::Some(prepared.working_directory.as_path()));
        if (output.is_err()) return rstd::Err(rstd::move(output).unwrap_err());
        auto command_output = rstd::move(output).unwrap();
        if (command_output.exit_code != rstd::i32 {}) {
            return clang_detail::failure<rstd::empty>(rstd::format(
                "clang++ failed for '{}'\n{}\n{}",
                prepared.unit.source.as_path(),
                command_text(command).as_str(),
                command_output.standard_error.as_str()));
        }
        return rstd::Ok(rstd::empty {});
    }

    auto archive(rstd::ref<rstd::path::Path> output_path,
                 const Vec<PathBuf>& objects,
                 rstd::ref<rstd::path::Path> working_directory) const -> Result<rstd::empty> {
        using namespace rstd::literals;

        auto parent = clang_detail::create_parent(output_path);
        if (parent.is_err()) return parent;
        auto archive_exists = rstd::fs::exists(output_path);
        if (archive_exists.is_err()) {
            return clang_detail::failure<rstd::empty>(rstd::format(
                "cannot inspect archive '{}': {}",
                output_path,
                rstd::move(archive_exists).unwrap_err()));
        }
        if (*archive_exists) {
            auto removed = rstd::fs::remove_file(output_path);
            if (removed.is_err()) {
                return clang_detail::failure<rstd::empty>(rstd::format(
                    "cannot replace archive '{}': {}",
                    output_path,
                    rstd::move(removed).unwrap_err()));
            }
        }
        auto command = Vec<String>::make();
        auto pushed = clang_detail::push_path(command, archiver_.as_path());
        if (pushed.is_err()) return rstd::Err(rstd::move(pushed).unwrap_err());
        command.push(String::make("rcs"_str));
        pushed = clang_detail::push_path(command, output_path);
        if (pushed.is_err()) return rstd::Err(rstd::move(pushed).unwrap_err());
        for (const auto& object : objects) {
            pushed = clang_detail::push_path(command, object.as_path());
            if (pushed.is_err()) return rstd::Err(rstd::move(pushed).unwrap_err());
        }
        auto output = run_command(command, rstd::Some(working_directory));
        if (output.is_err()) return rstd::Err(rstd::move(output).unwrap_err());
        auto command_output = rstd::move(output).unwrap();
        if (command_output.exit_code != rstd::i32 {}) {
            return clang_detail::failure<rstd::empty>(rstd::format(
                "llvm-ar failed for '{}'\n{}\n{}",
                output_path,
                command_text(command).as_str(),
                command_output.standard_error.as_str()));
        }
        return rstd::Ok(rstd::empty {});
    }

private:
    ClangToolchain(PathBuf compiler,
                   PathBuf scanner,
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
        -> Result<rstd::empty> {
        using namespace rstd::literals;

        auto pushed = clang_detail::push_path(command, compiler_.as_path());
        if (pushed.is_err()) return pushed;
        command.push(String::make("-resource-dir"_str));
        pushed = clang_detail::push_path(command, resource_dir_.as_path());
        if (pushed.is_err()) return pushed;
        command.push(rstd::format("-std={}", context.language_standard.as_str()));
        command.push(clang_detail::standard_library_flag(context.standard_library));
        command.push(clang_detail::bmi_flag(context.bmi_mode));
        for (const auto& option : context.options) command.push(option.clone());
        command.push(String::make("-fno-rtti"_str));
        command.push(String::make("-fno-exceptions"_str));
        for (const auto& definition : context.definitions) {
            command.push(rstd::format("-D{}", definition.as_str()));
        }
        for (const auto& include : context.include_directories) {
            command.push(String::make("-I"_str));
            pushed = clang_detail::push_path(command, include.as_path());
            if (pushed.is_err()) return pushed;
        }
        return rstd::Ok(rstd::empty {});
    }

    PathBuf compiler_;
    PathBuf scanner_;
    PathBuf archiver_;
    PathBuf resource_dir_;
    String  target_;
    String  identity_;
};

} // namespace tenon
