module;
#include <rstd/macro.hpp>

export module lito.cpp:package.plan;

import rstd;
import lito.crypto;
import lito.core;
import :bmi;
import :build.plan;
import :compiler.option;
import :package.metadata;
import :package.spec;
import :package.target;
import :usage;
import :c.compiler;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito::cpp
{

template<typename T>
auto plan_failure(String message) -> lito::package::PackageResult<T> {
    return Err(lito::package::PackageError::Message(rstd::move(message)));
}

template<typename T>
auto plan_failure(ref<str> message) -> lito::package::PackageResult<T> {
    return Err(lito::package::PackageError::Message(String::make(message)));
}

auto append_unique(Vec<String>& output, const Vec<String>& input) -> void {
    for (const auto& value : input) {
        auto present = false;
        for (const auto& existing : output) {
            if (existing == value.as_str()) {
                present = true;
                break;
            }
        }
        if (! present) output.push(value.clone());
    }
}

auto append_unique(Vec<String>& output, ref<str> input) -> void {
    for (const auto& existing : output) {
        if (existing.as_str() == input) return;
    }
    output.push(String::make(input));
}

auto append_all(Vec<String>& output, const Vec<String>& input) -> void {
    for (const auto& value : input) output.push(value.clone());
}

auto append_unique(Vec<PathBuf>& output, const Vec<PathBuf>& input) -> void {
    for (const auto& value : input) {
        auto present = false;
        for (const auto& existing : output) {
            if (existing.as_path() == value.as_path()) {
                present = true;
                break;
            }
        }
        if (! present) output.push(value.clone());
    }
}

auto same_tokens(const Vec<String>& left, const Vec<String>& right) -> bool {
    if (left.len() != right.len()) return false;
    for (auto index = usize {}; index < left.len(); ++index) {
        if (left[index] != right[index].as_str()) return false;
    }
    return true;
}

auto append_unique(CppArgumentLayer& output, const CppArgumentLayer& input) -> void {
    for (const auto& occurrence : input.occurrences) {
        auto present = false;
        for (const auto& existing : output.occurrences) {
            if (same_tokens(existing.raw_tokens, occurrence.raw_tokens)) {
                present = true;
                break;
            }
        }
        if (! present) {
            output.occurrences.push(as<Clone>(occurrence).clone());
        }
    }
}

auto append_unique(lito::c::CArgumentLayer& output, const lito::c::CArgumentLayer& input) -> void {
    for (const auto& occurrence : input.occurrences) {
        auto present = false;
        for (const auto& existing : output.occurrences) {
            if (same_tokens(existing.raw_tokens, occurrence.raw_tokens)) {
                present = true;
                break;
            }
        }
        if (! present) output.occurrences.push(occurrence.clone());
    }
}

auto append_language_arguments(LanguageArgumentLayer&       output,
                               const LanguageArgumentLayer& input,
                               ref<str>                     consumer,
                               ref<str> provider) -> lito::package::PackageResult<empty> {
    if (output.is_C()) {
        if (! input.is_C()) {
            return plan_failure<empty>(
                rstd::format("C target '{}' cannot consume C++ compiler arguments from '{}'",
                             consumer,
                             provider));
        }
        append_unique(output.as_C().layer, input.as_C().layer);
        return Ok(empty {});
    }
    if (input.is_Cpp()) {
        append_unique(output.as_Cpp().layer, input.as_Cpp().layer);
        return Ok(empty {});
    }
    for (const auto& occurrence : input.as_C().layer.occurrences) {
        if (! occurrence.argument.is_Common()) {
            return plan_failure<empty>(rstd::format(
                "C++ target '{}' cannot consume language-specific C compiler arguments from '{}'",
                consumer,
                provider));
        }
        output.as_Cpp().layer.occurrences.push(CppCompilerArgumentOccurrence {
            .argument = CppCompilerArgument::Common(
                as<Clone>(occurrence.argument.as_Common().argument).clone()),
            .raw_tokens = as<Clone>(occurrence.raw_tokens).clone(),
            .range      = occurrence.range,
            .source     = occurrence.source.clone(),
        });
    }
    return Ok(empty {});
}

auto append_unique(lito::link::Requirements& output, const lito::link::Requirements& input)
    -> void {
    lito::link::append_requirements(output, input);
}

auto append_unique(lito::link::Requirements& output, const ResolvedExternalDependency& input)
    -> void {
    append_unique(output, input.link_requirements);
    for (const auto& target : input.targets) {
        if (target.compile_arguments.is_C()) {
            for (const auto& occurrence : target.compile_arguments.as_C().layer.occurrences) {
                if (! occurrence.argument.is_Common() ||
                    ! occurrence.argument.as_Common().argument.is_Threading())
                    continue;
                output.posix_threads = true;
                append_unique(output.thread_sources, occurrence.source.as_str());
            }
        } else {
            for (const auto& occurrence : target.compile_arguments.as_Cpp().layer.occurrences) {
                if (! occurrence.argument.is_Common() ||
                    ! occurrence.argument.as_Common().argument.is_Threading())
                    continue;
                output.posix_threads = true;
                append_unique(output.thread_sources, occurrence.source.as_str());
            }
        }
    }
}

auto append_external_link_input(Vec<PlannedLinkInput>&                      inputs,
                                Option<lito::link::RustStaticRuntimeUsage>& rust_runtime,
                                const ResolvedExternalDependency&           external,
                                ref<str> target) -> lito::package::PackageResult<empty> {
    const auto& incoming = external.link_compatibility.rust_static_runtime;
    if (incoming.is_none()) {
        if (! external.link_arguments.tokens.is_empty()) {
            inputs.push(PlannedLinkInput::External(external.link_arguments.clone()));
        }
        return Ok(empty {});
    }
    if (rust_runtime.is_some()) {
        if (rust_runtime->artifact_identity == incoming->artifact_identity.as_str()) {
            return Ok(empty {});
        }
        return plan_failure<empty>(rstd::format(
            "Rust static runtime conflict in final target '{}': {} ({}) conflicts with {} ({}); "
            "aggregate the Rust crates behind one Cargo facade staticlib",
            target,
            rust_runtime->source.as_str(),
            rust_runtime->artifact_identity.as_str(),
            incoming->source.as_str(),
            incoming->artifact_identity.as_str()));
    }
    rust_runtime = Some(incoming->clone());
    if (! external.link_arguments.tokens.is_empty()) {
        inputs.push(PlannedLinkInput::External(external.link_arguments.clone()));
    }
    return Ok(empty {});
}

auto append_unique(Vec<TargetId>& output, TargetId value) -> bool {
    for (auto existing : output) {
        if (existing == value) return false;
    }
    output.emplace_back(value);
    return true;
}

auto append_unique(Vec<TargetId>& output, const Vec<TargetId>& input) -> void {
    for (auto value : input) append_unique(output, value);
}

auto target_text(const lito::package::PackageTargetId& id) -> String {
    return rstd::format("{}::{}::{}",
                        id.package.as_str(),
                        lito::package::package_target_kind_name(id.kind),
                        id.name.as_str());
}

auto target_index(const PackageMetadata& package, const lito::package::PackageTargetId& id)
    -> Option<TargetId> {
    for (auto candidate = TargetId {}; candidate < package.targets.len(); ++candidate) {
        if (package.targets[candidate].id == id) return Some(candidate);
    }
    return None();
}

auto target_index(const PackageSpec& package, const lito::package::PackageTargetId& id)
    -> Option<TargetId> {
    for (auto candidate = TargetId {}; candidate < package.targets.len(); ++candidate) {
        if (package.targets[candidate].id == id) return Some(candidate);
    }
    return None();
}

struct PublicUsage {
    Vec<PathBuf>          include_directories;
    Vec<String>           definitions;
    LanguageArgumentLayer arguments;
    Vec<String>           external_identities;
};

auto visit_target(const PackageMetadata& package,
                  TargetId               target,
                  Vec<uint8_t>&          colors,
                  Vec<TargetId>&         target_order) -> lito::package::PackageResult<empty> {
    auto& color = colors[target];
    if (color == 2) return Ok(empty {});
    if (color == 1) {
        return plan_failure<empty>(rstd::format("target dependency cycle at '{}'",
                                                target_text(package.targets[target].id).as_str()));
    }

    color = 1;
    for (auto pass = usize {}; pass < usize(2); ++pass) {
        const auto link_only = pass == usize {};
        for (const auto& dependency : package.targets[target].dependencies) {
            if ((dependency.visibility == lito::dependency::DependencyVisibility::LinkOnly) !=
                link_only)
                continue;
            auto found = target_index(package, dependency.target);
            if (found.is_none()) {
                return plan_failure<empty>(
                    rstd::format("target '{}' depends on unknown target '{}'",
                                 target_text(package.targets[target].id).as_str(),
                                 target_text(dependency.target).as_str()));
            }
            auto nested = visit_target(package, *found, colors, target_order);
            if (nested.is_err()) return nested;
        }
    }
    color = 2;
    target_order.emplace_back(target);
    return Ok(empty {});
}

auto visit_link_target(const PackageMetadata& package,
                       TargetId               target,
                       bool                   public_interface_only,
                       Vec<uint8_t>&          colors,
                       Vec<TargetId>&         target_order) -> lito::package::PackageResult<empty> {
    auto& color = colors[target];
    if (color == 2) return Ok(empty {});
    if (color == 1) {
        return plan_failure<empty>(rstd::format("target dependency cycle at '{}'",
                                                target_text(package.targets[target].id).as_str()));
    }

    color = 1;
    for (auto pass = usize {}; pass < usize(2); ++pass) {
        const auto link_only = pass == usize {};
        for (const auto& dependency : package.targets[target].dependencies) {
            if (public_interface_only &&
                dependency.visibility != lito::dependency::DependencyVisibility::Public) {
                continue;
            }
            if ((dependency.visibility == lito::dependency::DependencyVisibility::LinkOnly) !=
                link_only) {
                continue;
            }
            auto found = target_index(package, dependency.target);
            if (found.is_none()) {
                return plan_failure<empty>(
                    rstd::format("target '{}' depends on unknown target '{}'",
                                 target_text(package.targets[target].id).as_str(),
                                 target_text(dependency.target).as_str()));
            }
            const auto nested_public_interface_only =
                package.targets[*found].artifact_kind == ArtifactKind::SharedLibrary;
            auto nested = visit_link_target(
                package, *found, nested_public_interface_only, colors, target_order);
            if (nested.is_err()) return nested;
        }
    }
    color = 2;
    target_order.emplace_back(target);
    return Ok(empty {});
}

auto external_has_public_link_usage(const ResolvedExternalDependency& dependency) -> bool {
    for (const auto& target : dependency.targets) {
        if (target.visibility == lito::dependency::DependencyVisibility::Public) return true;
    }
    return false;
}

auto resolve_import_requirements(const PackageMetadata& package,
                                 const Vec<TargetId>&   target_order,
                                 Vec<CompileContext>&   contexts)
    -> lito::package::PackageResult<empty> {
    if (contexts.len() != package.targets.len()) {
        return plan_failure<empty>("import requirement contexts do not match package targets"_str);
    }
    auto selected = Vec<bool>::with_capacity(package.targets.len());
    for (auto target = TargetId {}; target < package.targets.len(); ++target) {
        selected.emplace_back(false);
    }
    for (auto target : target_order) selected[target] = true;

    auto changed = true;
    while (changed) {
        changed = false;
        for (auto importer : target_order) {
            for (const auto& dependency : package.targets[importer].dependencies) {
                if (dependency.visibility == lito::dependency::DependencyVisibility::LinkOnly)
                    continue;
                auto provider = target_index(package, dependency.target);
                if (provider.is_none() || ! selected[*provider]) {
                    return plan_failure<empty>(rstd::format(
                        "import requirement dependency '{}' of target '{}' is not selected",
                        target_text(dependency.target).as_str(),
                        target_text(package.targets[importer].id).as_str()));
                }
                if (package.targets[importer].language == lito::manifest::PackageLanguage::C) {
                    if (package.targets[*provider].language ==
                        lito::manifest::PackageLanguage::Cpp) {
                        return plan_failure<empty>(
                            rstd::format("C target '{}' cannot import C++ target '{}'",
                                         target_text(package.targets[importer].id).as_str(),
                                         target_text(package.targets[*provider].id).as_str()));
                    }
                    continue;
                }
                if (package.targets[*provider].language == lito::manifest::PackageLanguage::C) {
                    continue;
                }
                auto& importer_context = contexts[importer].language.as_Cpp();
                auto& provider_context = contexts[*provider].language.as_Cpp();
                if (merge_cpp_import_requirements(importer_context.options,
                                                  provider_context.options)) {
                    changed = true;
                }
            }
        }
    }
    return Ok(empty {});
}

auto context_id(const CompileContext& context) -> String {
    auto result = String::make("lito-compile-context-v6\n"_str);
    if (context.language.is_C()) {
        const auto& c = context.language.as_C();
        result.push_str("language:c\n"_str);
        result.push_str(lito::c::c_compile_identity(c.options).as_str());
        for (const auto& identity : context.external_identities) {
            result.push_str(
                rstd::format("external:{}:{}\n", identity.len(), identity.as_str()).as_str());
        }
        return result;
    }
    const auto& cpp = context.language.as_Cpp();
    result.push_str("language:cpp\n"_str);
    result.push_str(bmi_representation_name(cpp.bmi.representation));
    result.push_ascii('\n');
    result.push_str(bmi_source_embedding_name(cpp.bmi.source_embedding));
    result.push_ascii('\n');
    result.push_str(cpp_compile_identity(cpp.options).as_str());
    result.push_str(cpp_public_requirements_identity(cpp.public_requirements).as_str());
    for (const auto& identity : context.external_identities) {
        result.push_str(
            rstd::format("external:{}:{}\n", identity.len(), identity.as_str()).as_str());
    }
    return result;
}

auto scan_context_id(const CompileContext& context) -> String {
    auto result = String::make("lito-scan-context-v4\n"_str);
    if (context.language.is_C()) {
        const auto& c = context.language.as_C();
        result.push_str("language:c\n"_str);
        result.push_str(lito::c::c_scan_identity(c.options).as_str());
        for (const auto& identity : context.external_identities) {
            result.push_str(
                rstd::format("external:{}:{}\n", identity.len(), identity.as_str()).as_str());
        }
        return result;
    }
    const auto& cpp = context.language.as_Cpp();
    result.push_str("language:cpp\n"_str);
    result.push_str(bmi_representation_name(cpp.bmi.representation));
    result.push_ascii('\n');
    result.push_str(bmi_source_embedding_name(cpp.bmi.source_embedding));
    result.push_ascii('\n');
    result.push_str(cpp_scan_identity(cpp.options).as_str());
    result.push_str(cpp_public_requirements_identity(cpp.public_requirements).as_str());
    for (const auto& identity : context.external_identities) {
        result.push_str(
            rstd::format("external:{}:{}\n", identity.len(), identity.as_str()).as_str());
    }
    return result;
}

auto attachment_context(const CompileContext&       library,
                        const CompileContext&       test,
                        const TestAttachmentTarget& attachment)
    -> lito::package::PackageResult<CompileContext> {
    if (! library.language.is_Cpp() || ! test.language.is_Cpp()) {
        return plan_failure<CompileContext>("test attachments currently require C++ targets"_str);
    }
    const auto& library_cpp = library.language.as_Cpp();
    const auto& test_cpp    = test.language.as_Cpp();
    if (library_cpp.bmi.representation != test_cpp.bmi.representation ||
        library_cpp.bmi.source_embedding != test_cpp.bmi.source_embedding) {
        return plan_failure<CompileContext>(
            "test attachment cannot merge different BMI requests"_str);
    }
    auto result = library.clone();
    auto merged = merge_cpp_options(rstd::move(result.language.as_Cpp().options), test_cpp.options);
    if (merged.is_err()) {
        return Err(lito::package::PackageError::Configuration(
            erase_error(rstd::move(merged).unwrap_err())));
    }
    auto& result_cpp               = result.language.as_Cpp();
    result_cpp.options             = rstd::move(merged).unwrap();
    result_cpp.public_requirements = merge_cpp_public_requirements(
        rstd::move(result_cpp.public_requirements), test_cpp.public_requirements);
    append_unique(result.external_identities, test.external_identities);
    result.id      = rstd::format("lito-test-attachment-context-v1\ntest:{}\nlibrary:{}\n{}",
                                  target_text(attachment.test_target).as_str(),
                                  target_text(attachment.library_target).as_str(),
                                  context_id(result).as_str());
    result.scan_id = scan_context_id(result);
    return Ok(rstd::move(result));
}

} // namespace lito::cpp

export namespace lito::cpp
{

auto refresh_compile_context_identity(CompileContext& context) -> void {
    context.id      = context_id(context);
    context.scan_id = scan_context_id(context);
}

auto preprocessor_projection(const CompileContext& context) -> PreprocessorProjection {
    auto       result         = PreprocessorProjection {};
    const auto append_include = [&](const auto& include) {
        auto text = include.path.as_path().to_string_lossy();
        if (include.kind == decltype(include.kind)::System)
            result.system_include_directories.push(rstd::move(text));
        else
            result.user_include_directories.push(rstd::move(text));
    };
    const auto append_macro = [&](const auto& macro) {
        if (macro.action == decltype(macro.action)::Define)
            result.definitions.push(macro.value.clone());
        else
            result.undefinitions.push(macro.value.clone());
    };
    if (context.language.is_C()) {
        for (const auto& include : context.language.as_C().options.include_directories)
            append_include(include);
        for (const auto& macro : context.language.as_C().options.macros) append_macro(macro);
    } else {
        for (const auto& include :
             context.language.as_Cpp().options.preprocessor.include_directories)
            append_include(include);
        for (const auto& macro : context.language.as_Cpp().options.preprocessor.macros)
            append_macro(macro);
    }
    auto identity = String::make("lito-preprocessor-projection-v1\n"_str);
    for (const auto& value : result.user_include_directories)
        identity.push_str(rstd::format("include:{}:{}\n", value.len(), value.as_str()).as_str());
    for (const auto& value : result.system_include_directories)
        identity.push_str(rstd::format("system:{}:{}\n", value.len(), value.as_str()).as_str());
    for (const auto& value : result.definitions)
        identity.push_str(rstd::format("define:{}:{}\n", value.len(), value.as_str()).as_str());
    for (const auto& value : result.undefinitions)
        identity.push_str(rstd::format("undefine:{}:{}\n", value.len(), value.as_str()).as_str());
    result.identity = lito::crypto::sha256_hex(identity.as_str());
    return result;
}

auto add_private_include_directory(CompileContext& context, PathBuf path) -> bool {
    if (context.language.is_C()) {
        auto& includes = context.language.as_C().options.include_directories;
        for (const auto& include : includes) {
            if (include.kind == lito::c::CIncludeDirectoryKind::User &&
                include.path.as_path() == path.as_path()) {
                return false;
            }
        }
        includes.push(lito::c::CIncludeDirectory { .path = rstd::move(path) });
    } else {
        auto& includes = context.language.as_Cpp().options.preprocessor.include_directories;
        for (const auto& include : includes) {
            if (include.kind == CppIncludeDirectoryKind::User &&
                include.path.as_path() == path.as_path()) {
                return false;
            }
        }
        includes.push(CppIncludeDirectory { .path = rstd::move(path) });
    }
    refresh_compile_context_identity(context);
    return true;
}

auto add_generated_artifact_identity(CompileContext& context, ref<str> identity) -> bool {
    for (const auto& existing : context.external_identities) {
        if (existing == identity) return false;
    }
    context.external_identities.push(String::make(identity));
    refresh_compile_context_identity(context);
    return true;
}

auto compile_test_context(const CompileContext& base, const ResolvedCompileTestCase& test)
    -> lito::package::PackageResult<CompileContext> {
    if (! base.language.is_Cpp()) {
        return plan_failure<CompileContext>("compile tests currently require C++ targets"_str);
    }
    auto context    = base.clone();
    auto layer      = CppOptionLayer {};
    layer.arguments = as<Clone>(test.arguments).clone();
    auto applied =
        apply_cpp_option_layer(rstd::move(context.language.as_Cpp().options), rstd::move(layer));
    if (applied.is_err()) {
        return Err(lito::package::PackageError::Configuration(
            erase_error(rstd::move(applied).unwrap_err())));
    }
    context.language.as_Cpp().options = rstd::move(applied).unwrap();
    context.id                        = context_id(context);
    context.scan_id                   = scan_context_id(context);
    return Ok(rstd::move(context));
}

auto resolve_source_selection(const PackageMetadata&                     package,
                              ref<str>                                   requested_profile,
                              const Vec<String>&                         requested_targets,
                              const Vec<lito::package::PackageTargetId>& exact_targets = {})
    -> lito::package::PackageResult<SourceTargetSelection> {
    auto profile_name =
        requested_profile.size() == usize {} ? package.default_profile.as_str() : requested_profile;
    auto profile = Option<usize> {};
    for (auto index = usize {}; index < package.profiles.len(); ++index) {
        if (package.profiles[index].name == profile_name) {
            profile = Some(index);
            break;
        }
    }
    if (profile.is_none()) {
        return plan_failure<SourceTargetSelection>(
            rstd::format("unknown profile '{}'", profile_name));
    }

    auto selected_identities = Vec<lito::package::PackageTargetId>::make();
    if (! requested_targets.is_empty() && ! exact_targets.is_empty()) {
        return plan_failure<SourceTargetSelection>(
            "target selectors and exact target identities cannot be combined"_str);
    }
    if (! exact_targets.is_empty()) {
        selected_identities =
            Vec<lito::package::PackageTargetId>::with_capacity(exact_targets.len());
        for (const auto& exact : exact_targets) {
            const lito::package::PackageTargetId* found = nullptr;
            for (const auto& candidate : package.available_targets) {
                if (candidate == exact) {
                    found = rstd::addressof(candidate);
                    break;
                }
            }
            if (found == nullptr) {
                return plan_failure<SourceTargetSelection>(rstd::format(
                    "unknown exact target '{}'", lito::package::package_target_id_text(exact)));
            }
            for (const auto& prior : selected_identities) {
                if (prior == *found) {
                    return plan_failure<SourceTargetSelection>(
                        rstd::format("exact target '{}' was selected more than once",
                                     lito::package::package_target_id_text(exact)));
                }
            }
            selected_identities.push(found->clone());
        }
    } else if (requested_targets.is_empty()) {
        selected_identities =
            Vec<lito::package::PackageTargetId>::with_capacity(package.default_targets.len());
        for (const auto& target : package.default_targets) {
            selected_identities.push(target.clone());
        }
    } else {
        for (const auto& requested : requested_targets) {
            auto separated = requested.as_str().split_once(":"_str);
            auto kind      = Option<lito::package::PackageTargetKind> {};
            auto name      = requested.as_str();
            if (separated.is_some()) {
                auto kind_text = separated->get<0>();
                name           = separated->get<1>();
                if (kind_text == "lib"_str)
                    kind = Some(lito::package::PackageTargetKind::Library);
                else if (kind_text == "bin"_str)
                    kind = Some(lito::package::PackageTargetKind::Binary);
                else if (kind_text == "test"_str)
                    kind = Some(lito::package::PackageTargetKind::Test);
                else if (kind_text == "bench"_str)
                    kind = Some(lito::package::PackageTargetKind::Benchmark);
                else
                    return plan_failure<SourceTargetSelection>(
                        rstd::format("target selector '{}' has unknown kind '{}'",
                                     requested.as_str(),
                                     kind_text));
                if (name.is_empty()) {
                    return plan_failure<SourceTargetSelection>(
                        rstd::format("target selector '{}' is missing a name", requested.as_str()));
                }
            }
            auto matched_packages =
                rstd::collections::BTreeMap<String, lito::package::PackageTargetKind>::make();
            auto matches = usize {};
            for (const auto& candidate : package.default_targets) {
                if (candidate.name != name || (kind.is_some() && candidate.kind != *kind)) {
                    continue;
                }
                auto prior = matched_packages.get(candidate.package.as_str());
                if (prior.is_some()) {
                    return plan_failure<SourceTargetSelection>(rstd::format(
                        "target selector '{}' is ambiguous in package '{}'; use '{}:{}'",
                        requested.as_str(),
                        candidate.package.as_str(),
                        lito::package::package_target_kind_name(candidate.kind),
                        candidate.name.as_str()));
                }
                matched_packages.insert(candidate.package.clone(), candidate.kind);
                selected_identities.push(candidate.clone());
                ++matches;
            }
            if (matches == usize {}) {
                return plan_failure<SourceTargetSelection>(
                    rstd::format("unknown target selector '{}'", requested.as_str()));
            }
        }
    }
    auto colors       = Vec<uint8_t>::with_capacity(package.targets.len());
    auto target_order = Vec<TargetId>::make();
    for (auto id = TargetId {}; id < package.targets.len(); ++id) colors.emplace_back(0);
    for (const auto& target_identity : selected_identities) {
        auto found = target_index(package, target_identity);
        if (found.is_none()) {
            return plan_failure<SourceTargetSelection>(
                rstd::format("unknown target '{}'", target_text(target_identity).as_str()));
        }
        auto visited = visit_target(package, *found, colors, target_order);
        if (visited.is_err()) return Err(rstd::move(visited).unwrap_err());
    }

    auto selected_targets = Vec<TargetId>::with_capacity(selected_identities.len());
    for (const auto& identity : selected_identities) {
        selected_targets.emplace_back(*target_index(package, identity));
    }
    return Ok(SourceTargetSelection {
        .profile          = *profile,
        .selected_targets = rstd::move(selected_targets),
        .target_order     = rstd::move(target_order),
    });
}

auto resolve_build_script_packages(const PackageMetadata&       package,
                                   const SourceTargetSelection& selection)
    -> lito::package::PackageResult<Vec<String>> {
    auto result = Vec<String>::make();
    for (auto target : selection.target_order) {
        if (target >= package.targets.len()) {
            return plan_failure<Vec<String>>("source target selection does not match package"_str);
        }
        auto name    = package.targets[target].id.package.as_str();
        auto allowed = false;
        for (const auto& candidate : package.build_scripts) {
            if (candidate.kind == BuildScriptOwnerKind::Package && candidate.package.is_some() &&
                candidate.package->as_str() == name) {
                allowed = true;
                break;
            }
        }
        if (allowed) append_unique(result, name);
    }
    return Ok(rstd::move(result));
}

auto resolve_native_targets(const PackageMetadata& package, SourceTargetSelection selection)
    -> lito::package::PackageResult<ResolvedNativeTargetPlan> {
    if (selection.profile >= package.profiles.len()) {
        return plan_failure<ResolvedNativeTargetPlan>(
            "source target selection does not match package profile"_str);
    }
    for (auto target : selection.target_order) {
        if (target >= package.targets.len()) {
            return plan_failure<ResolvedNativeTargetPlan>(
                "source target selection does not match package targets"_str);
        }
    }
    auto profile      = selection.profile;
    auto target_order = rstd::move(selection.target_order);

    auto public_usage      = Vec<Option<PublicUsage>>::with_capacity(package.targets.len());
    auto public_targets    = Vec<Vec<TargetId>>::with_capacity(package.targets.len());
    auto contexts          = Vec<CompileContext>::with_capacity(package.targets.len());
    auto visible_targets   = Vec<Vec<TargetId>>::with_capacity(package.targets.len());
    auto link_inputs       = Vec<Vec<PlannedLinkInput>>::with_capacity(package.targets.len());
    auto link_requirements = Vec<lito::link::Requirements>::with_capacity(package.targets.len());
    auto linker_options    = Vec<Vec<String>>::with_capacity(package.targets.len());
    for (auto id = TargetId {}; id < package.targets.len(); ++id) {
        public_usage.emplace_back(None());
        public_targets.emplace_back();
        contexts.emplace_back();
        visible_targets.emplace_back();
        link_inputs.emplace_back();
        link_requirements.emplace_back();
        linker_options.emplace_back();
    }

    for (auto target : target_order) {
        const auto& spec  = package.targets[target];
        auto        usage = PublicUsage {
            .arguments = empty_language_arguments(spec.language),
        };
        if (spec.id.kind == lito::package::PackageTargetKind::Library) {
            append_unique(usage.include_directories, spec.usage.public_include_directories);
            append_unique(usage.definitions, spec.usage.public_definitions);
            rstd_try(append_language_arguments(usage.arguments,
                                               spec.usage.interface_arguments,
                                               target_text(spec.id).as_str(),
                                               target_text(spec.id).as_str()));
            for (const auto& dependency : spec.external_dependencies) {
                for (const auto& target : dependency.targets) {
                    if (target.visibility != lito::dependency::DependencyVisibility::Public)
                        continue;
                    rstd_try(append_language_arguments(usage.arguments,
                                                       target.compile_arguments,
                                                       target_text(spec.id).as_str(),
                                                       target.name.as_str()));
                    append_unique(usage.external_identities, target.identity.as_str());
                }
            }
        }

        auto exported_targets = Vec<TargetId>::make();
        append_unique(exported_targets, target);
        for (const auto& dependency : spec.dependencies) {
            if (dependency.visibility != lito::dependency::DependencyVisibility::Public) continue;
            auto  dependency_id = *target_index(package, dependency.target);
            auto& nested_usage  = *public_usage[dependency_id];
            append_unique(usage.include_directories, nested_usage.include_directories);
            append_unique(usage.definitions, nested_usage.definitions);
            rstd_try(
                append_language_arguments(usage.arguments,
                                          nested_usage.arguments,
                                          target_text(spec.id).as_str(),
                                          target_text(package.targets[dependency_id].id).as_str()));
            append_unique(usage.external_identities, nested_usage.external_identities);
            append_unique(exported_targets, public_targets[dependency_id]);
        }
        public_usage[target]   = Some(rstd::move(usage));
        public_targets[target] = rstd::move(exported_targets);
    }

    for (auto target : target_order) {
        const auto& spec             = package.targets[target];
        const auto& selected_profile = package.profiles[profile];
        auto        context          = CompileContext {};
        const auto& exported_usage   = *public_usage[target];
        if (spec.language == lito::manifest::PackageLanguage::C) {
            if (! exported_usage.arguments.is_C()) {
                return plan_failure<ResolvedNativeTargetPlan>(
                    rstd::format("C target '{}' received compiler arguments for the wrong language",
                                 target_text(spec.id).as_str()));
            }
            auto public_layer = lito::c::CArgumentLayer {};
            append_unique(public_layer.include_directories, exported_usage.include_directories);
            append_unique(public_layer.definitions, exported_usage.definitions);
            append_unique(public_layer, exported_usage.arguments.as_C().layer);
            auto c_options_result =
                lito::c::apply_c_option_layer(selected_profile.c.clone(), rstd::move(public_layer));
            if (c_options_result.is_err()) {
                return Err(lito::package::PackageError::Configuration(
                    erase_error(rstd::move(c_options_result).unwrap_err())));
            }
            auto c_options    = rstd::move(c_options_result).unwrap();
            auto requirements = lito::c::c_public_requirements(c_options);
            context.language =
                LanguageCompileContext::C(rstd::move(c_options), rstd::move(requirements));
        } else {
            auto public_layer = CppOptionLayer {};
            append_unique(public_layer.include_directories, exported_usage.include_directories);
            append_unique(public_layer.definitions, exported_usage.definitions);
            if (! exported_usage.arguments.is_Cpp()) {
                return plan_failure<ResolvedNativeTargetPlan>(rstd::format(
                    "C++ target '{}' received compiler arguments for the wrong language",
                    target_text(spec.id).as_str()));
            }
            append_unique(public_layer.arguments, exported_usage.arguments.as_Cpp().layer);
            auto public_cpp = apply_cpp_option_layer(as<Clone>(selected_profile.cpp).clone(),
                                                     rstd::move(public_layer));
            if (public_cpp.is_err()) {
                return Err(lito::package::PackageError::Configuration(
                    erase_error(rstd::move(public_cpp).unwrap_err())));
            }
            auto public_requirements = cpp_public_requirements(*public_cpp);
            context.language         = LanguageCompileContext::Cpp(selected_profile.bmi,
                                                                   rstd::move(public_cpp).unwrap(),
                                                                   rstd::move(public_requirements));
        }
        append_unique(context.external_identities, exported_usage.external_identities);

        auto private_include_directories = Vec<PathBuf>::make();
        auto private_definitions         = Vec<String>::make();
        auto private_arguments           = empty_language_arguments(spec.language);
        append_unique(private_include_directories, spec.usage.private_include_directories);
        append_unique(private_definitions, spec.usage.private_definitions);
        rstd_try(append_language_arguments(private_arguments,
                                           spec.usage.arguments,
                                           target_text(spec.id).as_str(),
                                           target_text(spec.id).as_str()));
        for (const auto& dependency : spec.external_dependencies) {
            for (const auto& external_target : dependency.targets) {
                const auto consumed_publicly =
                    spec.id.kind != lito::package::PackageTargetKind::Library &&
                    external_target.visibility == lito::dependency::DependencyVisibility::Public;
                if (external_target.visibility != lito::dependency::DependencyVisibility::Private &&
                    ! consumed_publicly) {
                    continue;
                }
                rstd_try(append_language_arguments(private_arguments,
                                                   external_target.compile_arguments,
                                                   target_text(spec.id).as_str(),
                                                   external_target.name.as_str()));
                append_unique(context.external_identities, external_target.identity.as_str());
            }
        }

        auto visible = Vec<TargetId>::make();
        append_unique(visible, target);
        for (const auto& dependency : spec.dependencies) {
            if (dependency.visibility == lito::dependency::DependencyVisibility::LinkOnly) continue;
            auto dependency_id = *target_index(package, dependency.target);
            append_unique(visible, public_targets[dependency_id]);
            if (dependency.visibility == lito::dependency::DependencyVisibility::Public) continue;
            const auto& usage = *public_usage[dependency_id];
            append_unique(private_include_directories, usage.include_directories);
            append_unique(private_definitions, usage.definitions);
            rstd_try(
                append_language_arguments(private_arguments,
                                          usage.arguments,
                                          target_text(spec.id).as_str(),
                                          target_text(package.targets[dependency_id].id).as_str()));
            append_unique(context.external_identities, usage.external_identities);
        }
        if (spec.language == lito::manifest::PackageLanguage::C) {
            if (! private_arguments.is_C()) {
                return plan_failure<ResolvedNativeTargetPlan>(
                    rstd::format("C target '{}' received compiler arguments for the wrong language",
                                 target_text(spec.id).as_str()));
            }
            auto c_layer = lito::c::CArgumentLayer {
                .include_directories = rstd::move(private_include_directories),
                .definitions         = rstd::move(private_definitions),
                .occurrences         = rstd::move(private_arguments.as_C().layer.occurrences),
            };
            auto& c = context.language.as_C();
            auto  c_options =
                lito::c::apply_c_option_layer(rstd::move(c.options), rstd::move(c_layer));
            if (c_options.is_err()) {
                return Err(lito::package::PackageError::Configuration(
                    erase_error(rstd::move(c_options).unwrap_err())));
            }
            c.options = rstd::move(c_options).unwrap();
            if (spec.usage.link_requirements.posix_threads) {
                c.options.common.threading = lito::compiler::ThreadingModel::Posix;
            }
        } else {
            if (! private_arguments.is_Cpp()) {
                return plan_failure<ResolvedNativeTargetPlan>(rstd::format(
                    "C++ target '{}' received compiler arguments for the wrong language",
                    target_text(spec.id).as_str()));
            }
            auto private_layer = CppOptionLayer {
                .include_directories = rstd::move(private_include_directories),
                .definitions         = rstd::move(private_definitions),
                .arguments           = rstd::move(private_arguments.as_Cpp().layer),
            };
            auto& cpp = context.language.as_Cpp();
            auto  applied =
                apply_cpp_option_layer(rstd::move(cpp.options), rstd::move(private_layer));
            if (applied.is_err()) {
                return Err(lito::package::PackageError::Configuration(
                    erase_error(rstd::move(applied).unwrap_err())));
            }
            cpp.options = rstd::move(applied).unwrap();
        }
        contexts[target]        = rstd::move(context);
        visible_targets[target] = rstd::move(visible);
        append_all(linker_options[target], selected_profile.linker_options);
        append_all(linker_options[target], spec.usage.linker_options);
    }

    auto import_requirements = resolve_import_requirements(package, target_order, contexts);
    if (import_requirements.is_err()) {
        return Err(rstd::move(import_requirements).unwrap_err());
    }
    for (auto target : target_order) {
        contexts[target].id      = context_id(contexts[target]);
        contexts[target].scan_id = scan_context_id(contexts[target]);
    }

    for (auto target = TargetId {}; target < package.targets.len(); ++target) {
        const auto& attachment = package.targets[target].test_attachment;
        if (attachment.is_none()) continue;
        auto test_id    = target_index(package, attachment->test_target);
        auto library_id = target_index(package, attachment->library_target);
        if (test_id.is_none() || library_id.is_none()) {
            return plan_failure<ResolvedNativeTargetPlan>(
                "test attachment references an unknown target"_str);
        }
        if (! append_unique(visible_targets[target], target)) {
            return plan_failure<ResolvedNativeTargetPlan>(
                "test attachment target is repeated in the build graph"_str);
        }
        append_unique(visible_targets[target], visible_targets[*library_id]);
        append_unique(visible_targets[target], visible_targets[*test_id]);
        auto attached = attachment_context(contexts[*library_id], contexts[*test_id], *attachment);
        if (attached.is_err()) return Err(rstd::move(attached).unwrap_err());
        contexts[target] = rstd::move(attached).unwrap();
    }

    auto expanded_target_order = Vec<TargetId>::with_capacity(package.targets.len());
    for (auto selected_target : target_order) {
        for (auto candidate = TargetId {}; candidate < package.targets.len(); ++candidate) {
            const auto& attachment = package.targets[candidate].test_attachment;
            if (attachment.is_some() &&
                attachment->test_target == package.targets[selected_target].id) {
                expanded_target_order.emplace_back(candidate);
            }
        }
        expanded_target_order.emplace_back(selected_target);
    }
    target_order = rstd::move(expanded_target_order);

    for (auto target : target_order) {
        auto colors = Vec<uint8_t>::with_capacity(package.targets.len());
        for (auto id = TargetId {}; id < package.targets.len(); ++id) {
            colors.emplace_back(uint8_t {});
        }
        auto dependency_order = Vec<TargetId>::make();
        auto ordered          = visit_link_target(package, target, false, colors, dependency_order);
        if (ordered.is_err()) return Err(rstd::move(ordered).unwrap_err());
        auto& inputs               = link_inputs[target];
        auto& requirements         = link_requirements[target];
        auto  rust_runtime         = Option<lito::link::RustStaticRuntimeUsage> {};
        requirements.posix_threads = lito::compiler::uses_posix_threads(
            contexts[target].language.is_C() ? contexts[target].language.as_C().options.common
                                             : contexts[target].language.as_Cpp().options.common);
        append_unique(requirements,
                      contexts[target].language.is_C()
                          ? package.profiles[profile].c_link_requirements
                          : package.profiles[profile].cpp_link_requirements);
        append_unique(requirements, package.targets[target].usage.link_requirements);
        for (auto index = dependency_order.len(); index > usize {}; --index) {
            const auto candidate = dependency_order[index - usize(1)];
            if (candidate == target) continue;
            auto abi_difference = Option<CppAbiCompatibilityDifference> {};
            if (contexts[candidate].language.is_Cpp() && contexts[target].language.is_Cpp()) {
                abi_difference =
                    check_cpp_abi_compatibility(contexts[candidate].language.as_Cpp().options,
                                                contexts[target].language.as_Cpp().options);
            }
            if (abi_difference.is_some()) {
                return plan_failure<ResolvedNativeTargetPlan>(rstd::format(
                    "artifact ABI conflict in {}: target '{}' has '{}', dependency '{}' has '{}'",
                    cpp_abi_compatibility_field_name(abi_difference->field),
                    target_text(package.targets[target].id).as_str(),
                    abi_difference->consumer.as_str(),
                    target_text(package.targets[candidate].id).as_str(),
                    abi_difference->provider.as_str()));
            }
            const auto shared_boundary =
                package.targets[candidate].artifact_kind == ArtifactKind::SharedLibrary;
            if (! shared_boundary) {
                append_unique(requirements, package.targets[candidate].usage.link_requirements);
            }
            inputs.push(PlannedLinkInput::Target(candidate));
            for (const auto& external : package.targets[candidate].external_dependencies) {
                if (shared_boundary && ! external_has_public_link_usage(external)) continue;
                append_unique(requirements, external);
                rstd_try(
                    append_external_link_input(inputs,
                                               rust_runtime,
                                               external,
                                               target_text(package.targets[target].id).as_str()));
            }
        }
        for (const auto& external : package.targets[target].external_dependencies) {
            append_unique(requirements, external);
            rstd_try(append_external_link_input(
                inputs, rust_runtime, external, target_text(package.targets[target].id).as_str()));
        }
    }

    auto target_identities =
        Vec<lito::package::PackageTargetId>::with_capacity(package.targets.len());
    for (const auto& target : package.targets) {
        target_identities.push(target.id.clone());
    }
    return Ok(ResolvedNativeTargetPlan {
        .profile           = profile,
        .target_identities = rstd::move(target_identities),
        .target_order      = rstd::move(target_order),
        .contexts          = rstd::move(contexts),
        .public_targets    = rstd::move(public_targets),
        .visible_targets   = rstd::move(visible_targets),
        .link_inputs       = rstd::move(link_inputs),
        .link_requirements = rstd::move(link_requirements),
        .linker_options    = rstd::move(linker_options),
    });
}

auto resolve_native_targets(const PackageMetadata& package,
                            ref<str>               requested_profile,
                            const Vec<String>&     requested_targets)
    -> lito::package::PackageResult<ResolvedNativeTargetPlan> {
    auto selection = resolve_source_selection(package, requested_profile, requested_targets);
    if (selection.is_err()) return Err(rstd::move(selection).unwrap_err());
    return resolve_native_targets(package, rstd::move(selection).unwrap());
}

auto finalize_package_plan(const PackageSpec& package, ResolvedNativeTargetPlan discovery)
    -> lito::package::PackageResult<PackagePlan> {
    if (discovery.profile >= package.profiles.len() ||
        discovery.target_identities.len() != package.targets.len()) {
        return plan_failure<PackagePlan>(
            "source discovery plan does not match the finalized package"_str);
    }
    for (auto target = TargetId {}; target < package.targets.len(); ++target) {
        if (! (discovery.target_identities[target] == package.targets[target].id)) {
            return plan_failure<PackagePlan>(
                "source discovery target order changed during finalization"_str);
        }
    }
    return Ok(PackagePlan {
        .package           = rstd::addressof(package),
        .profile           = rstd::addressof(package.profiles[discovery.profile]),
        .target_order      = rstd::move(discovery.target_order),
        .contexts          = rstd::move(discovery.contexts),
        .public_targets    = rstd::move(discovery.public_targets),
        .visible_targets   = rstd::move(discovery.visible_targets),
        .link_inputs       = rstd::move(discovery.link_inputs),
        .link_requirements = rstd::move(discovery.link_requirements),
        .linker_options    = rstd::move(discovery.linker_options),
    });
}

} // namespace lito::cpp
