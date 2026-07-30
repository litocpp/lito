export module tenon.package;

import rstd;
import tenon.model;

namespace tenon::package_detail
{

using namespace rstd::literals;

using TargetMap = rstd::collections::BTreeMap<String, TargetId>;

template<typename T>
auto failure(String message) -> Result<T> {
    return rstd::Err(Error::make(ErrorKind::Dependency, rstd::move(message)));
}

auto path_equal(rstd::ref<rstd::path::Path> left, rstd::ref<rstd::path::Path> right) -> bool {
    auto left_os     = left.as_os_str();
    auto right_os    = right.as_os_str();
    auto left_bytes  = left_os.as_encoded_bytes();
    auto right_bytes = right_os.as_encoded_bytes();
    if (left_bytes.len() != right_bytes.len()) return false;
    for (auto index = rstd::usize {}; index < left_bytes.len(); ++index) {
        if (left_bytes[index] != right_bytes[index]) return false;
    }
    return true;
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
            if (path_equal(existing.as_path(), value.as_path())) {
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

auto visit_target(const PackageSpec& package,
                  const TargetMap& target_ids,
                  TargetId target,
                  Vec<rstd::uint8_t>& colors,
                  Vec<TargetId>& target_order) -> Result<rstd::empty> {
    auto& color = colors[target];
    if (color == 2) return rstd::Ok(rstd::empty {});
    if (color == 1) {
        return failure<rstd::empty>(rstd::format(
            "target dependency cycle at '{}'", package.targets[target].name.as_str()));
    }

    color = 1;
    for (const auto& dependency : package.targets[target].dependencies) {
        auto found = target_ids.get(dependency.target.as_str());
        if (found.is_none()) {
            return failure<rstd::empty>(rstd::format(
                "target '{}' depends on unknown target '{}'",
                package.targets[target].name.as_str(),
                dependency.target.as_str()));
        }
        auto nested = visit_target(package, target_ids, **found, colors, target_order);
        if (nested.is_err()) return nested;
    }
    color = 2;
    target_order.emplace_back(target);
    return rstd::Ok(rstd::empty {});
}

auto mark_link_dependencies(const PackageSpec& package,
                            const TargetMap& target_ids,
                            TargetId target,
                            Vec<rstd::uint8_t>& marked) -> void {
    for (const auto& dependency : package.targets[target].dependencies) {
        const auto dependency_id = **target_ids.get(dependency.target.as_str());
        if (marked[dependency_id] != rstd::uint8_t {}) continue;
        marked[dependency_id] = rstd::uint8_t(1);
        mark_link_dependencies(package, target_ids, dependency_id, marked);
    }
}

auto append_context_path(String& result,
                         rstd::ref<rstd::str> prefix,
                         rstd::ref<rstd::path::Path> path) -> void {
    result.push_str(prefix);
    auto text = path.to_str();
    if (text.is_some()) result.push_str(*text);
    result.push_ascii('\n');
}

auto context_id(const CompileContext& context, rstd::ref<rstd::str> toolchain_identity) -> String {
    auto result = String::make(toolchain_identity);
    result.push_ascii('\n');
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

} // namespace tenon::package_detail

export namespace tenon
{

auto resolve_package(const PackageSpec& package,
                     rstd::ref<rstd::str> requested_profile,
                     const Vec<String>& requested_targets,
                     rstd::ref<rstd::str> toolchain_identity) -> Result<PackagePlan> {
    using namespace package_detail;

    auto target_ids = TargetMap::make();
    for (auto id = TargetId {}; id < package.targets.len(); ++id) {
        target_ids.insert(package.targets[id].name.clone(), id);
    }

    auto profile_name = requested_profile.size() == rstd::usize {}
                            ? package.default_profile.as_str()
                            : requested_profile;
    const ProfileSpec* profile = nullptr;
    for (const auto& item : package.profiles) {
        if (item.name == profile_name) {
            profile = rstd::addressof(item);
            break;
        }
    }
    if (profile == nullptr) {
        return failure<PackagePlan>(rstd::format("unknown profile '{}'", profile_name));
    }

    const auto& targets = requested_targets.is_empty() ? package.default_targets : requested_targets;
    auto colors       = Vec<rstd::uint8_t>::with_capacity(package.targets.len());
    auto target_order = Vec<TargetId>::make();
    for (auto id = TargetId {}; id < package.targets.len(); ++id) colors.emplace_back(0);
    for (const auto& target_name : targets) {
        auto found = target_ids.get(target_name.as_str());
        if (found.is_none()) {
            return failure<PackagePlan>(
                rstd::format("unknown target '{}'", target_name.as_str()));
        }
        auto visited = visit_target(package, target_ids, **found, colors, target_order);
        if (visited.is_err()) return rstd::Err(rstd::move(visited).unwrap_err());
    }

    auto public_usage   = Vec<rstd::Option<PublicUsage>>::with_capacity(package.targets.len());
    auto public_visible = Vec<Vec<TargetId>>::with_capacity(package.targets.len());
    auto contexts       = Vec<CompileContext>::with_capacity(package.targets.len());
    auto visible_targets = Vec<Vec<TargetId>>::with_capacity(package.targets.len());
    auto link_dependencies = Vec<Vec<TargetId>>::with_capacity(package.targets.len());
    for (auto id = TargetId {}; id < package.targets.len(); ++id) {
        public_usage.emplace_back(rstd::None());
        public_visible.emplace_back();
        contexts.emplace_back();
        visible_targets.emplace_back();
        link_dependencies.emplace_back();
    }

    for (auto target : target_order) {
        const auto& spec = package.targets[target];
        auto usage       = PublicUsage {};
        append_unique(usage.include_directories, spec.usage.public_include_directories);
        append_unique(usage.definitions, spec.usage.public_definitions);
        append_unique(usage.options, spec.usage.public_options);

        auto exported_targets = Vec<TargetId>::make();
        append_unique(exported_targets, target);
        for (const auto& dependency : spec.dependencies) {
            if (dependency.visibility != DependencyVisibility::Public) continue;
            auto dependency_id = **target_ids.get(dependency.target.as_str());
            auto& nested_usage = *public_usage[dependency_id];
            append_unique(usage.include_directories, nested_usage.include_directories);
            append_unique(usage.definitions, nested_usage.definitions);
            append_unique(usage.options, nested_usage.options);
            append_unique(exported_targets, public_visible[dependency_id]);
        }
        public_usage[target]   = rstd::Some(rstd::move(usage));
        public_visible[target] = rstd::move(exported_targets);
    }

    for (auto target : target_order) {
        const auto& spec = package.targets[target];
        auto context = CompileContext {
            .standard_library = profile->standard_library,
            .bmi_mode = profile->bmi_mode,
            .language_standard = profile->language_standard.clone(),
        };
        append_unique(context.include_directories, spec.usage.public_include_directories);
        append_unique(context.include_directories, spec.usage.private_include_directories);
        append_unique(context.definitions, spec.usage.public_definitions);
        append_unique(context.definitions, spec.usage.private_definitions);
        append_unique(context.options, profile->options);
        append_unique(context.options, spec.usage.public_options);
        append_unique(context.options, spec.usage.private_options);

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
        context.id              = context_id(context, toolchain_identity);
        contexts[target]        = rstd::move(context);
        visible_targets[target] = rstd::move(visible);
    }

    for (auto target : target_order) {
        auto marked = Vec<rstd::uint8_t>::with_capacity(package.targets.len());
        for (auto id = TargetId {}; id < package.targets.len(); ++id) {
            marked.emplace_back(rstd::uint8_t {});
        }
        mark_link_dependencies(package, target_ids, target, marked);
        auto& dependencies = link_dependencies[target];
        for (auto index = target_order.len(); index > rstd::usize {}; --index) {
            const auto candidate = target_order[index - rstd::usize(1)];
            if (marked[candidate] != rstd::uint8_t {}) dependencies.emplace_back(candidate);
        }
    }

    return rstd::Ok(PackagePlan {
        .package = rstd::addressof(package),
        .profile = profile,
        .target_order = rstd::move(target_order),
        .contexts = rstd::move(contexts),
        .visible_targets = rstd::move(visible_targets),
        .link_dependencies = rstd::move(link_dependencies),
    });
}

} // namespace tenon
