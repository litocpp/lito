export module tenon.dependency;

import rstd;
import tenon.model;
import tenon.process;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace tenon
{

template<typename T>
auto dependency_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Dependency, rstd::move(message)));
}

template<typename T>
auto dependency_failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Dependency, message));
}

auto version_operator(PkgConfigVersionOperator value) noexcept -> ref<str> {
    switch (value) {
    case PkgConfigVersionOperator::Equal: return "="_str;
    case PkgConfigVersionOperator::Less: return "<"_str;
    case PkgConfigVersionOperator::Greater: return ">"_str;
    case PkgConfigVersionOperator::LessEqual: return "<="_str;
    case PkgConfigVersionOperator::GreaterEqual: return ">="_str;
    }
    return "="_str;
}

auto module_spec(const PkgConfigDependencyRequirement& requirement) -> String {
    auto result = requirement.module.clone();
    if (requirement.version.is_some()) {
        result.push_ascii(u8(' '));
        result.push_str(version_operator(requirement.version->comparison));
        result.push_ascii(u8(' '));
        result.push_str(requirement.version->value.as_str());
    }
    return result;
}

auto path_list(const Vec<PathBuf>& paths, const TargetInfo& target) -> Result<String> {
    auto result    = String::make();
    auto separator = target.family == TargetFamily::Windows ? u8(';') : u8(':');
    for (const auto& path : paths) {
        auto text = path.as_path().to_str();
        if (text.is_none()) {
            return dependency_failure<String>(
                rstd::format("pkg-config path '{}' is not valid UTF-8", path.as_path()));
        }
        if (! result.is_empty()) result.push_ascii(separator);
        result.push_str(*text);
    }
    return Ok(rstd::move(result));
}

auto provider_environment(const PkgConfigProviderConfig& config, const TargetInfo& target)
    -> Result<CommandEnvironment> {
    auto result = CommandEnvironment {};
    if (! config.search_paths.is_empty()) {
        auto value = path_list(config.search_paths, target);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err());
        result.entries.push(CommandEnvironmentEntry {
            .key   = String::make("PKG_CONFIG_PATH"_str),
            .value = Some(rstd::move(value).unwrap()),
        });
    }
    if (! config.library_paths.is_empty()) {
        auto value = path_list(config.library_paths, target);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err());
        result.entries.push(CommandEnvironmentEntry {
            .key   = String::make("PKG_CONFIG_LIBDIR"_str),
            .value = Some(rstd::move(value).unwrap()),
        });
    }
    if (config.sysroot.is_some()) {
        auto text = config.sysroot->as_path().to_str();
        if (text.is_none()) {
            return dependency_failure<CommandEnvironment>(rstd::format(
                "pkg-config sysroot '{}' is not valid UTF-8", config.sysroot->as_path()));
        }
        result.entries.push(CommandEnvironmentEntry {
            .key   = String::make("PKG_CONFIG_SYSROOT_DIR"_str),
            .value = Some(String::make(*text)),
        });
    }
    return Ok(rstd::move(result));
}

auto push_word(Vec<String>& output, Vec<u8>& current) -> Result<empty> {
    auto word = String::from_utf8(rstd::move(current));
    if (word.is_err()) {
        return dependency_failure<empty>("pkg-config output contains invalid UTF-8"_str);
    }
    output.push(rstd::move(word).unwrap());
    current = Vec<u8>::make();
    return Ok(empty {});
}

enum class FragmentQuote
{
    None,
    Single,
    Double,
};

struct PkgConfigSnapshot {
    String               module;
    String               version;
    CppArgumentLayer     compile_arguments;
    LinkArgumentSequence link_arguments;
    String               identity;

    auto clone() const -> PkgConfigSnapshot {
        return PkgConfigSnapshot {
            .module            = module.clone(),
            .version           = version.clone(),
            .compile_arguments = as<rstd::clone::Clone>(compile_arguments).clone(),
            .link_arguments    = link_arguments.clone(),
            .identity          = identity.clone(),
        };
    }
};

auto query_pkg_config(const PkgConfigProviderConfig&        config,
                      const PkgConfigDependencyRequirement& requirement,
                      ref<str>                              alias,
                      ref<str>                              query,
                      const CommandEnvironment&             environment) -> Result<String> {
    auto executable = config.executable.as_path().to_str();
    if (executable.is_none()) {
        return dependency_failure<String>(rstd::format(
            "pkg-config executable '{}' is not valid UTF-8", config.executable.as_path()));
    }
    auto arguments = Vec<String>::make();
    arguments.push(String::make(*executable));
    if (requirement.mode == PkgConfigQueryMode::Static) {
        arguments.push(String::make("--static"_str));
    }
    arguments.push(String::make("--print-errors"_str));
    arguments.push(rstd::format("--{}", query));
    arguments.push(module_spec(requirement));
    auto output =
        run_command(arguments,
                    None(),
                    Some(ref<CommandEnvironment>::from_raw_parts(rstd::addressof(environment))));
    if (output.is_err()) {
        return dependency_failure<String>(
            rstd::format("pkg-config dependency '{}' module '{}' {} query could not execute: {}",
                         alias,
                         requirement.module.as_str(),
                         query,
                         rstd::move(output).unwrap_err().message.as_str()));
    }
    auto value = rstd::move(output).unwrap();
    if (value.exit_code != i32 {}) {
        return dependency_failure<String>(rstd::format(
            "pkg-config dependency '{}' module '{}' {} query failed with exit code {}: {}",
            alias,
            requirement.module.as_str(),
            query,
            value.exit_code,
            value.standard_error.as_str()));
    }
    return Ok(rstd::move(value.standard_output));
}

auto provider_version(const PkgConfigProviderConfig&    config,
                      const CommandEnvironment&         environment,
                      const DeclaredExternalDependency& declaration) -> Result<String> {
    auto executable = config.executable.as_path().to_str();
    if (executable.is_none()) {
        return dependency_failure<String>(rstd::format(
            "pkg-config dependency '{}' module '{}' has a provider path that is not valid UTF-8",
            declaration.alias.as_str(),
            declaration.requirement.module.as_str()));
    }
    auto arguments = Vec<String>::make();
    arguments.push(String::make(*executable));
    arguments.push(String::make("--version"_str));
    auto output =
        run_command(arguments,
                    None(),
                    Some(ref<CommandEnvironment>::from_raw_parts(rstd::addressof(environment))));
    if (output.is_err()) {
        return dependency_failure<String>(
            rstd::format("pkg-config dependency '{}' module '{}' cannot execute provider '{}': {}",
                         declaration.alias.as_str(),
                         declaration.requirement.module.as_str(),
                         *executable,
                         rstd::move(output).unwrap_err().message.as_str()));
    }
    auto value = rstd::move(output).unwrap();
    if (value.exit_code != i32 {}) {
        return dependency_failure<String>(rstd::format(
            "pkg-config dependency '{}' module '{}' provider '{}' failed with exit code {}: {}",
            declaration.alias.as_str(),
            declaration.requirement.module.as_str(),
            *executable,
            value.exit_code,
            value.standard_error.as_str()));
    }
    auto version = String::make(value.standard_output.as_str().trim_ascii());
    if (version.is_empty()) {
        return dependency_failure<String>(rstd::format(
            "pkg-config dependency '{}' module '{}' provider returned an empty version",
            declaration.alias.as_str(),
            declaration.requirement.module.as_str()));
    }
    return Ok(rstd::move(version));
}

auto append_identity_value(String& output, ref<str> value) -> void {
    output.push_str(rstd::format("{}:{}\n", value.len(), value).as_str());
}

auto provider_identity(const PkgConfigProviderConfig& config,
                       ref<str>                       effective_target,
                       ref<str>                       version) -> Result<String> {
    auto executable = config.executable.as_path().to_str();
    if (executable.is_none()) {
        return dependency_failure<String>("pkg-config executable path is not valid UTF-8"_str);
    }
    auto result = String::make("tenon-pkg-config-provider-v1\n"_str);
    append_identity_value(result, *executable);
    append_identity_value(result, version);
    append_identity_value(result, effective_target);
    for (const auto& path : config.search_paths) {
        auto text = path.as_path().to_str();
        if (text.is_none()) return dependency_failure<String>("pkg-config path is not UTF-8"_str);
        append_identity_value(result, *text);
    }
    for (const auto& path : config.library_paths) {
        auto text = path.as_path().to_str();
        if (text.is_none()) return dependency_failure<String>("pkg-config path is not UTF-8"_str);
        append_identity_value(result, *text);
    }
    if (config.sysroot.is_some()) {
        auto text = config.sysroot->as_path().to_str();
        if (text.is_none())
            return dependency_failure<String>("pkg-config sysroot is not UTF-8"_str);
        append_identity_value(result, *text);
    }
    return Ok(rstd::move(result));
}

auto snapshot_identity(ref<str>                              provider,
                       const PkgConfigDependencyRequirement& requirement,
                       ref<str>                              version,
                       const Vec<String>&                    cflags,
                       const Vec<String>&                    libs) -> String {
    auto result = String::make("tenon-external-dependency-v1\n"_str);
    append_identity_value(result, provider);
    append_identity_value(result, module_spec(requirement).as_str());
    append_identity_value(result, version);
    append_identity_value(
        result, requirement.mode == PkgConfigQueryMode::Static ? "static"_str : "shared"_str);
    for (const auto& value : cflags) append_identity_value(result, value.as_str());
    for (const auto& value : libs) append_identity_value(result, value.as_str());
    return result;
}

} // namespace tenon

export namespace tenon
{

auto tokenize_pkg_config_fragments(ref<str> input) -> Result<Vec<String>> {
    auto result      = Vec<String>::make();
    auto current     = Vec<u8>::make();
    auto quote       = FragmentQuote::None;
    auto escaping    = false;
    auto word_active = false;
    for (auto byte : input.as_bytes()) {
        if (byte == u8()) {
            return dependency_failure<Vec<String>>("pkg-config output contains NUL"_str);
        }
        if (escaping) {
            if (quote == FragmentQuote::Double && byte != u8('"') && byte != u8('\\') &&
                byte != u8('$') && byte != u8('`') && byte != u8('\n')) {
                current.emplace_back(u8('\\'));
            }
            if (quote == FragmentQuote::Double && byte == u8('\n')) {
                escaping = false;
                continue;
            }
            current.emplace_back(byte);
            escaping    = false;
            word_active = true;
            continue;
        }
        if (quote == FragmentQuote::Single) {
            if (byte == u8('\''))
                quote = FragmentQuote::None;
            else
                current.emplace_back(byte);
            word_active = true;
            continue;
        }
        if (quote == FragmentQuote::Double) {
            if (byte == u8('"')) {
                quote = FragmentQuote::None;
            } else if (byte == u8('\\')) {
                escaping = true;
            } else {
                current.emplace_back(byte);
            }
            word_active = true;
            continue;
        }
        if (byte == u8('\\')) {
            escaping    = true;
            word_active = true;
        } else if (byte == u8('\'')) {
            quote       = FragmentQuote::Single;
            word_active = true;
        } else if (byte == u8('"')) {
            quote       = FragmentQuote::Double;
            word_active = true;
        } else if (byte == u8(' ') || byte == u8('\t') || byte == u8('\n') || byte == u8('\r')) {
            if (word_active) {
                auto pushed = push_word(result, current);
                if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
                word_active = false;
            }
        } else {
            current.emplace_back(byte);
            word_active = true;
        }
    }
    if (escaping) {
        return dependency_failure<Vec<String>>("pkg-config output ends with an escape"_str);
    }
    if (quote != FragmentQuote::None) {
        return dependency_failure<Vec<String>>("pkg-config output contains an unclosed quote"_str);
    }
    if (word_active) {
        auto pushed = push_word(result, current);
        if (pushed.is_err()) return Err(rstd::move(pushed).unwrap_err());
    }
    return Ok(rstd::move(result));
}

auto resolve_external_dependencies(const Vec<DeclaredExternalDependency>& declarations,
                                   const PkgConfigProviderConfig&         config,
                                   const TargetInfo&                      default_target,
                                   ref<str>                               effective_target,
                                   const CppArgumentParser&               parser)
    -> Result<Vec<ResolvedExternalDependency>> {
    auto result = Vec<ResolvedExternalDependency>::with_capacity(declarations.len());
    if (declarations.is_empty()) return Ok(rstd::move(result));
    if (effective_target != default_target.triple.as_str() && ! config.target_configured) {
        return dependency_failure<Vec<ResolvedExternalDependency>>(
            rstd::format("target '{}' requires explicit pkg-config executable, library-path, or "
                         "sysroot configuration",
                         effective_target));
    }
    auto environment = provider_environment(config, default_target);
    if (environment.is_err()) return Err(rstd::move(environment).unwrap_err());
    auto provider = provider_version(config, *environment, declarations[usize {}]);
    if (provider.is_err()) return Err(rstd::move(provider).unwrap_err());
    auto provider_id = provider_identity(config, effective_target, provider->as_str());
    if (provider_id.is_err()) return Err(rstd::move(provider_id).unwrap_err());
    auto snapshots = rstd::collections::BTreeMap<String, PkgConfigSnapshot>::make();
    for (const auto& declaration : declarations) {
        auto key = provider_id->clone();
        key.push_str(module_spec(declaration.requirement).as_str());
        key.push_str(declaration.requirement.mode == PkgConfigQueryMode::Static ? "\nstatic"_str
                                                                                : "\nshared"_str);
        auto cached   = snapshots.get(key.as_str());
        auto snapshot = PkgConfigSnapshot {};
        if (cached.is_some()) {
            snapshot = (**cached).clone();
        } else {
            auto version = query_pkg_config(config,
                                            declaration.requirement,
                                            declaration.alias.as_str(),
                                            "modversion"_str,
                                            *environment);
            if (version.is_err()) return Err(rstd::move(version).unwrap_err());
            auto normalized_version = String::make(version->as_str().trim_ascii());
            if (normalized_version.is_empty()) {
                return dependency_failure<Vec<ResolvedExternalDependency>>(
                    rstd::format("pkg-config dependency '{}' module '{}' returned an empty version",
                                 declaration.alias.as_str(),
                                 declaration.requirement.module.as_str()));
            }
            auto cflags = query_pkg_config(config,
                                           declaration.requirement,
                                           declaration.alias.as_str(),
                                           "cflags"_str,
                                           *environment);
            if (cflags.is_err()) return Err(rstd::move(cflags).unwrap_err());
            auto libs = query_pkg_config(config,
                                         declaration.requirement,
                                         declaration.alias.as_str(),
                                         "libs"_str,
                                         *environment);
            if (libs.is_err()) return Err(rstd::move(libs).unwrap_err());
            auto compile_tokens = tokenize_pkg_config_fragments(cflags->as_str());
            if (compile_tokens.is_err()) return Err(rstd::move(compile_tokens).unwrap_err());
            auto link_tokens = tokenize_pkg_config_fragments(libs->as_str());
            if (link_tokens.is_err()) return Err(rstd::move(link_tokens).unwrap_err());
            auto source  = rstd::format("pkg-config dependency '{}' module '{}'",
                                        declaration.alias.as_str(),
                                        declaration.requirement.module.as_str());
            auto compile = parser.parse(*compile_tokens, source.as_str());
            if (compile.is_err()) {
                return dependency_failure<Vec<ResolvedExternalDependency>>(
                    rstd::format("{} has invalid Cflags: {}",
                                 source.as_str(),
                                 rstd::move(compile).unwrap_err()));
            }
            auto identity = snapshot_identity(provider_id->as_str(),
                                              declaration.requirement,
                                              normalized_version.as_str(),
                                              *compile_tokens,
                                              *link_tokens);
            snapshot      = PkgConfigSnapshot {
                .module            = declaration.requirement.module.clone(),
                .version           = rstd::move(normalized_version),
                .compile_arguments = rstd::move(compile).unwrap(),
                .link_arguments =
                    LinkArgumentSequence {
                        .tokens   = rstd::move(link_tokens).unwrap(),
                        .source   = source.clone(),
                        .identity = identity.clone(),
                    },
                .identity = rstd::move(identity),
            };
            snapshots.insert(rstd::move(key), snapshot.clone());
        }
        result.push(ResolvedExternalDependency {
            .alias             = declaration.alias.clone(),
            .provider          = String::make("pkg-config"_str),
            .module            = snapshot.module.clone(),
            .version           = snapshot.version.clone(),
            .visibility        = declaration.visibility,
            .compile_arguments = as<rstd::clone::Clone>(snapshot.compile_arguments).clone(),
            .link_arguments    = snapshot.link_arguments.clone(),
            .identity          = snapshot.identity.clone(),
        });
    }
    return Ok(rstd::move(result));
}

} // namespace tenon
