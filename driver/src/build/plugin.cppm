module;
#include <rstd/macro.hpp>

module lito.driver:build.plugin;

import rstd;
import licrypto;
import lito.core;
import lito.cpp;
import lito.system;
import lito.toolchain;
import lito.toolchain.common;
import :build.artifact;
import :build.compile_plan;
import :build.error;
import :build.layout;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

template<typename T>
auto plugin_failure(String message) -> BuildResult<T> {
    return Err(BuildError::Message(rstd::move(message)));
}

template<typename T>
auto plugin_failure(ref<str> message) -> BuildResult<T> {
    return plugin_failure<T>(String::make(message));
}

auto plugin_file_digest(ref<rstd::path::Path> path, ref<str> operation) -> BuildResult<String> {
    auto data = rstd::fs::read(path);
    if (data.is_err()) {
        return Err(BuildError::System(lito::system::SystemError::Io(
            String::make(operation), PathBuf::from(path), rstd::move(data).unwrap_err())));
    }
    return Ok(licrypto::sha256_hex(data->as_slice()));
}

auto append_plugin_link_input(Vec<ResolvedLinkInput>& inputs,
                              ref<rstd::path::Path>   path,
                              bool                    shared,
                              LinkArchiveMode         mode = LinkArchiveMode::Normal) -> void {
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

auto compiler_plugin_link_inputs(cpp::TargetId               target,
                                 const cpp::PackageSpec&     package,
                                 const cpp::PackagePlan&     plan,
                                 const Vec<Option<PathBuf>>& libraries,
                                 const ClangSdk& sdk) -> BuildResult<Vec<ResolvedLinkInput>> {
    auto output = Vec<ResolvedLinkInput>::make();
    append_plugin_link_input(output, libraries[target]->as_path(), false, LinkArchiveMode::Whole);
    for (const auto& input : plan.link_inputs[target]) {
        if (input.is_External()) {
            output.push(ResolvedLinkInput::External(input.as_External().arguments.clone()));
            continue;
        }
        const auto dependency = input.as_Target().target;
        if (dependency >= package.targets.len() || dependency >= libraries.len() ||
            libraries[dependency].is_none()) {
            return plugin_failure<Vec<ResolvedLinkInput>>(rstd::format(
                "compiler plugin target '{}' has an unavailable host link dependency",
                lito::package::package_target_id_text(package.targets[target].id).as_str()));
        }
        const auto shared =
            package.targets[dependency].artifact_kind == cpp::ArtifactKind::SharedLibrary;
        append_plugin_link_input(output, libraries[dependency]->as_path(), shared);
    }
    if (sdk.plugin_link_library.is_some()) {
        append_plugin_link_input(output, sdk.plugin_link_library->as_path(), true);
    }
    return Ok(rstd::move(output));
}

auto compiler_plugin_link_context(const cpp::BuildConfiguration&     configuration,
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

auto compiler_plugin_link_lto(const cpp::PackagePlan& plan) -> Option<lito::manifest::Lto> {
    return plan.profile->link_lto.is_some() ? Option<lito::manifest::Lto> {}
                                            : plan.profile->cpp.common.codegen.lto;
}

auto build_compiler_plugins(const cpp::BuildConfiguration&     configuration,
                            const lito::system::BuildPlatform& platform,
                            const BuildLayout&                 layout,
                            const ClangToolchain&              toolchain,
                            const ClangSdk&                    sdk,
                            const cpp::PackageSpec&            package,
                            const cpp::PackagePlan&            plan,
                            const Vec<Option<PathBuf>>&        libraries)
    -> BuildResult<Vec<BuiltCompilerPlugin>> {
    auto result = Vec<BuiltCompilerPlugin>::make();
    for (auto target : plan.target_order) {
        const auto& spec = package.targets[target];
        if (spec.artifact_kind != cpp::ArtifactKind::CompilerPlugin) continue;
        if (target >= libraries.len() || libraries[target].is_none()) {
            return plugin_failure<Vec<BuiltCompilerPlugin>>(
                rstd::format("compiler plugin target '{}' has no support archive",
                             lito::package::package_target_id_text(spec.id).as_str()));
        }
        auto plugin_stem = spec.archive_stem.clone();
        plugin_stem.push_str("-plugin"_str);
        auto filename =
            lito::system::plugin_filename(plugin_stem.as_str(), platform.effective_target);
        auto output = layout.compiler_plugin(spec.id, filename.as_str());
        auto parent = output.as_path().parent();
        if (parent.is_none()) {
            return plugin_failure<Vec<BuiltCompilerPlugin>>(
                "compiler plugin output has no parent directory"_str);
        }
        auto created = rstd::fs::create_dir_all(*parent);
        if (created.is_err()) {
            return Err(BuildError::System(
                lito::system::SystemError::Io(String::make("create compiler plugin directory"_str),
                                              PathBuf::from(*parent),
                                              rstd::move(created).unwrap_err())));
        }
        auto inputs = rstd_try(compiler_plugin_link_inputs(target, package, plan, libraries, sdk));
        auto requirements = plan.link_requirements[target].clone();
        auto options      = plan.linker_options[target].clone();
        auto context      = compiler_plugin_link_context(configuration, platform, plan);
        context.soname    = Some(filename.clone());
        auto linked       = toolchain.link_shared_library(output.as_path(),
                                                          Vec<PathBuf>::make(),
                                                          inputs,
                                                          rstd::move(context),
                                                          compiler_plugin_link_lto(plan),
                                                          requirements,
                                                          options,
                                                          spec.root.as_path());
        if (linked.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(linked).unwrap_err()));
        }
        auto archive_identity = rstd_try(plugin_file_digest(
            libraries[target]->as_path(), "read compiler plugin support archive"_str));
        auto content_identity = rstd_try(
            plugin_file_digest(output.as_path(), "read compiler plugin shared object"_str));
        auto identity_source = String::make("lito-compiler-plugin-v2\n"_str);
        identity_source.push_str(lito::package::package_target_id_text(spec.id).as_str());
        identity_source.push_ascii('\n');
        identity_source.push_str(archive_identity.as_str());
        identity_source.push_ascii('\n');
        identity_source.push_str(toolchain.compiler_identity().build_identity.as_str());
        identity_source.push_ascii('\n');
        identity_source.push_str(plan.contexts[target].id.as_str());
        identity_source.push_ascii('\n');
        identity_source.push_str(lito::link::requirements_identity(requirements).as_str());
        identity_source.push_ascii('\n');
        for (const auto& option : options) {
            identity_source.push_str(
                rstd::format("{}:{}\n", option.size(), option.as_str()).as_str());
        }
        result.push(BuiltCompilerPlugin {
            .target           = spec.id.clone(),
            .support_archive  = libraries[target]->clone(),
            .plugin           = rstd::move(output),
            .identity         = licrypto::sha256_hex(identity_source.as_str()),
            .content_identity = rstd::move(content_identity),
        });
    }
    return Ok(rstd::move(result));
}

struct PreparedCompilerPluginSdk {
    ClangSdk                     sdk;
    Vec<cpp::ResolvedHeaderRoot> header_roots;
};

auto prepare_compiler_plugin_sdk(const cpp::PackageMetadata&       package,
                                 cpp::ResolvedNativeTargetPlan&    plan,
                                 const ClangToolchain&             toolchain,
                                 const ResolvedProcessEnvironment& environment)
    -> BuildResult<Option<PreparedCompilerPluginSdk>> {
    auto required = false;
    for (auto target : plan.target_order) {
        if (package.targets[target].artifact_kind == cpp::ArtifactKind::CompilerPlugin) {
            required = true;
            break;
        }
    }
    if (! required) return Ok(Option<PreparedCompilerPluginSdk> {});

    auto resolved = resolve_clang_sdk(toolchain.compiler_identity(), environment);
    if (resolved.is_err()) return Err(rstd::into<BuildError>(rstd::move(resolved).unwrap_err()));
    auto sdk          = rstd::move(resolved).unwrap();
    auto header_roots = Vec<cpp::ResolvedHeaderRoot>::make();
    for (auto target : plan.target_order) {
        if (package.targets[target].artifact_kind != cpp::ArtifactKind::CompilerPlugin) continue;
        auto defaults = toolchain.header_roots(plan.contexts[target],
                                               package.targets[target].source_root.as_path());
        if (defaults.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(defaults).unwrap_err()));
        }
        auto is_default = false;
        for (const auto& root : *defaults) {
            if (root.root.as_path() == sdk.include_directory.as_path()) {
                is_default = true;
                break;
            }
        }
        auto attached = attach_clang_plugin_sdk(plan.contexts[target], sdk, ! is_default);
        if (attached.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(attached).unwrap_err()));
        }
        if (! is_default) {
            header_roots.push(clang_plugin_sdk_header_root(sdk, package.targets[target].id));
        }
    }
    return Ok(Some(PreparedCompilerPluginSdk {
        .sdk          = rstd::move(sdk),
        .header_roots = rstd::move(header_roots),
    }));
}

auto compiler_plugin_for_package(const Vec<BuiltCompilerPlugin>& products, ref<str> package)
    -> BuildResult<const BuiltCompilerPlugin*> {
    const BuiltCompilerPlugin* result = nullptr;
    for (const auto& product : products) {
        if (product.target.package != package) continue;
        if (result != nullptr) {
            return plugin_failure<const BuiltCompilerPlugin*>(
                rstd::format("compiler plugin package '{}' has more than one product", package));
        }
        result = rstd::addressof(product);
    }
    if (result == nullptr) {
        return plugin_failure<const BuiltCompilerPlugin*>(
            rstd::format("compiler plugin package '{}' is unavailable", package));
    }
    return Ok(result);
}

auto attach_target_compiler_plugins(const cpp::PackageSpec&         package,
                                    const cpp::PackagePlan&         plan,
                                    const Vec<Vec<cpp::UnitId>>&    target_units,
                                    const Vec<BuiltCompilerPlugin>& products,
                                    const ClangToolchain&           toolchain,
                                    CompilePlan& compile_plan) -> BuildResult<empty> {
    for (auto target : plan.target_order) {
        const auto& spec = package.targets[target];
        if (spec.artifact_kind == cpp::ArtifactKind::ProcMacroProvider) continue;
        for (const auto& dependency : spec.plugin_dependencies) {
            const BuiltCompilerPlugin* product = nullptr;
            for (const auto& candidate : products) {
                if (candidate.target.package != dependency.package.as_str()) continue;
                if (product != nullptr) {
                    return plugin_failure<empty>(rstd::format(
                        "compiler plugin package '{}' provides more than one selected product",
                        dependency.package.as_str()));
                }
                product = rstd::addressof(candidate);
            }
            if (product == nullptr) {
                return plugin_failure<empty>(
                    rstd::format("target '{}' has no compiler plugin product for dependency '{}'",
                                 lito::package::package_target_id_text(spec.id).as_str(),
                                 dependency.package.as_str()));
            }
            for (auto unit : target_units[target]) {
                auto invocation = compile_plan_invocation(compile_plan, unit);
                if (invocation == nullptr) {
                    return plugin_failure<empty>(
                        "compiler plugin target invocation is unavailable"_str);
                }
                auto attached =
                    toolchain.attach_compile_plugin(*invocation,
                                                    ResolvedCompilerPluginUsage {
                                                        .plugin = product->plugin.clone(),
                                                        .name   = product->target.package.clone(),
                                                        .arguments = Vec<String>::make(),
                                                        .identity  = product->identity.clone(),
                                                    });
                if (attached.is_err()) {
                    return Err(rstd::into<BuildError>(rstd::move(attached).unwrap_err()));
                }
            }
        }
    }
    return Ok(empty {});
}

} // namespace lito
