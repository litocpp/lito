export module lito.package:plan;

import rstd;
import lito.error;
import lito.cpp;
import lito.cpp.bmi;
import lito.manifest.contract;
import lito.dependency.contract;
import lito.package.identity;
import lito.package.target_contract;
import lito.build.identity;
import lito.build.plan_contract;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

template<typename T>
auto plan_failure(String message) -> Result<T> {
    return Err(Error::make(ErrorKind::Dependency, rstd::move(message)));
}

template<typename T>
auto plan_failure(ref<str> message) -> Result<T> {
    return Err(Error::make(ErrorKind::Dependency, message));
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
            output.occurrences.push(as<rstd::clone::Clone>(occurrence).clone());
        }
    }
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

auto target_text(const PackageTargetId& id) -> String {
    return rstd::format(
        "{}::{}::{}", id.package.as_str(), package_target_kind_name(id.kind), id.name.as_str());
}

auto target_index(const PackageMetadata& package, const PackageTargetId& id) -> Option<TargetId> {
    for (auto candidate = TargetId {}; candidate < package.targets.len(); ++candidate) {
        if (package.targets[candidate].id == id) return Some(candidate);
    }
    return None();
}

auto target_index(const PackageSpec& package, const PackageTargetId& id) -> Option<TargetId> {
    for (auto candidate = TargetId {}; candidate < package.targets.len(); ++candidate) {
        if (package.targets[candidate].id == id) return Some(candidate);
    }
    return None();
}

struct PublicUsage {
    Vec<PathBuf>     include_directories;
    Vec<String>      definitions;
    CppArgumentLayer arguments;
    Vec<String>      external_identities;
};

auto visit_target(const PackageMetadata& package,
                  TargetId               target,
                  Vec<uint8_t>&          colors,
                  Vec<TargetId>&         target_order) -> Result<empty> {
    auto& color = colors[target];
    if (color == 2) return Ok(empty {});
    if (color == 1) {
        return plan_failure<empty>(rstd::format("target dependency cycle at '{}'",
                                                target_text(package.targets[target].id).as_str()));
    }

    color = 1;
    for (auto pass = usize {}; pass < usize(2); ++pass) {
        const auto runtime = pass == usize {};
        for (const auto& dependency : package.targets[target].dependencies) {
            if ((dependency.visibility == DependencyVisibility::Runtime) != runtime) continue;
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

auto context_id(const CompileContext& context) -> String {
    auto result = String::make("lito-compile-context-v3\n"_str);
    result.push_str(bmi_representation_name(context.bmi.representation));
    result.push_ascii('\n');
    result.push_str(bmi_source_embedding_name(context.bmi.source_embedding));
    result.push_ascii('\n');
    result.push_str(cpp_compile_identity(context.cpp).as_str());
    result.push_str(cpp_public_requirements_identity(context.public_requirements).as_str());
    for (const auto& identity : context.external_identities) {
        result.push_str(
            rstd::format("external:{}:{}\n", identity.len(), identity.as_str()).as_str());
    }
    return result;
}

auto scan_context_id(const CompileContext& context) -> String {
    auto result = String::make("lito-scan-context-v1\n"_str);
    result.push_str(bmi_representation_name(context.bmi.representation));
    result.push_ascii('\n');
    result.push_str(bmi_source_embedding_name(context.bmi.source_embedding));
    result.push_ascii('\n');
    result.push_str(cpp_scan_identity(context.cpp).as_str());
    result.push_str(cpp_public_requirements_identity(context.public_requirements).as_str());
    for (const auto& identity : context.external_identities) {
        result.push_str(
            rstd::format("external:{}:{}\n", identity.len(), identity.as_str()).as_str());
    }
    return result;
}

auto attachment_context(const CompileContext&       library,
                        const CompileContext&       test,
                        const TestAttachmentTarget& attachment) -> Result<CompileContext> {
    if (library.bmi.representation != test.bmi.representation ||
        library.bmi.source_embedding != test.bmi.source_embedding) {
        return plan_failure<CompileContext>(
            "test attachment cannot merge different BMI requests"_str);
    }
    auto result = CompileContext {
        .bmi                 = library.bmi,
        .cpp                 = as<rstd::clone::Clone>(library.cpp).clone(),
        .public_requirements = as<rstd::clone::Clone>(library.public_requirements).clone(),
        .external_identities = as<rstd::clone::Clone>(library.external_identities).clone(),
    };
    auto merged = merge_cpp_options(rstd::move(result.cpp), test.cpp);
    if (merged.is_err()) {
        return plan_failure<CompileContext>(rstd::move(merged).unwrap_err());
    }
    result.cpp                 = rstd::move(merged).unwrap();
    result.public_requirements = merge_cpp_public_requirements(
        rstd::move(result.public_requirements), test.public_requirements);
    append_unique(result.external_identities, test.external_identities);
    result.id      = rstd::format("lito-test-attachment-context-v1\ntest:{}\nlibrary:{}\n{}",
                                  target_text(attachment.test_target).as_str(),
                                  target_text(attachment.library_target).as_str(),
                                  context_id(result).as_str());
    result.scan_id = scan_context_id(result);
    return Ok(rstd::move(result));
}

} // namespace lito

export namespace lito
{

auto compile_test_context(const CompileContext& base, const CompileTestCase& test)
    -> Result<CompileContext> {
    auto context = CompileContext {
        .bmi                 = base.bmi,
        .cpp                 = as<rstd::clone::Clone>(base.cpp).clone(),
        .public_requirements = as<rstd::clone::Clone>(base.public_requirements).clone(),
        .external_identities = as<rstd::clone::Clone>(base.external_identities).clone(),
    };
    auto layer      = CppOptionLayer {};
    layer.arguments = as<rstd::clone::Clone>(test.arguments).clone();
    auto applied    = apply_cpp_option_layer(rstd::move(context.cpp), rstd::move(layer));
    if (applied.is_err()) {
        return plan_failure<CompileContext>(rstd::move(applied).unwrap_err());
    }
    context.cpp     = rstd::move(applied).unwrap();
    context.id      = context_id(context);
    context.scan_id = scan_context_id(context);
    return Ok(rstd::move(context));
}

auto resolve_source_selection(const PackageMetadata& package,
                              ref<str>               requested_profile,
                              const Vec<String>&     requested_targets)
    -> Result<SourceTargetSelection> {
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

    auto selected_identities = Vec<PackageTargetId>::make();
    if (requested_targets.is_empty()) {
        selected_identities = Vec<PackageTargetId>::with_capacity(package.default_targets.len());
        for (const auto& target : package.default_targets) {
            selected_identities.push(target.clone());
        }
    } else {
        for (const auto& requested : requested_targets) {
            auto separated = requested.as_str().split_once(":"_str);
            auto kind      = Option<PackageTargetKind> {};
            auto name      = requested.as_str();
            if (separated.is_some()) {
                auto kind_text = separated->get<0>();
                name           = separated->get<1>();
                if (kind_text == "lib"_str)
                    kind = Some(PackageTargetKind::Library);
                else if (kind_text == "bin"_str)
                    kind = Some(PackageTargetKind::Binary);
                else if (kind_text == "test"_str)
                    kind = Some(PackageTargetKind::Test);
                else if (kind_text == "bench"_str)
                    kind = Some(PackageTargetKind::Benchmark);
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
            auto matched_packages = rstd::collections::BTreeMap<String, PackageTargetKind>::make();
            auto matches          = usize {};
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
                        package_target_kind_name(candidate.kind),
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
                                   const SourceTargetSelection& selection) -> Result<Vec<String>> {
    auto result = Vec<String>::make();
    for (auto target : selection.selected_targets) {
        if (target >= package.targets.len()) {
            return plan_failure<Vec<String>>("source target selection does not match package"_str);
        }
        auto name    = package.targets[target].id.package.as_str();
        auto allowed = false;
        for (const auto& candidate : package.build_script_packages) {
            if (candidate == name) {
                allowed = true;
                break;
            }
        }
        if (! allowed) continue;
        append_unique(result, name);
    }
    return Ok(rstd::move(result));
}

auto resolve_source_discovery(const PackageMetadata& package, SourceTargetSelection selection)
    -> Result<SourceDiscoveryPlan> {
    if (selection.profile >= package.profiles.len()) {
        return plan_failure<SourceDiscoveryPlan>(
            "source target selection does not match package profile"_str);
    }
    for (auto target : selection.target_order) {
        if (target >= package.targets.len()) {
            return plan_failure<SourceDiscoveryPlan>(
                "source target selection does not match package targets"_str);
        }
    }
    auto profile      = selection.profile;
    auto target_order = rstd::move(selection.target_order);

    auto public_usage    = Vec<Option<PublicUsage>>::with_capacity(package.targets.len());
    auto public_visible  = Vec<Vec<TargetId>>::with_capacity(package.targets.len());
    auto contexts        = Vec<CompileContext>::with_capacity(package.targets.len());
    auto visible_targets = Vec<Vec<TargetId>>::with_capacity(package.targets.len());
    auto link_inputs     = Vec<Vec<PlannedLinkInput>>::with_capacity(package.targets.len());
    auto linker_options  = Vec<Vec<String>>::with_capacity(package.targets.len());
    for (auto id = TargetId {}; id < package.targets.len(); ++id) {
        public_usage.emplace_back(None());
        public_visible.emplace_back();
        contexts.emplace_back();
        visible_targets.emplace_back();
        link_inputs.emplace_back();
        linker_options.emplace_back();
    }

    for (auto target : target_order) {
        const auto& spec  = package.targets[target];
        auto        usage = PublicUsage {};
        if (spec.id.kind == PackageTargetKind::Library) {
            append_unique(usage.include_directories, spec.usage.public_include_directories);
            append_unique(usage.definitions, spec.usage.public_definitions);
            append_unique(usage.arguments, spec.usage.public_arguments);
            for (const auto& dependency : spec.external_dependencies) {
                for (const auto& target : dependency.targets) {
                    if (target.visibility != DependencyVisibility::Public) continue;
                    append_unique(usage.arguments, target.compile_arguments);
                    append_unique(usage.external_identities, target.identity.as_str());
                }
            }
        }

        auto exported_targets = Vec<TargetId>::make();
        append_unique(exported_targets, target);
        for (const auto& dependency : spec.dependencies) {
            if (dependency.visibility != DependencyVisibility::Public) continue;
            auto  dependency_id = *target_index(package, dependency.target);
            auto& nested_usage  = *public_usage[dependency_id];
            append_unique(usage.include_directories, nested_usage.include_directories);
            append_unique(usage.definitions, nested_usage.definitions);
            append_unique(usage.arguments, nested_usage.arguments);
            append_unique(usage.external_identities, nested_usage.external_identities);
            append_unique(exported_targets, public_visible[dependency_id]);
        }
        public_usage[target]   = Some(rstd::move(usage));
        public_visible[target] = rstd::move(exported_targets);
    }

    for (auto target : target_order) {
        const auto& spec             = package.targets[target];
        const auto& selected_profile = package.profiles[profile];
        auto        context          = CompileContext {
            .bmi = selected_profile.bmi,
        };
        auto        public_layer   = CppOptionLayer {};
        const auto& exported_usage = *public_usage[target];
        append_unique(public_layer.include_directories, exported_usage.include_directories);
        append_unique(public_layer.definitions, exported_usage.definitions);
        append_unique(public_layer.arguments, exported_usage.arguments);
        auto public_cpp = apply_cpp_option_layer(
            as<rstd::clone::Clone>(selected_profile.cpp).clone(), rstd::move(public_layer));
        if (public_cpp.is_err()) {
            return plan_failure<SourceDiscoveryPlan>(rstd::move(public_cpp).unwrap_err());
        }
        context.public_requirements = cpp_public_requirements(*public_cpp);
        context.cpp                 = rstd::move(public_cpp).unwrap();
        append_unique(context.external_identities, exported_usage.external_identities);

        auto private_layer = CppOptionLayer {};
        append_unique(private_layer.include_directories, spec.usage.private_include_directories);
        append_unique(private_layer.definitions, spec.usage.private_definitions);
        append_unique(private_layer.arguments, spec.usage.private_arguments);
        for (const auto& dependency : spec.external_dependencies) {
            for (const auto& external_target : dependency.targets) {
                const auto consumed_publicly =
                    spec.id.kind != PackageTargetKind::Library &&
                    external_target.visibility == DependencyVisibility::Public;
                if (external_target.visibility != DependencyVisibility::Private &&
                    ! consumed_publicly) {
                    continue;
                }
                append_unique(private_layer.arguments, external_target.compile_arguments);
                append_unique(context.external_identities, external_target.identity.as_str());
            }
        }

        auto visible = Vec<TargetId>::make();
        append_unique(visible, target);
        for (const auto& dependency : spec.dependencies) {
            if (dependency.visibility == DependencyVisibility::Runtime) continue;
            auto dependency_id = *target_index(package, dependency.target);
            append_unique(visible, public_visible[dependency_id]);
            if (dependency.visibility == DependencyVisibility::Public) continue;
            const auto& usage = *public_usage[dependency_id];
            append_unique(private_layer.include_directories, usage.include_directories);
            append_unique(private_layer.definitions, usage.definitions);
            append_unique(private_layer.arguments, usage.arguments);
            append_unique(context.external_identities, usage.external_identities);
        }
        auto applied = apply_cpp_option_layer(rstd::move(context.cpp), rstd::move(private_layer));
        if (applied.is_err()) {
            return plan_failure<SourceDiscoveryPlan>(rstd::move(applied).unwrap_err());
        }
        context.cpp             = rstd::move(applied).unwrap();
        context.id              = context_id(context);
        context.scan_id         = scan_context_id(context);
        contexts[target]        = rstd::move(context);
        visible_targets[target] = rstd::move(visible);
        append_unique(linker_options[target], selected_profile.linker_options);
        append_unique(linker_options[target], spec.usage.private_linker_options);
    }

    for (auto target = TargetId {}; target < package.targets.len(); ++target) {
        const auto& attachment = package.targets[target].test_attachment;
        if (attachment.is_none()) continue;
        auto test_id    = target_index(package, attachment->test_target);
        auto library_id = target_index(package, attachment->library_target);
        if (test_id.is_none() || library_id.is_none()) {
            return plan_failure<SourceDiscoveryPlan>(
                "test attachment references an unknown target"_str);
        }
        if (! append_unique(visible_targets[target], target)) {
            return plan_failure<SourceDiscoveryPlan>(
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
        auto ordered          = visit_target(package, target, colors, dependency_order);
        if (ordered.is_err()) return Err(rstd::move(ordered).unwrap_err());
        auto& inputs = link_inputs[target];
        for (auto index = dependency_order.len(); index > usize {}; --index) {
            const auto candidate = dependency_order[index - usize(1)];
            if (candidate == target) continue;
            inputs.push(PlannedLinkInput::Target(candidate));
            for (const auto& external : package.targets[candidate].external_dependencies) {
                if (! external.link_arguments.tokens.is_empty()) {
                    inputs.push(PlannedLinkInput::External(external.link_arguments.clone()));
                }
            }
        }
        for (const auto& external : package.targets[target].external_dependencies) {
            if (! external.link_arguments.tokens.is_empty()) {
                inputs.push(PlannedLinkInput::External(external.link_arguments.clone()));
            }
        }
    }

    auto target_identities = Vec<PackageTargetId>::with_capacity(package.targets.len());
    for (const auto& target : package.targets) {
        target_identities.push(target.id.clone());
    }
    return Ok(SourceDiscoveryPlan {
        .profile           = profile,
        .target_identities = rstd::move(target_identities),
        .target_order      = rstd::move(target_order),
        .contexts          = rstd::move(contexts),
        .visible_targets   = rstd::move(visible_targets),
        .link_inputs       = rstd::move(link_inputs),
        .linker_options    = rstd::move(linker_options),
    });
}

auto resolve_source_discovery(const PackageMetadata& package,
                              ref<str>               requested_profile,
                              const Vec<String>& requested_targets) -> Result<SourceDiscoveryPlan> {
    auto selection = resolve_source_selection(package, requested_profile, requested_targets);
    if (selection.is_err()) return Err(rstd::move(selection).unwrap_err());
    return resolve_source_discovery(package, rstd::move(selection).unwrap());
}

auto finalize_package_plan(const PackageSpec& package, SourceDiscoveryPlan discovery)
    -> Result<PackagePlan> {
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
        .package         = rstd::addressof(package),
        .profile         = rstd::addressof(package.profiles[discovery.profile]),
        .target_order    = rstd::move(discovery.target_order),
        .contexts        = rstd::move(discovery.contexts),
        .visible_targets = rstd::move(discovery.visible_targets),
        .link_inputs     = rstd::move(discovery.link_inputs),
        .linker_options  = rstd::move(discovery.linker_options),
    });
}

} // namespace lito
