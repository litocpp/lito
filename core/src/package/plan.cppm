export module tenon.package:plan;

import rstd;
import tenon.model;

using namespace rstd::prelude;
using namespace rstd::literals;
using TargetMap = rstd::collections::BTreeMap<String, tenon::TargetId>;

namespace tenon
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

struct PublicUsage {
    Vec<PathBuf> include_directories;
    Vec<String>  definitions;
    Vec<String>  options;
};

auto visit_target(const PackageMetadata& package,
                  const TargetMap&       target_ids,
                  TargetId               target,
                  Vec<uint8_t>&          colors,
                  Vec<TargetId>&         target_order) -> Result<empty> {
    auto& color = colors[target];
    if (color == 2) return Ok(empty {});
    if (color == 1) {
        return plan_failure<empty>(rstd::format("target dependency cycle at '{}'",
                                                package.targets[target].manifest.name.as_str()));
    }

    color = 1;
    for (auto pass = usize {}; pass < usize(2); ++pass) {
        const auto runtime = pass == usize {};
        for (const auto& dependency : package.targets[target].dependencies) {
            if ((dependency.visibility == DependencyVisibility::Runtime) != runtime) continue;
            auto found = target_ids.get(dependency.target.as_str());
            if (found.is_none()) {
                return plan_failure<empty>(
                    rstd::format("target '{}' depends on unknown target '{}'",
                                 package.targets[target].manifest.name.as_str(),
                                 dependency.target.as_str()));
            }
            auto nested = visit_target(package, target_ids, **found, colors, target_order);
            if (nested.is_err()) return nested;
        }
    }
    color = 2;
    target_order.emplace_back(target);
    return Ok(empty {});
}

auto append_context_path(String& result, ref<str> prefix, ref<rstd::path::Path> path) -> void {
    result.push_str(prefix);
    auto text = path.to_str();
    if (text.is_some()) result.push_str(*text);
    result.push_ascii('\n');
}

auto context_id(const CompileContext& context) -> String {
    auto result = String::make("tenon-compile-context-v1\n"_str);
    result.push_str(context.language_standard.as_str());
    result.push_ascii('\n');
    result.push_str(context.standard_library == StandardLibrary::Libstdcxx ? "libstdc++\n"_str
                                                                           : "libc++\n"_str);
    result.push_str(context.bmi_mode == BmiMode::Reduced ? "reduced\n"_str : "full\n"_str);
    for (const auto& value : context.include_directories) {
        append_context_path(result, "I"_str, value.as_path());
    }
    for (const auto& value : context.definitions) {
        result.push_ascii('D');
        result.push_str(value.as_str());
        result.push_ascii('\n');
    }
    for (const auto& value : context.options) {
        result.push_ascii('O');
        result.push_str(value.as_str());
        result.push_ascii('\n');
    }
    return result;
}

auto attachment_context(const CompileContext&       library,
                        const CompileContext&       test,
                        const TestAttachmentTarget& attachment) -> CompileContext {
    auto result = CompileContext {
        .standard_library  = library.standard_library,
        .bmi_mode          = library.bmi_mode,
        .language_standard = library.language_standard.clone(),
    };
    append_unique(result.include_directories, library.include_directories);
    append_unique(result.include_directories, test.include_directories);
    append_unique(result.definitions, library.definitions);
    append_unique(result.definitions, test.definitions);
    append_unique(result.options, library.options);
    append_unique(result.options, test.options);
    result.id = rstd::format("tenon-test-attachment-context-v1\ntest:{}\nlibrary:{}\n{}",
                             attachment.test_target.as_str(),
                             attachment.library_target.as_str(),
                             context_id(result).as_str());
    return result;
}

} // namespace tenon

export namespace tenon
{

auto compile_test_context(const CompileContext& base, const CompileTestCase& test)
    -> CompileContext {
    auto context = CompileContext {
        .standard_library  = base.standard_library,
        .bmi_mode          = base.bmi_mode,
        .language_standard = base.language_standard.clone(),
    };
    append_unique(context.include_directories, base.include_directories);
    append_unique(context.definitions, base.definitions);
    append_unique(context.options, base.options);
    append_unique(context.options, test.options);
    context.id = context_id(context);
    return context;
}

auto resolve_source_discovery(const PackageMetadata& package,
                              ref<str>               requested_profile,
                              const Vec<String>& requested_targets) -> Result<SourceDiscoveryPlan> {
    auto target_ids = TargetMap::make();
    for (auto id = TargetId {}; id < package.targets.len(); ++id) {
        target_ids.insert(package.targets[id].manifest.name.clone(), id);
    }

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
        return plan_failure<SourceDiscoveryPlan>(
            rstd::format("unknown profile '{}'", profile_name));
    }

    const auto& targets =
        requested_targets.is_empty() ? package.default_targets : requested_targets;
    auto colors       = Vec<uint8_t>::with_capacity(package.targets.len());
    auto target_order = Vec<TargetId>::make();
    for (auto id = TargetId {}; id < package.targets.len(); ++id) colors.emplace_back(0);
    for (const auto& target_name : targets) {
        auto found = target_ids.get(target_name.as_str());
        if (found.is_none()) {
            return plan_failure<SourceDiscoveryPlan>(
                rstd::format("unknown target '{}'", target_name.as_str()));
        }
        auto visited = visit_target(package, target_ids, **found, colors, target_order);
        if (visited.is_err()) return Err(rstd::move(visited).unwrap_err());
    }

    auto public_usage      = Vec<Option<PublicUsage>>::with_capacity(package.targets.len());
    auto public_visible    = Vec<Vec<TargetId>>::with_capacity(package.targets.len());
    auto contexts          = Vec<CompileContext>::with_capacity(package.targets.len());
    auto visible_targets   = Vec<Vec<TargetId>>::with_capacity(package.targets.len());
    auto link_dependencies = Vec<Vec<TargetId>>::with_capacity(package.targets.len());
    auto linker_options    = Vec<Vec<String>>::with_capacity(package.targets.len());
    for (auto id = TargetId {}; id < package.targets.len(); ++id) {
        public_usage.emplace_back(None());
        public_visible.emplace_back();
        contexts.emplace_back();
        visible_targets.emplace_back();
        link_dependencies.emplace_back();
        linker_options.emplace_back();
    }

    for (auto target : target_order) {
        const auto& spec  = package.targets[target];
        auto        usage = PublicUsage {};
        append_unique(usage.include_directories, spec.manifest.usage.public_include_directories);
        append_unique(usage.definitions, spec.manifest.usage.public_definitions);
        append_unique(usage.options, spec.manifest.usage.public_options);

        auto exported_targets = Vec<TargetId>::make();
        append_unique(exported_targets, target);
        for (const auto& dependency : spec.dependencies) {
            if (dependency.visibility != DependencyVisibility::Public) continue;
            auto  dependency_id = **target_ids.get(dependency.target.as_str());
            auto& nested_usage  = *public_usage[dependency_id];
            append_unique(usage.include_directories, nested_usage.include_directories);
            append_unique(usage.definitions, nested_usage.definitions);
            append_unique(usage.options, nested_usage.options);
            append_unique(exported_targets, public_visible[dependency_id]);
        }
        public_usage[target]   = Some(rstd::move(usage));
        public_visible[target] = rstd::move(exported_targets);
    }

    for (auto target : target_order) {
        const auto& spec             = package.targets[target];
        const auto& selected_profile = package.profiles[*profile];
        auto        context          = CompileContext {
            .standard_library  = selected_profile.standard_library,
            .bmi_mode          = selected_profile.bmi_mode,
            .language_standard = selected_profile.language_standard.clone(),
        };
        append_unique(context.include_directories, spec.manifest.usage.public_include_directories);
        append_unique(context.include_directories, spec.manifest.usage.private_include_directories);
        append_unique(context.definitions, spec.manifest.usage.public_definitions);
        append_unique(context.definitions, spec.manifest.usage.private_definitions);
        append_unique(context.options, selected_profile.options);
        append_unique(context.options, spec.manifest.usage.public_options);
        append_unique(context.options, spec.manifest.usage.private_options);

        auto visible = Vec<TargetId>::make();
        append_unique(visible, target);
        for (const auto& dependency : spec.dependencies) {
            if (dependency.visibility == DependencyVisibility::Runtime) continue;
            auto dependency_id = **target_ids.get(dependency.target.as_str());
            append_unique(visible, public_visible[dependency_id]);
            const auto& usage = *public_usage[dependency_id];
            append_unique(context.include_directories, usage.include_directories);
            append_unique(context.definitions, usage.definitions);
            append_unique(context.options, usage.options);
        }
        context.id              = context_id(context);
        contexts[target]        = rstd::move(context);
        visible_targets[target] = rstd::move(visible);
        append_unique(linker_options[target], selected_profile.linker_options);
        append_unique(linker_options[target], spec.manifest.usage.private_linker_options);
    }

    for (auto target = TargetId {}; target < package.targets.len(); ++target) {
        const auto& attachment = package.targets[target].test_attachment;
        if (attachment.is_none()) continue;
        auto test_id    = target_ids.get(attachment->test_target.as_str());
        auto library_id = target_ids.get(attachment->library_target.as_str());
        if (test_id.is_none() || library_id.is_none()) {
            return plan_failure<SourceDiscoveryPlan>(
                "test attachment references an unknown target"_str);
        }
        if (! append_unique(visible_targets[target], target)) {
            return plan_failure<SourceDiscoveryPlan>(
                "test attachment target is repeated in the build graph"_str);
        }
        append_unique(visible_targets[target], visible_targets[**library_id]);
        append_unique(visible_targets[target], visible_targets[**test_id]);
        contexts[target] =
            attachment_context(contexts[**library_id], contexts[**test_id], *attachment);
    }

    auto expanded_target_order = Vec<TargetId>::with_capacity(package.targets.len());
    for (auto selected_target : target_order) {
        for (auto candidate = TargetId {}; candidate < package.targets.len(); ++candidate) {
            const auto& attachment = package.targets[candidate].test_attachment;
            if (attachment.is_some() &&
                attachment->test_target ==
                    package.targets[selected_target].manifest.name.as_str()) {
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
        auto ordered          = visit_target(package, target_ids, target, colors, dependency_order);
        if (ordered.is_err()) return Err(rstd::move(ordered).unwrap_err());
        auto& dependencies = link_dependencies[target];
        for (auto index = dependency_order.len(); index > usize {}; --index) {
            const auto candidate = dependency_order[index - usize(1)];
            if (candidate != target) dependencies.emplace_back(candidate);
        }
    }

    auto target_names = Vec<String>::with_capacity(package.targets.len());
    for (const auto& target : package.targets) {
        target_names.push(target.manifest.name.clone());
    }
    return Ok(SourceDiscoveryPlan {
        .profile           = *profile,
        .target_names      = rstd::move(target_names),
        .target_order      = rstd::move(target_order),
        .contexts          = rstd::move(contexts),
        .visible_targets   = rstd::move(visible_targets),
        .link_dependencies = rstd::move(link_dependencies),
        .linker_options    = rstd::move(linker_options),
    });
}

auto finalize_package_plan(const PackageSpec& package, SourceDiscoveryPlan discovery)
    -> Result<PackagePlan> {
    if (discovery.profile >= package.profiles.len() ||
        discovery.target_names.len() != package.targets.len()) {
        return plan_failure<PackagePlan>(
            "source discovery plan does not match the finalized package"_str);
    }
    for (auto target = TargetId {}; target < package.targets.len(); ++target) {
        if (discovery.target_names[target].as_str() != package.targets[target].name.as_str()) {
            return plan_failure<PackagePlan>(
                "source discovery target order changed during finalization"_str);
        }
    }
    return Ok(PackagePlan {
        .package           = rstd::addressof(package),
        .profile           = rstd::addressof(package.profiles[discovery.profile]),
        .target_order      = rstd::move(discovery.target_order),
        .contexts          = rstd::move(discovery.contexts),
        .visible_targets   = rstd::move(discovery.visible_targets),
        .link_dependencies = rstd::move(discovery.link_dependencies),
        .linker_options    = rstd::move(discovery.linker_options),
    });
}

} // namespace tenon
