module;
#include <rstd/macro.hpp>

module lito.driver:build.native_action_graph;

import rstd;
import lito.core;
import lito.cpp;
import :build.action_graph;
import :build.compile_plan;
import :build.error;
import :build.layout;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

auto target_output_kind(cpp::ArtifactKind kind) noexcept -> Option<BuildArtifactKind> {
    if (kind == cpp::ArtifactKind::StaticLibrary || kind == cpp::ArtifactKind::CompilerPlugin ||
        kind == cpp::ArtifactKind::ProcMacroProvider ||
        kind == cpp::ArtifactKind::TestAttachmentArchive) {
        return Some(BuildArtifactKind::Archive);
    }
    if (kind == cpp::ArtifactKind::SharedLibrary) return Some(BuildArtifactKind::SharedLibrary);
    if (kind == cpp::ArtifactKind::Executable || kind == cpp::ArtifactKind::TestExecutable ||
        kind == cpp::ArtifactKind::BenchmarkExecutable) {
        return Some(BuildArtifactKind::Executable);
    }
    return None();
}

auto target_output_path(const BuildLayout& layout, const cpp::TargetSpec& target) -> PathBuf {
    if (target.artifact_kind == cpp::ArtifactKind::StaticLibrary ||
        target.artifact_kind == cpp::ArtifactKind::CompilerPlugin ||
        target.artifact_kind == cpp::ArtifactKind::ProcMacroProvider ||
        target.artifact_kind == cpp::ArtifactKind::TestAttachmentArchive) {
        return target.test_attachment.is_some()
                   ? layout.test_attachment_archive(*target.test_attachment,
                                                    target.archive_stem.as_str())
                   : layout.archive(target.id, target.artifact_name.as_str());
    }
    if (target.artifact_kind == cpp::ArtifactKind::SharedLibrary) {
        return layout.shared_library(target.id, target.artifact_name.as_str());
    }
    if (target.artifact_kind == cpp::ArtifactKind::TestExecutable) {
        return layout.test(target.id, target.artifact_name.as_str());
    }
    if (target.artifact_kind == cpp::ArtifactKind::BenchmarkExecutable) {
        return layout.benchmark(target.id, target.artifact_name.as_str());
    }
    return layout.executable(target.id, target.artifact_name.as_str());
}

auto contains_target(slice<cpp::TargetId> targets, cpp::TargetId target) noexcept -> bool {
    for (auto candidate : targets)
        if (candidate == target) return true;
    return false;
}

struct NativeActionGraph {
    BuildActionGraph             graph;
    Vec<BuildArtifactId>         source_artifacts;
    Vec<BuildActionId>           compile_actions;
    Vec<BuildArtifactId>         object_artifacts;
    Vec<Option<BuildArtifactId>> bmi_artifacts;
    Vec<Option<BuildActionId>>   target_actions;
    Vec<Option<BuildArtifactId>> target_artifacts;

    auto compile_unit(BuildActionId action) const noexcept -> Option<cpp::UnitId> {
        for (auto unit = cpp::UnitId {}; unit < compile_actions.len(); ++unit) {
            if (compile_actions[unit] == action) return Some(unit);
        }
        return None();
    }

    auto target(BuildActionId action) const noexcept -> Option<cpp::TargetId> {
        for (auto target = cpp::TargetId {}; target < target_actions.len(); ++target) {
            if (target_actions[target].is_some() && *target_actions[target] == action) {
                return Some(target);
            }
        }
        return None();
    }
};

auto make_native_action_graph(usize target_count) -> NativeActionGraph {
    auto result = NativeActionGraph {
        .graph            = BuildActionGraph {},
        .source_artifacts = Vec<BuildArtifactId>::make(),
        .compile_actions  = Vec<BuildActionId>::make(),
        .object_artifacts = Vec<BuildArtifactId>::make(),
        .bmi_artifacts    = Vec<Option<BuildArtifactId>>::make(),
        .target_actions   = Vec<Option<BuildActionId>>::with_capacity(target_count),
        .target_artifacts = Vec<Option<BuildArtifactId>>::with_capacity(target_count),
    };
    for (usize target {}; target < target_count; ++target) {
        result.target_actions.emplace_back();
        result.target_artifacts.emplace_back();
    }
    return result;
}

auto extend_native_action_graph(NativeActionGraph&            result,
                                const cpp::PackageSpec&       package,
                                const cpp::PackagePlan&       package_plan,
                                const BuildLayout&            layout,
                                const Vec<cpp::PreparedUnit>& units,
                                const Vec<Vec<cpp::UnitId>>&  target_units,
                                const CompilePlan&            compile,
                                slice<cpp::TargetId>          action_targets,
                                const ExecutionDomainId&      domain) -> BuildResult<empty> {
    if (units.len() != compile.nodes.len() || target_units.len() != package.targets.len() ||
        result.source_artifacts.len() != result.compile_actions.len() ||
        result.compile_actions.len() != result.object_artifacts.len() ||
        result.object_artifacts.len() != result.bmi_artifacts.len() ||
        result.compile_actions.len() > units.len() ||
        result.target_actions.len() != package.targets.len() ||
        result.target_artifacts.len() != package.targets.len()) {
        return Err(BuildError::Message(
            String::make("native action graph inputs have inconsistent lengths"_str)));
    }

    auto first_new_unit = cpp::UnitId(result.compile_actions.len());
    for (auto unit = first_new_unit; unit < units.len(); ++unit) {
        auto generated = Option<BuildArtifactId> {};
        for (usize index {}; index < (*result.graph.artifacts()).len(); ++index) {
            const auto& artifact = (*result.graph.artifacts())[index];
            if (artifact.kind != BuildArtifactKind::Generated || artifact.path.is_none() ||
                artifact.path->as_path() != units[unit].unit.source.as_path()) {
                continue;
            }
            generated = Some(BuildArtifactId { .value = index });
            break;
        }
        if (generated.is_some()) {
            result.source_artifacts.emplace_back(*generated);
        } else {
            result.source_artifacts.emplace_back(
                rstd_try(result.graph.add_artifact(BuildArtifactSpec {
                    .identity = rstd::format("source:{}:{}",
                                             units[unit].unit.source_origin_identity.as_str(),
                                             units[unit].unit.source.as_path()),
                    .domain   = domain.clone(),
                    .kind     = BuildArtifactKind::Source,
                    .path     = Some(units[unit].unit.source.clone()),
                    .initially_ready = true,
                })));
        }
        result.object_artifacts.emplace_back(rstd_try(result.graph.add_artifact(BuildArtifactSpec {
            .identity = rstd::format("object:{}:{}",
                                     units[unit].source_content_identity.as_str(),
                                     units[unit].unit.context->id.as_str()),
            .domain   = domain.clone(),
            .kind     = BuildArtifactKind::Object,
            .path     = Some(units[unit].unit.object.clone()),
        })));
        const auto* bmi = cpp::unit_bmi(units[unit].unit);
        if (bmi == nullptr) {
            result.bmi_artifacts.emplace_back();
        } else {
            result.bmi_artifacts.push(Some(rstd_try(result.graph.add_artifact(BuildArtifactSpec {
                .identity =
                    rstd::format("bmi:{}:{}", bmi->key.value.as_str(), bmi->logical_name.as_str()),
                .domain = domain.clone(),
                .kind   = BuildArtifactKind::Bmi,
                .path   = Some(bmi->path.clone()),
            }))));
        }
    }
    for (auto target : action_targets) {
        if (target >= package.targets.len() || result.target_artifacts[target].is_some()) continue;
        auto kind = target_output_kind(package.targets[target].artifact_kind);
        if (kind.is_none()) continue;
        result.target_artifacts[target] =
            Some(rstd_try(result.graph.add_artifact(BuildArtifactSpec {
                .identity = rstd::format(
                    "target:{}", lito::package::package_target_id_text(package.targets[target].id)),
                .domain = domain.clone(),
                .kind   = *kind,
                .path   = Some(target_output_path(layout, package.targets[target])),
            })));
    }
    for (auto unit = first_new_unit; unit < units.len(); ++unit) {
        auto inputs = Vec<BuildArtifactId>::make();
        inputs.emplace_back(result.source_artifacts[unit]);
        for (auto prerequisite : compile.nodes[unit].prerequisites) {
            if (result.bmi_artifacts[prerequisite].is_some())
                inputs.emplace_back(*result.bmi_artifacts[prerequisite]);
            else
                inputs.emplace_back(result.object_artifacts[prerequisite]);
        }
        auto outputs = Vec<BuildArtifactId>::make();
        outputs.emplace_back(result.object_artifacts[unit]);
        if (result.bmi_artifacts[unit].is_some()) {
            outputs.emplace_back(*result.bmi_artifacts[unit]);
        }
        result.compile_actions.emplace_back(rstd_try(result.graph.add_action(BuildActionSpec {
            .identity = rstd::format("compile:{}:{}",
                                     units[unit].source_content_identity.as_str(),
                                     units[unit].unit.context->id.as_str()),
            .domain   = domain.clone(),
            .kind     = BuildActionKind::Compile,
            .inputs   = rstd::move(inputs),
            .outputs  = rstd::move(outputs),
        })));
    }
    for (auto target : package_plan.target_order) {
        if (! contains_target(action_targets, target) || result.target_actions[target].is_some() ||
            result.target_artifacts[target].is_none()) {
            continue;
        }
        auto inputs = Vec<BuildArtifactId>::make();
        for (auto unit : target_units[target]) inputs.emplace_back(result.object_artifacts[unit]);
        for (const auto& input : package_plan.link_inputs[target]) {
            if (! input.is_Target()) continue;
            auto dependency = input.as_Target().target;
            if (result.target_artifacts[dependency].is_none()) {
                return Err(BuildError::Message(rstd::format(
                    "target '{}' consumes dependency '{}' without a link artifact",
                    lito::package::package_target_id_text(package.targets[target].id),
                    lito::package::package_target_id_text(package.targets[dependency].id))));
            }
            inputs.emplace_back(*result.target_artifacts[dependency]);
        }
        if (package.targets[target].artifact_kind == cpp::ArtifactKind::TestExecutable) {
            for (auto candidate : package_plan.target_order) {
                const auto& attachment = package.targets[candidate].test_attachment;
                if (attachment.is_none() || attachment->test_target != package.targets[target].id) {
                    continue;
                }
                if (result.target_artifacts[candidate].is_none()) {
                    return Err(BuildError::Message(rstd::format(
                        "test target '{}' has no attachment archive artifact",
                        lito::package::package_target_id_text(package.targets[target].id))));
                }
                inputs.emplace_back(*result.target_artifacts[candidate]);
            }
        }
        auto outputs = Vec<BuildArtifactId>::make();
        outputs.emplace_back(*result.target_artifacts[target]);
        auto kind =
            package.targets[target].artifact_kind == cpp::ArtifactKind::StaticLibrary ||
                    package.targets[target].artifact_kind == cpp::ArtifactKind::CompilerPlugin ||
                    package.targets[target].artifact_kind == cpp::ArtifactKind::ProcMacroProvider ||
                    package.targets[target].artifact_kind ==
                        cpp::ArtifactKind::TestAttachmentArchive
                ? BuildActionKind::Archive
                : BuildActionKind::Link;
        result.target_actions[target] = Some(rstd_try(result.graph.add_action(BuildActionSpec {
            .identity =
                rstd::format("{}:{}",
                             kind == BuildActionKind::Archive ? "archive"_str : "link"_str,
                             lito::package::package_target_id_text(package.targets[target].id)),
            .domain  = domain.clone(),
            .kind    = kind,
            .inputs  = rstd::move(inputs),
            .outputs = rstd::move(outputs),
        })));
    }
    return result.graph.validate();
}

} // namespace lito
