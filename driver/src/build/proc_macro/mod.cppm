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
import :build.compile.plan;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

auto create_parent(ref<rstd::path::Path> path) -> BuildResult<empty> {
    auto parent = path.parent();
    if (parent.is_none()) {
        return Err(BuildError::Message(
            rstd::format("proc-macro artifact '{}' has no parent directory", path)));
    }
    auto created = rstd::fs::create_dir_all(*parent);
    if (created.is_err()) {
        return Err(BuildError::System(
            lito::system::SystemError::Io("create proc-macro artifact directory"_Str,
                                          PathBuf::from(*parent),
                                          rstd::move(created).unwrap_err())));
    }
    return Ok(empty {});
}

auto file_digest(ref<rstd::path::Path> path, ref<str> operation) -> BuildResult<String> {
    auto data = rstd::fs::read(path);
    if (data.is_err()) {
        return Err(BuildError::System(lito::system::SystemError::Io(
            operation.into(), PathBuf::from(path), rstd::move(data).unwrap_err())));
    }
    return Ok(rstd_try(licrypto::sha256_hex(data->as_slice())));
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
            return Err(BuildError::Message(rstd::format(
                "proc-macro target '{}' has an unavailable host link dependency",
                lito::package::package_target_id_text(package.targets[target].id).as_str())));
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
            return Err(BuildError::Message(rstd::format(
                "proc-macro package '{}' has more than one provider target", package_name)));
        }
        result = Some(target);
    }
    if (result.is_none()) {
        return Err(BuildError::Message(
            rstd::format("proc-macro package '{}' has no built provider target", package_name)));
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

auto attach_proc_macro_plugins(const cpp::PackageSpec&             package,
                               const cpp::PackagePlan&             plan,
                               const Vec<Vec<cpp::UnitId>>&        target_units,
                               const Vec<BuiltProcMacroAggregate>& aggregates,
                               const ClangToolchain&               toolchain,
                               CompilePlan& compile_plan) -> BuildResult<empty> {
    for (auto target : plan.target_order) {
        const auto& spec = package.targets[target];
        if (spec.proc_macro_dependencies.is_empty()) continue;
        if (spec.language != lito::manifest::PackageLanguage::Cpp)
            return Err(BuildError::Message("proc-macro dependencies require a C++ target"_Str));
        auto selection = rstd_try(proc_macro_aggregate_identity(spec.proc_macro_dependencies));
        const BuiltProcMacroAggregate* aggregate = nullptr;
        for (const auto& candidate : aggregates) {
            if (candidate.selection_identity == selection.as_str()) {
                aggregate = rstd::addressof(candidate);
                break;
            }
        }
        if (aggregate == nullptr)
            return Err(BuildError::Message(
                rstd::format("target '{}' has no matching host proc-macro aggregate",
                             lito::package::package_target_id_text(spec.id).as_str())));
        auto identity =
            rstd_try(licrypto::sha256_hex(rstd::format("lito-pmacro-compile-v1\n{}\n{}\n",
                                                       aggregate->identity.as_str(),
                                                       aggregate->content_identity.as_str())
                                              .as_str()));
        for (auto unit : target_units[target]) {
            auto* invocation = compile_plan_invocation(compile_plan, unit);
            if (invocation == nullptr)
                return Err(BuildError::Message("proc-macro compile invocation is unavailable"_Str));
            auto arguments = Vec<String>::make();
            auto attached = toolchain.attach_compile_plugin(*invocation,
                                                            ResolvedCompilerPluginUsage {
                                                                .plugin = aggregate->plugin.clone(),
                                                                .name   = "pmacro"_Str,
                                                                .arguments = rstd::move(arguments),
                                                                .identity  = identity.clone(),
                                                            });
            if (attached.is_err())
                return Err(rstd::into<BuildError>(rstd::move(attached).unwrap_err()));
        }
    }
    return Ok(empty {});
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
        return Err(BuildError::Message(
            "proc-macro Clang plugin linking is not yet validated on macOS"_Str));
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
        return Err(BuildError::Message(
            "pmacro compiler plugin target is missing from the host package"_Str));
    }
    const auto  support_target = *support;
    const auto& context        = plan.contexts[support_target];
    if (support_target >= libraries.len() || libraries[support_target].is_none() ||
        libraries[support_target]->as_path() != support_product->support_archive.as_path()) {
        return Err(BuildError::Message("pmacro compiler-support target has no host archive"_Str));
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
            return Err(BuildError::Message(
                rstd::format("proc-macro target '{}' has no provider archive",
                             lito::package::package_target_id_text(target.id).as_str())));
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
            "lito-proc-macro-aggregate-product-v5\ncontract:cpp3\n"_Str;
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
                return Err(BuildError::Message("proc-macro provider product is missing"_Str));
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
        auto aggregate_identity =
            rstd_try(licrypto::sha256_hex(aggregate_identity_source.as_str()));
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
        auto providers = request.providers.iter()
                             .map([](auto provider) {
                                 return provider->clone();
                             })
                             .collect<Vec<ProcMacroProviderBinding>>();
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
