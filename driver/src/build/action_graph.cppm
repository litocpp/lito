export module lito.driver:build.action_graph;

import rstd;
import lito.core;
import :build.error;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

struct ExecutionDomainId {
    String value;

    auto clone() const -> ExecutionDomainId { return ExecutionDomainId { .value = value.clone() }; }
};

enum class BuildArtifactKind
{
    Source,
    External,
    Generated,
    Object,
    Bmi,
    Archive,
    SharedLibrary,
    Executable,
};

enum class BuildActionKind
{
    Compile,
    Archive,
    Link,
    RunTool,
    Write,
    Copy,
    Transform,
};

enum class BuildActionState
{
    Declared,
    Ready,
    Running,
    Succeeded,
    Failed,
    Blocked,
};

struct BuildArtifactId {
    usize value {};

    friend auto operator==(BuildArtifactId, BuildArtifactId) noexcept -> bool = default;
};

struct BuildActionId {
    usize value {};

    friend auto operator==(BuildActionId, BuildActionId) noexcept -> bool = default;
};

struct BuildArtifactSpec {
    String            identity;
    ExecutionDomainId domain;
    BuildArtifactKind kind { BuildArtifactKind::Source };
    Option<PathBuf>   path;
    bool              initially_ready {};
};

struct BuildActionSpec {
    String               identity;
    ExecutionDomainId    domain;
    BuildActionKind      kind { BuildActionKind::Compile };
    Vec<BuildArtifactId> inputs;
    Vec<BuildArtifactId> outputs;
};

struct BuildArtifactNode {
    String                identity;
    ExecutionDomainId     domain;
    BuildArtifactKind     kind { BuildArtifactKind::Source };
    Option<PathBuf>       path;
    Option<BuildActionId> producer;
    Vec<BuildActionId>    consumers;
    bool                  ready {};
};

struct BuildActionNode {
    String               identity;
    ExecutionDomainId    domain;
    BuildActionKind      kind { BuildActionKind::Compile };
    Vec<BuildArtifactId> inputs;
    Vec<BuildArtifactId> outputs;
    Vec<BuildActionId>   prerequisites;
    Vec<BuildActionId>   dependents;
    BuildActionState     state { BuildActionState::Declared };
};

class BuildActionGraph {
public:
    auto add_artifact(BuildArtifactSpec spec) -> BuildResult<BuildArtifactId>;
    auto add_action(BuildActionSpec spec) -> BuildResult<BuildActionId>;
    auto validate() const -> BuildResult<empty>;
    auto ready_actions() const -> Vec<BuildActionId>;
    auto mark_running(BuildActionId id) -> BuildResult<empty>;
    auto mark_succeeded(BuildActionId id) -> BuildResult<empty>;
    auto mark_failed(BuildActionId id) -> BuildResult<empty>;

    auto artifact(BuildArtifactId id) const noexcept -> Option<ref<BuildArtifactNode>>;
    auto action(BuildActionId id) const noexcept -> Option<ref<BuildActionNode>>;
    auto artifacts() const noexcept -> ref<Vec<BuildArtifactNode>> {
        return ref<Vec<BuildArtifactNode>>::from_raw_parts(&artifacts_);
    }
    auto actions() const noexcept -> ref<Vec<BuildActionNode>> {
        return ref<Vec<BuildActionNode>>::from_raw_parts(&actions_);
    }

private:
    Vec<BuildArtifactNode> artifacts_;
    Vec<BuildActionNode>   actions_;
};

} // namespace lito

namespace lito
{

template<typename T>
auto action_graph_failure(String message) -> BuildResult<T> {
    return Err(BuildError::Message(rstd::move(message)));
}

template<typename T>
auto action_graph_failure(ref<str> message) -> BuildResult<T> {
    return action_graph_failure<T>(String::make(message));
}

auto artifact_kind_name(BuildArtifactKind kind) noexcept -> ref<str> {
    switch (kind) {
    case BuildArtifactKind::Source: return "source"_str;
    case BuildArtifactKind::External: return "external"_str;
    case BuildArtifactKind::Generated: return "generated"_str;
    case BuildArtifactKind::Object: return "object"_str;
    case BuildArtifactKind::Bmi: return "bmi"_str;
    case BuildArtifactKind::Archive: return "archive"_str;
    case BuildArtifactKind::SharedLibrary: return "shared-library"_str;
    case BuildArtifactKind::Executable: return "executable"_str;
    }
    return "unknown"_str;
}

auto action_kind_name(BuildActionKind kind) noexcept -> ref<str> {
    switch (kind) {
    case BuildActionKind::Compile: return "compile"_str;
    case BuildActionKind::Archive: return "archive"_str;
    case BuildActionKind::Link: return "link"_str;
    case BuildActionKind::RunTool: return "run-tool"_str;
    case BuildActionKind::Write: return "write"_str;
    case BuildActionKind::Copy: return "copy"_str;
    case BuildActionKind::Transform: return "transform"_str;
    }
    return "unknown"_str;
}

auto same_path(const Option<PathBuf>& left, const Option<PathBuf>& right) noexcept -> bool {
    if (left.is_some() != right.is_some()) return false;
    return left.is_none() || left->as_path() == right->as_path();
}

auto contains_action(const Vec<BuildActionId>& values, BuildActionId value) noexcept -> bool {
    for (auto candidate : values)
        if (candidate == value) return true;
    return false;
}

auto action_inputs_ready(const Vec<BuildArtifactNode>& artifacts,
                         const BuildActionNode&        action) noexcept -> bool {
    for (auto input : action.inputs)
        if (! artifacts[input.value].ready) return false;
    return true;
}

auto action_prerequisites_succeeded(const Vec<BuildActionNode>& actions,
                                    const BuildActionNode&      action) noexcept -> bool {
    for (auto prerequisite : action.prerequisites)
        if (actions[prerequisite.value].state != BuildActionState::Succeeded) return false;
    return true;
}

auto same_artifacts(const Vec<BuildArtifactId>& left, const Vec<BuildArtifactId>& right) noexcept
    -> bool {
    if (left.len() != right.len()) return false;
    for (usize index {}; index < left.len(); ++index)
        if (left[index] != right[index]) return false;
    return true;
}

auto BuildActionGraph::add_artifact(BuildArtifactSpec spec) -> BuildResult<BuildArtifactId> {
    if (spec.identity.is_empty() || spec.domain.value.is_empty()) {
        return action_graph_failure<BuildArtifactId>(
            "build artifact identity and execution domain must not be empty"_str);
    }
    for (usize index {}; index < artifacts_.len(); ++index) {
        const auto& existing = artifacts_[index];
        if (existing.identity != spec.identity.as_str() ||
            existing.domain.value != spec.domain.value.as_str()) {
            continue;
        }
        if (existing.kind != spec.kind || ! same_path(existing.path, spec.path)) {
            return action_graph_failure<BuildArtifactId>(
                rstd::format("artifact '{}' in execution domain '{}' has conflicting declarations",
                             spec.identity.as_str(),
                             spec.domain.value.as_str()));
        }
        if (spec.initially_ready) artifacts_[index].ready = true;
        return Ok(BuildArtifactId { .value = index });
    }
    auto id = BuildArtifactId { .value = artifacts_.len() };
    artifacts_.push(BuildArtifactNode {
        .identity  = rstd::move(spec.identity),
        .domain    = rstd::move(spec.domain),
        .kind      = spec.kind,
        .path      = rstd::move(spec.path),
        .consumers = Vec<BuildActionId>::make(),
        .ready     = spec.initially_ready,
    });
    return Ok(id);
}

auto BuildActionGraph::add_action(BuildActionSpec spec) -> BuildResult<BuildActionId> {
    if (spec.identity.is_empty() || spec.domain.value.is_empty() || spec.outputs.is_empty()) {
        return action_graph_failure<BuildActionId>(
            "build action identity, execution domain and outputs must not be empty"_str);
    }
    for (auto input : spec.inputs) {
        if (input.value >= artifacts_.len()) {
            return action_graph_failure<BuildActionId>(
                rstd::format("{} action '{}' references an unknown input artifact",
                             action_kind_name(spec.kind),
                             spec.identity.as_str()));
        }
        if ((spec.kind == BuildActionKind::Compile || spec.kind == BuildActionKind::Archive ||
             spec.kind == BuildActionKind::Link) &&
            artifacts_[input.value].domain.value != spec.domain.value.as_str()) {
            return action_graph_failure<BuildActionId>(rstd::format(
                "{} action '{}' cannot consume artifact '{}' from execution domain '{}'",
                action_kind_name(spec.kind),
                spec.identity.as_str(),
                artifacts_[input.value].identity.as_str(),
                artifacts_[input.value].domain.value.as_str()));
        }
    }
    for (auto output : spec.outputs) {
        if (output.value >= artifacts_.len()) {
            return action_graph_failure<BuildActionId>(
                rstd::format("{} action '{}' references an unknown output artifact",
                             action_kind_name(spec.kind),
                             spec.identity.as_str()));
        }
        if (artifacts_[output.value].domain.value != spec.domain.value.as_str()) {
            return action_graph_failure<BuildActionId>(rstd::format(
                "{} action '{}' and output artifact '{}' use different execution domains",
                action_kind_name(spec.kind),
                spec.identity.as_str(),
                artifacts_[output.value].identity.as_str()));
        }
    }
    for (usize index {}; index < actions_.len(); ++index) {
        const auto& existing = actions_[index];
        if (existing.identity != spec.identity.as_str() ||
            existing.domain.value != spec.domain.value.as_str()) {
            continue;
        }
        if (existing.kind != spec.kind || ! same_artifacts(existing.inputs, spec.inputs) ||
            ! same_artifacts(existing.outputs, spec.outputs)) {
            return action_graph_failure<BuildActionId>(
                rstd::format("action '{}' in execution domain '{}' has conflicting declarations",
                             spec.identity.as_str(),
                             spec.domain.value.as_str()));
        }
        return Ok(BuildActionId { .value = index });
    }

    auto id = BuildActionId { .value = actions_.len() };
    for (auto output : spec.outputs) {
        auto& artifact = artifacts_[output.value];
        if (artifact.producer.is_some()) {
            return action_graph_failure<BuildActionId>(
                rstd::format("artifact '{}' already has a producer", artifact.identity.as_str()));
        }
        if (artifact.ready) {
            return action_graph_failure<BuildActionId>(rstd::format(
                "produced artifact '{}' cannot be initially ready", artifact.identity.as_str()));
        }
    }

    auto prerequisites = Vec<BuildActionId>::make();
    for (auto input : spec.inputs) {
        auto& artifact = artifacts_[input.value];
        if (! contains_action(artifact.consumers, id)) artifact.consumers.emplace_back(id);
        if (artifact.producer.is_some() && ! contains_action(prerequisites, *artifact.producer)) {
            prerequisites.emplace_back(*artifact.producer);
        }
    }
    actions_.push(BuildActionNode {
        .identity      = rstd::move(spec.identity),
        .domain        = rstd::move(spec.domain),
        .kind          = spec.kind,
        .inputs        = rstd::move(spec.inputs),
        .outputs       = rstd::move(spec.outputs),
        .prerequisites = rstd::move(prerequisites),
        .dependents    = Vec<BuildActionId>::make(),
    });
    for (auto output : actions_[id.value].outputs) {
        auto& artifact    = artifacts_[output.value];
        artifact.producer = Some(id);
        for (auto consumer : artifact.consumers) {
            if (consumer != id && ! contains_action(actions_[consumer.value].prerequisites, id)) {
                actions_[consumer.value].prerequisites.emplace_back(id);
            }
        }
    }
    for (auto prerequisite : actions_[id.value].prerequisites) {
        if (! contains_action(actions_[prerequisite.value].dependents, id)) {
            actions_[prerequisite.value].dependents.emplace_back(id);
        }
    }
    for (auto dependent = BuildActionId {}; dependent.value < actions_.len(); ++dependent.value) {
        if (contains_action(actions_[dependent.value].prerequisites, id) &&
            ! contains_action(actions_[id.value].dependents, dependent)) {
            actions_[id.value].dependents.emplace_back(dependent);
        }
    }
    if (action_inputs_ready(artifacts_, actions_[id.value]) &&
        action_prerequisites_succeeded(actions_, actions_[id.value])) {
        actions_[id.value].state = BuildActionState::Ready;
    }
    return Ok(id);
}

auto BuildActionGraph::validate() const -> BuildResult<empty> {
    auto indegree = Vec<usize>::with_capacity(actions_.len());
    auto ready    = Vec<BuildActionId>::make();
    for (usize index {}; index < actions_.len(); ++index) {
        indegree.push(actions_[index].prerequisites.len());
        if (actions_[index].prerequisites.is_empty()) {
            ready.push(BuildActionId { .value = index });
        }
        for (auto input : actions_[index].inputs) {
            const auto& artifact = artifacts_[input.value];
            if (artifact.producer.is_none() && ! artifact.ready) {
                return action_graph_failure<empty>(
                    rstd::format("{} action '{}' consumes artifact '{}' without a producer",
                                 action_kind_name(actions_[index].kind),
                                 actions_[index].identity.as_str(),
                                 artifact.identity.as_str()));
            }
        }
    }
    auto visited = usize {};
    while (! ready.is_empty()) {
        auto current = rstd::move(ready.pop()).unwrap_unchecked();
        ++visited;
        for (auto dependent : actions_[current.value].dependents) {
            if (indegree[dependent.value] == usize {}) {
                return action_graph_failure<empty>(
                    "build action graph contains an invalid reverse edge"_str);
            }
            --indegree[dependent.value];
            if (indegree[dependent.value] == usize {}) ready.emplace_back(dependent);
        }
    }
    if (visited != actions_.len()) {
        return action_graph_failure<empty>("build action graph contains a dependency cycle"_str);
    }
    return Ok(empty {});
}

auto BuildActionGraph::artifact(BuildArtifactId id) const noexcept
    -> Option<ref<BuildArtifactNode>> {
    if (id.value >= artifacts_.len()) return None();
    return Some(ref<BuildArtifactNode>::from_raw_parts(rstd::addressof(artifacts_[id.value])));
}

auto BuildActionGraph::action(BuildActionId id) const noexcept -> Option<ref<BuildActionNode>> {
    if (id.value >= actions_.len()) return None();
    return Some(ref<BuildActionNode>::from_raw_parts(rstd::addressof(actions_[id.value])));
}

auto BuildActionGraph::ready_actions() const -> Vec<BuildActionId> {
    auto result = Vec<BuildActionId>::make();
    for (auto id = BuildActionId {}; id.value < actions_.len(); ++id.value)
        if (actions_[id.value].state == BuildActionState::Ready) result.emplace_back(id);
    return result;
}

auto BuildActionGraph::mark_running(BuildActionId id) -> BuildResult<empty> {
    if (id.value >= actions_.len()) {
        return action_graph_failure<empty>("cannot start an unknown build action"_str);
    }
    auto& action = actions_[id.value];
    if (action.state != BuildActionState::Ready) {
        return action_graph_failure<empty>(
            rstd::format("build action '{}' is not ready", action.identity.as_str()));
    }
    action.state = BuildActionState::Running;
    return Ok(empty {});
}

auto BuildActionGraph::mark_succeeded(BuildActionId id) -> BuildResult<empty> {
    if (id.value >= actions_.len()) {
        return action_graph_failure<empty>("cannot complete an unknown build action"_str);
    }
    auto& action = actions_[id.value];
    if (action.state != BuildActionState::Running && action.state != BuildActionState::Ready) {
        return action_graph_failure<empty>(
            rstd::format("build action '{}' is not running", action.identity.as_str()));
    }
    action.state = BuildActionState::Succeeded;
    for (auto output : action.outputs) artifacts_[output.value].ready = true;
    for (auto dependent : action.dependents) {
        auto& node = actions_[dependent.value];
        if (node.state != BuildActionState::Declared) continue;
        if (action_inputs_ready(artifacts_, node) &&
            action_prerequisites_succeeded(actions_, node)) {
            node.state = BuildActionState::Ready;
        }
    }
    return Ok(empty {});
}

auto BuildActionGraph::mark_failed(BuildActionId id) -> BuildResult<empty> {
    if (id.value >= actions_.len()) {
        return action_graph_failure<empty>("cannot fail an unknown build action"_str);
    }
    auto& action = actions_[id.value];
    if (action.state != BuildActionState::Running && action.state != BuildActionState::Ready) {
        return action_graph_failure<empty>(
            rstd::format("build action '{}' is not running", action.identity.as_str()));
    }
    action.state = BuildActionState::Failed;
    auto pending = action.dependents.clone();
    while (! pending.is_empty()) {
        auto  dependent = rstd::move(pending.pop()).unwrap_unchecked();
        auto& node      = actions_[dependent.value];
        if (node.state == BuildActionState::Succeeded || node.state == BuildActionState::Failed ||
            node.state == BuildActionState::Blocked) {
            continue;
        }
        node.state = BuildActionState::Blocked;
        for (auto nested : node.dependents) pending.emplace_back(nested);
    }
    return Ok(empty {});
}

} // namespace lito
