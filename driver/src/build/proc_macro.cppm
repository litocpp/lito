module;
#include <rstd/macro.hpp>

module lito.driver:build.proc_macro;

import rstd;
import licrypto;
import lito.core;
import lito.cpp;
import lito.system;
import lito.toolchain;
import lito.toolchain.common;
import :build.artifact;
import :build.error;
import :build.layout;
import :build.plugin;
import :build.compile_plan;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

template<typename T>
auto proc_macro_failure(String message) -> BuildResult<T> {
    return Err(BuildError::Message(rstd::move(message)));
}

template<typename T>
auto proc_macro_failure(ref<str> message) -> BuildResult<T> {
    return proc_macro_failure<T>(String::make(message));
}

auto create_parent(ref<rstd::path::Path> path) -> BuildResult<empty> {
    auto parent = path.parent();
    if (parent.is_none()) {
        return proc_macro_failure<empty>(
            rstd::format("proc-macro artifact '{}' has no parent directory", path));
    }
    auto created = rstd::fs::create_dir_all(*parent);
    if (created.is_err()) {
        return Err(BuildError::System(
            lito::system::SystemError::Io(String::make("create proc-macro artifact directory"_str),
                                          PathBuf::from(*parent),
                                          rstd::move(created).unwrap_err())));
    }
    return Ok(empty {});
}

auto write_source(ref<rstd::path::Path> path, ref<str> source) -> BuildResult<empty> {
    rstd_try(create_parent(path));
    auto written = rstd::fs::write_atomic(path, source.as_bytes());
    if (written.is_err()) {
        return Err(BuildError::System(
            lito::system::SystemError::Io(String::make("write proc-macro generated source"_str),
                                          PathBuf::from(path),
                                          rstd::move(written).unwrap_err())));
    }
    return Ok(empty {});
}

auto file_digest(ref<rstd::path::Path> path, ref<str> operation) -> BuildResult<String> {
    auto data = rstd::fs::read(path);
    if (data.is_err()) {
        return Err(BuildError::System(lito::system::SystemError::Io(
            String::make(operation), PathBuf::from(path), rstd::move(data).unwrap_err())));
    }
    return Ok(licrypto::sha256_hex(data->as_slice()));
}

auto module_dependencies(const Vec<cpp::PreparedUnit>& units, Option<cpp::UnitId> excluded = None())
    -> Vec<cpp::ModuleArtifactDependency> {
    auto dependencies = Vec<cpp::ModuleArtifactDependency>::make();
    for (auto unit = cpp::UnitId {}; unit < units.len(); ++unit) {
        if (excluded.is_some() && unit == *excluded) continue;
        const auto* bmi = cpp::unit_bmi(units[unit].unit);
        if (bmi == nullptr) continue;
        dependencies.push(cpp::ModuleArtifactDependency {
            .logical_name = bmi->logical_name.clone(),
            .artifact_key = cpp::BmiArtifactKey { .value = bmi->key.value.clone() },
            .path         = bmi->path.clone(),
        });
    }
    return dependencies;
}

auto append_unique_path_input(Vec<ResolvedLinkInput>& inputs,
                              ref<rstd::path::Path>   path,
                              bool                    shared = false,
                              LinkArchiveMode         mode   = LinkArchiveMode::Normal) -> void {
    for (const auto& input : inputs) {
        if (! shared && input.is_Archive() && input.as_Archive().archive.path.as_path() == path)
            return;
        if (shared && input.is_SharedLibrary() &&
            input.as_SharedLibrary().library.as_path() == path)
            return;
    }
    if (shared) {
        inputs.push(ResolvedLinkInput::SharedLibrary(PathBuf::from(path)));
    } else {
        inputs.push(ResolvedLinkInput::Archive(LinkArchive {
            .path = PathBuf::from(path),
            .mode = mode,
        }));
    }
}

auto append_link_inputs(Vec<ResolvedLinkInput>&     output,
                        cpp::TargetId               target,
                        const cpp::PackageSpec&     package,
                        const cpp::PackagePlan&     plan,
                        const Vec<Option<PathBuf>>& libraries) -> BuildResult<empty> {
    for (const auto& input : plan.link_inputs[target]) {
        if (input.is_External()) {
            output.push(ResolvedLinkInput::External(input.as_External().arguments.clone()));
            continue;
        }
        const auto dependency = input.as_Target().target;
        if (dependency >= package.targets.len() || dependency >= libraries.len() ||
            libraries[dependency].is_none()) {
            return proc_macro_failure<empty>(rstd::format(
                "proc-macro target '{}' has an unavailable host link dependency",
                lito::package::package_target_id_text(package.targets[target].id).as_str()));
        }
        const auto shared =
            package.targets[dependency].artifact_kind == cpp::ArtifactKind::SharedLibrary;
        append_unique_path_input(output, libraries[dependency]->as_path(), shared);
    }
    return Ok(empty {});
}

auto append_unique_option(Vec<String>& output, ref<str> option) -> void {
    for (const auto& existing : output) {
        if (existing.as_str() == option) return;
    }
    output.push(String::make(option));
}

auto provider_target(ref<str> package_name, const cpp::PackageSpec& package)
    -> BuildResult<cpp::TargetId> {
    auto result = Option<cpp::TargetId> {};
    for (auto target = cpp::TargetId {}; target < package.targets.len(); ++target) {
        const auto& candidate = package.targets[target];
        if (candidate.id.package != package_name ||
            candidate.artifact_kind != cpp::ArtifactKind::ProcMacroProvider)
            continue;
        if (result.is_some()) {
            return proc_macro_failure<cpp::TargetId>(rstd::format(
                "proc-macro package '{}' has more than one provider target", package_name));
        }
        result = Some(target);
    }
    if (result.is_none()) {
        return proc_macro_failure<cpp::TargetId>(
            rstd::format("proc-macro package '{}' has no built provider target", package_name));
    }
    return Ok(*result);
}

auto link_context(const cpp::BuildConfiguration&     configuration,
                  const lito::system::BuildPlatform& platform,
                  const cpp::PackagePlan&            plan) -> LinkTargetContext {
    return LinkTargetContext {
        .platform                  = platform.clone(),
        .language                  = lito::manifest::PackageLanguage::Cpp,
        .standard_library          = plan.profile->cpp.abi.standard_library,
        .standard_library_runtime  = configuration.standard_library_runtime,
        .microsoft_runtime_library = plan.profile->cpp.common.microsoft_runtime_library,
        .link_standard_library     = true,
    };
}

auto link_lto(const cpp::PackagePlan& plan) -> Option<lito::manifest::Lto> {
    return plan.profile->link_lto.is_some() ? Option<lito::manifest::Lto> {}
                                            : plan.profile->cpp.common.codegen.lto;
}

struct ProcMacroLinkPolicy {
    lito::link::Requirements requirements;
    Vec<String>              options;
};

auto provider_link_policy(const Vec<cpp::TargetId>& providers, const cpp::PackagePlan& plan)
    -> ProcMacroLinkPolicy {
    auto requirements = lito::link::Requirements {};
    auto options      = Vec<String>::make();
    for (auto provider : providers) {
        lito::link::append_requirements(requirements, plan.link_requirements[provider]);
        for (const auto& option : plan.linker_options[provider]) {
            append_unique_option(options, option.as_str());
        }
    }
    return ProcMacroLinkPolicy {
        .requirements = rstd::move(requirements),
        .options      = rstd::move(options),
    };
}

auto json_string(ref<rstd::path::Path> path) -> BuildResult<String> {
    auto text = path.to_str();
    if (text.is_none()) {
        return proc_macro_failure<String>(
            rstd::format("proc-macro source path '{}' is not valid UTF-8", path));
    }
    auto output = String::make("\""_str);
    for (auto byte : *text) {
        if (byte == u8('\\') || byte == u8('"')) output.push_ascii('\\');
        output.push_ascii(static_cast<char>(byte.to_primitive()));
    }
    output.push_ascii('"');
    return Ok(rstd::move(output));
}

auto write_overlay(ref<rstd::path::Path> overlay,
                   ref<rstd::path::Path> logical,
                   ref<rstd::path::Path> physical) -> BuildResult<empty> {
    auto logical_text  = rstd_try(json_string(logical));
    auto physical_text = rstd_try(json_string(physical));
    auto source        = rstd::format("{{\"version\":0,\"roots\":[{{\"type\":\"file\",\"name\":{},"
                                      "\"external-contents\":{}}}]}}\n",
                                      logical_text.as_str(),
                                      physical_text.as_str());
    return write_source(overlay, source.as_str());
}

auto append_expansion_trace(Vec<String>& output, ref<str> trace) -> void {
    auto remaining = trace;
    while (! remaining.is_empty()) {
        auto separated = remaining.split_once("\n"_str);
        auto identity  = separated.is_some() ? separated->template get<0>() : remaining;
        if (! identity.is_empty()) output.push(String::make(identity));
        if (separated.is_none()) break;
        remaining = separated->template get<1>();
    }
}

auto recursion_failure(ref<rstd::path::Path> source, const Vec<String>& expansions) -> String {
    auto message = rstd::format("proc-macro expansion recursion limit reached for '{}'", source);
    if (expansions.is_empty()) return message;
    message.push_str("\nmacro expansion stack:"_str);
    for (auto index = usize {}; index < expansions.len(); ++index) {
        message.push_str(rstd::format("\n  {}: {}", index + usize(1), expansions[index]).as_str());
    }
    return message;
}

auto proc_macro_transform_identity(ref<str>                       selection_identity,
                                   const BuiltProcMacroAggregate& aggregate,
                                   const ClangToolchain&          toolchain,
                                   const cpp::CompileContext&     context,
                                   ref<str>                       original_digest) -> String {
    auto producer = String::make("lito-proc-macro-transform-v5\n"_str);
    producer.push_str(
        "contract:cpp2\nspelling:attr-derive-v1\ntrace:kind-v1\nrecursion-limit:16\n"_str);
    producer.push_str(selection_identity);
    producer.push_ascii('\n');
    producer.push_str(aggregate.identity.as_str());
    producer.push_ascii('\n');
    producer.push_str(aggregate.content_identity.as_str());
    producer.push_ascii('\n');
    producer.push_str(toolchain.compiler_identity().build_identity.as_str());
    producer.push_ascii('\n');
    producer.push_str(context.scan_id.as_str());
    producer.push_ascii('\n');
    producer.push_str(original_digest);
    return licrypto::sha256_hex(producer.as_str());
}

} // namespace lito

namespace lito
{

auto transform_proc_macro_provider_sources(const BuildLayout&              layout,
                                           const ClangToolchain&           toolchain,
                                           cpp::PackageSpec&               package,
                                           const cpp::PackagePlan&         plan,
                                           const Vec<cpp::PreparedUnit>&   units,
                                           const Vec<Vec<cpp::UnitId>>&    target_units,
                                           const Vec<BuiltCompilerPlugin>& compiler_plugins)
    -> BuildResult<bool> {
    auto transformed = false;
    auto support     = rstd_try(compiler_plugin_for_package(compiler_plugins, "pmacro"_str));
    for (auto provider : plan.target_order) {
        auto& target = package.targets[provider];
        if (target.artifact_kind != cpp::ArtifactKind::ProcMacroProvider) continue;
        auto declares_support = false;
        for (const auto& dependency : target.plugin_dependencies) {
            if (dependency.package == support->target.package.as_str()) {
                declares_support = true;
                break;
            }
        }
        if (! declares_support) {
            return proc_macro_failure<bool>(rstd::format(
                "proc-macro target '{}' does not depend on compiler plugin package '{}'",
                lito::package::package_target_id_text(target.id).as_str(),
                support->target.package.as_str()));
        }
        if (target_units[provider].len() != target.sources.len()) {
            return proc_macro_failure<bool>(
                "proc-macro provider source preparation does not match target sources"_str);
        }
        for (auto index = usize {}; index < target.sources.len(); ++index) {
            auto&      source   = target.sources[index];
            const auto unit     = target_units[provider][index];
            auto       original = rstd::fs::read_to_string(source.path.as_path());
            if (original.is_err()) {
                return Err(BuildError::System(lito::system::SystemError::Io(
                    String::make("read proc-macro provider source"_str),
                    source.path.clone(),
                    rstd::move(original).unwrap_err())));
            }
            auto original_digest = licrypto::sha256_hex(original->as_str());
            auto identity        = String::make("lito-pmacro-provider-transform-v1\n"_str);
            identity.push_str(support->identity.as_str());
            identity.push_ascii('\n');
            identity.push_str(support->content_identity.as_str());
            identity.push_ascii('\n');
            identity.push_str(target.id.package.as_str());
            identity.push_ascii('\n');
            identity.push_str(toolchain.compiler_identity().build_identity.as_str());
            identity.push_ascii('\n');
            identity.push_str(plan.contexts[provider].scan_id.as_str());
            identity.push_ascii('\n');
            identity.push_str(original_digest.as_str());
            auto producer_identity = licrypto::sha256_hex(identity.as_str());
            auto output =
                layout.proc_macro_transformed_source(target.id, source.relative_path.as_path());
            if (output.is_err()) {
                return Err(rstd::into<BuildError>(rstd::move(output).unwrap_err()));
            }
            auto overlay =
                layout.proc_macro_source_overlay(target.id, source.relative_path.as_path());
            if (overlay.is_err()) {
                return Err(rstd::into<BuildError>(rstd::move(overlay).unwrap_err()));
            }
            rstd_try(create_parent(output->as_path()));
            auto arguments = Vec<String>::make();
            arguments.push(String::make("mode=define"_str));
            arguments.push(rstd::format("output={}", output->as_path()));
            arguments.push(rstd::format("provider={}", target.id.package.as_str()));
            auto dependencies = module_dependencies(units, Some(cpp::UnitId(unit)));
            auto invocation =
                toolchain.prepare_frontend_plugin(plan.contexts[provider],
                                                  source.path.as_path(),
                                                  target.root.as_path(),
                                                  dependencies,
                                                  support->plugin.as_path(),
                                                  "pmacro"_str,
                                                  arguments,
                                                  "proc-macro provider transformation"_str);
            if (invocation.is_err()) {
                return Err(rstd::into<BuildError>(rstd::move(invocation).unwrap_err()));
            }
            auto expanded = toolchain.execute_frontend_plugin(*invocation);
            if (expanded.is_err()) {
                return Err(rstd::into<BuildError>(rstd::move(expanded).unwrap_err()));
            }
            rstd_try(write_overlay(overlay->as_path(), source.path.as_path(), output->as_path()));
            source.origin_identity = producer_identity.clone();
            source.scan_artifact   = None();
            source.transformed     = Some(cpp::TransformedSource {
                .logical_path             = source.path.clone(),
                .physical_path            = rstd::move(output).unwrap(),
                .overlay                  = rstd::move(overlay).unwrap(),
                .producer_identity        = rstd::move(producer_identity),
                .original_source_identity = rstd::move(original_digest),
            });
            transformed            = true;
        }
    }
    return Ok(transformed);
}

auto select_proc_macro_sources(const cpp::PackageSpec&      package,
                               const cpp::PackagePlan&      plan,
                               const Vec<Vec<cpp::UnitId>>& target_units,
                               const Vec<cpp::ScanResult>&  scans) -> BuildResult<Vec<u8>> {
    auto selected = Vec<u8>::with_capacity(scans.len());
    for (auto unit = cpp::UnitId {}; unit < scans.len(); ++unit) selected.push(u8 {});
    for (auto target : plan.target_order) {
        const auto& target_spec = package.targets[target];
        if (target_spec.proc_macro_dependencies.is_empty()) continue;
        if (target_spec.language != lito::manifest::PackageLanguage::Cpp) {
            return proc_macro_failure<Vec<u8>>(
                rstd::format("C target '{}' cannot use proc-macro dependencies",
                             lito::package::package_target_id_text(target_spec.id).as_str()));
        }
        for (auto unit : target_units[target]) {
            if (unit >= scans.len() || ! scans[unit].language.is_Cpp()) {
                return proc_macro_failure<Vec<u8>>(
                    "proc-macro source selection does not match C++ scan results"_str);
            }
            for (const auto& attribute : scans[unit].language.as_Cpp().facts.scoped_attributes) {
                if (attribute.scope == "pmacro"_str &&
                    (attribute.name == "attr"_str || attribute.name == "derive"_str)) {
                    selected[unit] = u8(1);
                    break;
                }
            }
        }
    }
    return Ok(rstd::move(selected));
}

auto transform_proc_macro_sources(const BuildLayout&                  layout,
                                  const ClangToolchain&               toolchain,
                                  cpp::PackageSpec&                   package,
                                  const cpp::PackagePlan&             plan,
                                  const Vec<cpp::PreparedUnit>&       units,
                                  const Vec<Vec<cpp::UnitId>>&        target_units,
                                  const Vec<u8>&                      source_selection,
                                  const Vec<BuiltProcMacroAggregate>& aggregates)
    -> BuildResult<bool> {
    if (source_selection.len() != units.len()) {
        return proc_macro_failure<bool>(
            "proc-macro source selection does not match prepared units"_str);
    }
    auto transformed = false;
    for (auto target : plan.target_order) {
        auto& target_spec = package.targets[target];
        if (target_spec.proc_macro_dependencies.is_empty()) continue;
        if (target_spec.language != lito::manifest::PackageLanguage::Cpp) {
            return proc_macro_failure<bool>(
                rstd::format("C target '{}' cannot use proc-macro dependencies",
                             lito::package::package_target_id_text(target_spec.id).as_str()));
        }
        auto identity = proc_macro_aggregate_identity(target_spec.proc_macro_dependencies);
        const BuiltProcMacroAggregate* aggregate = nullptr;
        for (const auto& candidate : aggregates) {
            if (candidate.selection_identity == identity.as_str()) {
                aggregate = rstd::addressof(candidate);
                break;
            }
        }
        if (aggregate == nullptr) {
            return proc_macro_failure<bool>(
                rstd::format("target '{}' has no matching host proc-macro aggregate",
                             lito::package::package_target_id_text(target_spec.id).as_str()));
        }
        if (target_units[target].len() != target_spec.sources.len()) {
            return proc_macro_failure<bool>(
                "proc-macro source preparation does not match target sources"_str);
        }
        for (auto source_index = usize {}; source_index < target_spec.sources.len();
             ++source_index) {
            auto&      source = target_spec.sources[source_index];
            const auto unit   = target_units[target][source_index];
            if (source_selection[unit] == u8 {}) continue;
            auto original = rstd::fs::read_to_string(source.path.as_path());
            if (original.is_err()) {
                return Err(BuildError::System(
                    lito::system::SystemError::Io(String::make("read proc-macro input source"_str),
                                                  source.path.clone(),
                                                  rstd::move(original).unwrap_err())));
            }
            auto original_digest   = licrypto::sha256_hex(original->as_str());
            auto producer_identity = proc_macro_transform_identity(identity.as_str(),
                                                                   *aggregate,
                                                                   toolchain,
                                                                   plan.contexts[target],
                                                                   original_digest.as_str());
            if (source.transformed.is_some() &&
                source.transformed->original_source_identity == original_digest.as_str() &&
                source.transformed->producer_identity == producer_identity.as_str())
                continue;
            source.transformed = None();
            auto output = layout.proc_macro_transformed_source(target_spec.id,
                                                               source.relative_path.as_path());
            if (output.is_err()) {
                return Err(rstd::into<BuildError>(rstd::move(output).unwrap_err()));
            }
            auto overlay =
                layout.proc_macro_source_overlay(target_spec.id, source.relative_path.as_path());
            if (overlay.is_err()) {
                return Err(rstd::into<BuildError>(rstd::move(overlay).unwrap_err()));
            }
            auto trace =
                layout.proc_macro_expansion_trace(target_spec.id, source.relative_path.as_path());
            if (trace.is_err()) {
                return Err(rstd::into<BuildError>(rstd::move(trace).unwrap_err()));
            }
            auto status =
                layout.proc_macro_expansion_status(target_spec.id, source.relative_path.as_path());
            if (status.is_err()) {
                return Err(rstd::into<BuildError>(rstd::move(status).unwrap_err()));
            }
            rstd_try(create_parent(output->as_path()));
            auto previous        = rstd::move(original).unwrap();
            auto dependencies    = module_dependencies(units, Some(cpp::UnitId(unit)));
            auto source_overlay  = Option<PathBuf> {};
            auto expansion_stack = Vec<String>::make();
            auto stable          = false;
            for (auto iteration = usize {}; iteration < usize(16); ++iteration) {
                auto arguments = Vec<String>::make();
                arguments.push(String::make("mode=expand"_str));
                arguments.push(rstd::format("output={}", output->as_path()));
                arguments.push(rstd::format("trace={}", trace->as_path()));
                arguments.push(rstd::format("status={}", status->as_path()));
                auto invocation =
                    toolchain.prepare_frontend_plugin(plan.contexts[target],
                                                      source.path.as_path(),
                                                      target_spec.root.as_path(),
                                                      dependencies,
                                                      aggregate->plugin.as_path(),
                                                      "pmacro"_str,
                                                      arguments,
                                                      "proc-macro source expansion"_str,
                                                      source_overlay);
                if (invocation.is_err()) {
                    return Err(rstd::into<BuildError>(rstd::move(invocation).unwrap_err()));
                }
                auto expanded = toolchain.execute_frontend_plugin(*invocation);
                if (expanded.is_err()) {
                    return Err(rstd::into<BuildError>(rstd::move(expanded).unwrap_err()));
                }
                auto expansion_trace = rstd::fs::read_to_string(trace->as_path());
                if (expansion_trace.is_err()) {
                    return Err(BuildError::System(lito::system::SystemError::Io(
                        String::make("read proc-macro expansion trace"_str),
                        trace->clone(),
                        rstd::move(expansion_trace).unwrap_err())));
                }
                const auto had_expansions = ! expansion_trace->as_str().trim_ascii().is_empty();
                append_expansion_trace(expansion_stack, expansion_trace->as_str());
                auto expansion_status = rstd::fs::read_to_string(status->as_path());
                if (expansion_status.is_err()) {
                    return Err(BuildError::System(lito::system::SystemError::Io(
                        String::make("read proc-macro expansion status"_str),
                        status->clone(),
                        rstd::move(expansion_status).unwrap_err())));
                }
                const auto pending = expansion_status->as_str().trim_ascii();
                if (pending != "pending"_str && pending != "complete"_str) {
                    return proc_macro_failure<bool>(
                        "proc-macro expansion returned an invalid status"_str);
                }
                auto current = rstd::fs::read_to_string(output->as_path());
                if (current.is_err()) {
                    return Err(BuildError::System(lito::system::SystemError::Io(
                        String::make("read proc-macro transformed source"_str),
                        output->clone(),
                        rstd::move(current).unwrap_err())));
                }
                if (pending == "complete"_str ||
                    (! had_expansions && current->as_str() == previous.as_str())) {
                    stable = true;
                    break;
                }
                previous = rstd::move(current).unwrap();
                rstd_try(
                    write_overlay(overlay->as_path(), source.path.as_path(), output->as_path()));
                source_overlay = Some(overlay->clone());
            }
            if (! stable) {
                return proc_macro_failure<bool>(
                    recursion_failure(source.path.as_path(), expansion_stack));
            }
            rstd_try(write_overlay(overlay->as_path(), source.path.as_path(), output->as_path()));
            source.origin_identity = producer_identity.clone();
            source.scan_artifact   = None();
            source.transformed     = Some(cpp::TransformedSource {
                .logical_path             = source.path.clone(),
                .physical_path            = rstd::move(output).unwrap(),
                .overlay                  = rstd::move(overlay).unwrap(),
                .producer_identity        = rstd::move(producer_identity),
                .original_source_identity = rstd::move(original_digest),
            });
            transformed            = true;
        }
    }
    return Ok(transformed);
}

auto build_proc_macro_aggregates(const cpp::BuildConfiguration&        configuration,
                                 const lito::system::BuildPlatform&    platform,
                                 const BuildLayout&                    layout,
                                 const ClangToolchain&                 toolchain,
                                 const cpp::PackageSpec&               package,
                                 const cpp::PackagePlan&               plan,
                                 const Vec<Option<PathBuf>>&           libraries,
                                 const Vec<BuiltCompilerPlugin>&       compiler_plugins,
                                 const Vec<ProcMacroAggregateRequest>& requests)
    -> BuildResult<BuiltProcMacroProducts> {
    auto output = BuiltProcMacroProducts {
        .providers  = Vec<BuiltProcMacroProvider>::make(),
        .aggregates = Vec<BuiltProcMacroAggregate>::make(),
    };
    auto provider_targets = Vec<cpp::TargetId>::make();
    for (auto target : plan.target_order) {
        if (package.targets[target].artifact_kind == cpp::ArtifactKind::ProcMacroProvider)
            provider_targets.emplace_back(target);
    }
    for (const auto& request : requests) {
        for (const auto& provider : request.providers) {
            auto target  = rstd_try(provider_target(provider.package.as_str(), package));
            auto present = false;
            for (auto existing : provider_targets) {
                if (existing == target) {
                    present = true;
                    break;
                }
            }
            if (! present) provider_targets.emplace_back(target);
        }
    }
    if (provider_targets.is_empty()) {
        return Ok(rstd::move(output));
    }
    if (platform.effective_target.platform == lito::system::TargetPlatform::Macos) {
        return proc_macro_failure<BuiltProcMacroProducts>(
            "proc-macro Clang plugin linking is not yet validated on macOS"_str);
    }

    const auto* support_product =
        rstd_try(compiler_plugin_for_package(compiler_plugins, "pmacro"_str));
    auto support = Option<cpp::TargetId> {};
    for (auto target = cpp::TargetId {}; target < package.targets.len(); ++target) {
        if (package.targets[target].id == support_product->target) {
            support = Some(target);
            break;
        }
    }
    if (support.is_none()) {
        return proc_macro_failure<BuiltProcMacroProducts>(
            "pmacro compiler plugin target is missing from the host package"_str);
    }
    const auto  support_target = *support;
    const auto& context        = plan.contexts[support_target];
    if (support_target >= libraries.len() || libraries[support_target].is_none() ||
        libraries[support_target]->as_path() != support_product->support_archive.as_path()) {
        return proc_macro_failure<BuiltProcMacroProducts>(
            "pmacro compiler-support target has no host archive"_str);
    }
    auto        support_content_identity   = rstd_try(file_digest(
        support_product->support_archive.as_path(), "read pmacro compiler-support archive"_str));
    const auto& bootstrap_content_identity = support_product->content_identity;
    auto        no_objects                 = Vec<PathBuf>::make();

    struct ProviderBuildProduct {
        cpp::TargetId target {};
        PathBuf       archive;
        String        identity;
    };
    auto products = Vec<ProviderBuildProduct>::make();
    for (auto provider : provider_targets) {
        const auto& target = package.targets[provider];
        if (provider >= libraries.len() || libraries[provider].is_none()) {
            return proc_macro_failure<BuiltProcMacroProducts>(
                rstd::format("proc-macro target '{}' has no provider archive",
                             lito::package::package_target_id_text(target.id).as_str()));
        }
        auto archive = libraries[provider]->clone();
        auto identity =
            rstd_try(file_digest(archive.as_path(), "read self-contained proc-macro archive"_str));
        output.providers.push(BuiltProcMacroProvider {
            .target   = target.id.clone(),
            .archive  = archive.clone(),
            .identity = identity.clone(),
        });
        products.push(ProviderBuildProduct {
            .target   = provider,
            .archive  = rstd::move(archive),
            .identity = rstd::move(identity),
        });
    }

    for (const auto& request : requests) {
        auto aggregate_targets = Vec<cpp::TargetId>::make();
        aggregate_targets.emplace_back(support_target);
        auto inputs = Vec<ResolvedLinkInput>::make();
        append_unique_path_input(
            inputs, libraries[support_target]->as_path(), false, LinkArchiveMode::Whole);
        rstd_try(append_link_inputs(inputs, support_target, package, plan, libraries));

        auto aggregate_identity_source =
            String::make("lito-proc-macro-aggregate-product-v4\ncontract:cpp2\n"_str);
        aggregate_identity_source.push_str(request.identity.as_str());
        aggregate_identity_source.push_ascii('\n');
        aggregate_identity_source.push_str(support_content_identity.as_str());
        aggregate_identity_source.push_ascii('\n');
        aggregate_identity_source.push_str(bootstrap_content_identity.as_str());
        aggregate_identity_source.push_ascii('\n');
        aggregate_identity_source.push_str(toolchain.compiler_identity().build_identity.as_str());
        aggregate_identity_source.push_ascii('\n');
        aggregate_identity_source.push_str(context.scan_id.as_str());
        aggregate_identity_source.push_ascii('\n');
        for (const auto& binding : request.providers) {
            const auto target = rstd_try(provider_target(binding.package.as_str(), package));
            const ProviderBuildProduct* product = nullptr;
            for (const auto& candidate : products) {
                if (candidate.target == target) {
                    product = rstd::addressof(candidate);
                    break;
                }
            }
            if (product == nullptr) {
                return proc_macro_failure<BuiltProcMacroProducts>(
                    "proc-macro provider product is missing"_str);
            }
            aggregate_targets.emplace_back(target);
            append_unique_path_input(
                inputs, product->archive.as_path(), false, LinkArchiveMode::Whole);
            rstd_try(append_link_inputs(inputs, target, package, plan, libraries));
            aggregate_identity_source.push_str(product->identity.as_str());
            aggregate_identity_source.push_ascii('\n');
        }
        auto aggregate_policy = provider_link_policy(aggregate_targets, plan);
        aggregate_identity_source.push_str(
            lito::link::requirements_identity(aggregate_policy.requirements).as_str());
        aggregate_identity_source.push_ascii('\n');
        for (const auto& option : aggregate_policy.options) {
            aggregate_identity_source.push_str(
                rstd::format("{}:{}\n", option.size(), option.as_str()).as_str());
        }
        auto aggregate_identity = licrypto::sha256_hex(aggregate_identity_source.as_str());
        auto aggregate_name =
            lito::system::plugin_filename("pmacro"_str, platform.effective_target);
        auto plugin =
            layout.proc_macro_aggregate(aggregate_identity.as_str(), aggregate_name.as_str());
        rstd_try(create_parent(plugin.as_path()));
        auto aggregate_link_context   = link_context(configuration, platform, plan);
        aggregate_link_context.soname = Some(aggregate_name.clone());
        auto linked = toolchain.link_shared_library(plugin.as_path(),
                                                    no_objects,
                                                    inputs,
                                                    rstd::move(aggregate_link_context),
                                                    link_lto(plan),
                                                    aggregate_policy.requirements,
                                                    aggregate_policy.options,
                                                    package.root.as_path());
        if (linked.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(linked).unwrap_err()));
        }
        auto aggregate_content_identity =
            rstd_try(file_digest(plugin.as_path(), "read proc-macro aggregate plugin"_str));
        auto providers = Vec<ProcMacroProviderBinding>::with_capacity(request.providers.len());
        for (const auto& provider : request.providers) providers.push(provider.clone());
        output.aggregates.push(BuiltProcMacroAggregate {
            .selection_identity = request.identity.clone(),
            .identity           = rstd::move(aggregate_identity),
            .content_identity   = rstd::move(aggregate_content_identity),
            .plugin             = rstd::move(plugin),
            .providers          = rstd::move(providers),
        });
    }
    return Ok(rstd::move(output));
}

} // namespace lito
