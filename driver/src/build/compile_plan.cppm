module lito.driver:build.compile_plan;

import rstd;
import lito.core;
import lito.cpp;
import :build.event;
import :build.request;
import :build.artifact;
import :build.documentation;
import :build.error;
import :build.compile_executor;
import lito.toolchain.common;
import lito.system;
import :cache;
import lito.toolchain;
import :build.layout;
import :build.compile_test;
import :build.profiling;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

namespace lito
{

enum class CompileNodeStatus
{
    Pending,
    Ready,
    Running,
    Succeeded,
    Failed,
    Blocked,
};

struct CompileNodeRuntime {
    CompileNodeStatus            status { CompileNodeStatus::Pending };
    usize                        remaining {};
    Option<CacheDecision>        decision;
    Option<CompileTestExecution> compile_test;
};

auto next_ready(const Vec<CompileNodeRuntime>& runtime) -> Option<cpp::UnitId> {
    for (auto unit = cpp::UnitId {}; unit < runtime.len(); ++unit) {
        if (runtime[unit].status == CompileNodeStatus::Ready) return Some(unit);
    }
    return None();
}

auto block_node(cpp::UnitId                  unit,
                const Vec<Vec<cpp::UnitId>>& dependents,
                Vec<CompileNodeRuntime>&     runtime,
                usize&                       terminal,
                CompileExecutionStatistics&  statistics) -> void {
    if (runtime[unit].status != CompileNodeStatus::Pending &&
        runtime[unit].status != CompileNodeStatus::Ready) {
        return;
    }
    runtime[unit].status = CompileNodeStatus::Blocked;
    ++terminal;
    ++statistics.blocked;
    for (auto dependent : dependents[unit]) {
        block_node(dependent, dependents, runtime, terminal, statistics);
    }
}

auto fail_node(cpp::UnitId                  unit,
               BuildError                   error,
               const Vec<Vec<cpp::UnitId>>& dependents,
               Vec<CompileNodeRuntime>&     runtime,
               Vec<Option<BuildError>>&     errors,
               usize&                       terminal,
               CompileExecutionStatistics&  statistics) -> void {
    runtime[unit].status = CompileNodeStatus::Failed;
    errors[unit]         = Some(rstd::move(error));
    ++terminal;
    ++statistics.failed;
    for (auto dependent : dependents[unit]) {
        block_node(dependent, dependents, runtime, terminal, statistics);
    }
}

auto succeed_node(cpp::UnitId                  unit,
                  const Vec<Vec<cpp::UnitId>>& dependents,
                  Vec<CompileNodeRuntime>&     runtime,
                  usize&                       terminal) -> BuildResult<empty> {
    runtime[unit].status = CompileNodeStatus::Succeeded;
    ++terminal;
    for (auto dependent : dependents[unit]) {
        if (runtime[dependent].status == CompileNodeStatus::Blocked) continue;
        if (runtime[dependent].remaining == usize {}) {
            return compile_failure<empty>("compile DAG prerequisite underflow"_str);
        }
        --runtime[dependent].remaining;
        if (runtime[dependent].remaining == usize {}) {
            runtime[dependent].status = CompileNodeStatus::Ready;
        }
    }
    return Ok(empty {});
}

auto emit_compile_event(const Option<BuildEventSink>& observer,
                        const cpp::PackageSpec&       package,
                        const Vec<cpp::PreparedUnit>& units,
                        cpp::UnitId                   unit,
                        BuildEventKind                kind,
                        Option<BuildProgress>         progress = None()) noexcept -> void {
    if (observer.is_none() || observer->notify == nullptr) return;
    auto target_name = String::make();
    auto target      = cpp::project_target(units[unit].unit);
    if (target.is_some()) {
        target_name = lito::package::package_target_id_text(package.targets[*target].id);
    } else {
        auto module = cpp::standard_library_module(units[unit].unit);
        target_name = rstd::format("standard-library::{}", (*module)->logical_name.as_str());
    }
    observer->notify(observer->context,
                     BuildEvent {
                         .kind     = kind,
                         .target   = target_name.as_str(),
                         .path     = units[unit].unit.source.as_path(),
                         .progress = rstd::move(progress),
                     });
}

struct CompileNodePlan {
    Vec<cpp::UnitId>          prerequisites;
    Vec<cpp::UnitId>          dependents;
    Vec<DependencyArtifact>   dependencies;
    Option<CompileInvocation> invocation;
};

struct CompilePlan {
    Vec<CompileNodePlan> nodes;
};

struct CompilePlanRetainedBytes {
    usize total {};
    usize invocations {};
    usize dependencies {};
};

auto compile_plan_retained_bytes(const CompilePlan& plan) noexcept -> CompilePlanRetainedBytes {
    auto result = CompilePlanRetainedBytes {
        .total = plan.nodes.capacity() * usize(sizeof(CompileNodePlan)),
    };
    for (const auto& node : plan.nodes) {
        result.total += node.prerequisites.capacity() * usize(sizeof(cpp::UnitId)) +
                        node.dependents.capacity() * usize(sizeof(cpp::UnitId)) +
                        node.dependencies.capacity() * usize(sizeof(DependencyArtifact));
        for (const auto& dependency : node.dependencies) {
            result.dependencies += dependency.retained_bytes();
        }
        result.dependencies += node.dependencies.capacity() * usize(sizeof(DependencyArtifact));
        if (node.invocation.is_some()) {
            result.invocations += node.invocation->retained_bytes();
        }
    }
    result.total += result.invocations + result.dependencies;
    return result;
}

struct MaterializedBuildActions {
    CompilePlan                 compile;
    Vec<DocumentationBuildUnit> documentation;
};

struct CompileExecutionResult {
    usize                               compiled {};
    usize                               reused {};
    Vec<Option<CachedArtifactIdentity>> object_identities;
    Vec<CompileTestExecution>           compile_tests;
    CompileExecutionStatistics          statistics;
    BuildTimingReport                   timing;
};

auto documentation_unit_kind(const cpp::ScanResult& scan) -> DocumentationUnitKind {
    if (! scan.language.is_Cpp()) return DocumentationUnitKind::TranslationUnit;
    const auto& facts = scan.language.as_Cpp().facts;
    if (facts.provided.is_some()) {
        if (facts.provided->logical_name.as_str().contains(":"_str)) {
            return DocumentationUnitKind::ModulePartition;
        }
        return facts.provided->is_interface ? DocumentationUnitKind::ModuleInterface
                                            : DocumentationUnitKind::ModulePartition;
    }
    if (facts.implementation_module.is_some()) {
        return DocumentationUnitKind::ModuleImplementation;
    }
    return DocumentationUnitKind::TranslationUnit;
}

auto materialize_documentation_units(const cpp::PackageSpec&               package,
                                     const Vec<cpp::PreparedUnit>&         units,
                                     const Vec<cpp::ScanResult>&           scans,
                                     const CompilePlan&                    plan,
                                     slice<lito::package::PackageTargetId> selected_targets)
    -> BuildResult<Vec<DocumentationBuildUnit>> {
    if (units.len() != scans.len() || units.len() != plan.nodes.len()) {
        return compile_failure<Vec<DocumentationBuildUnit>>(
            "documentation view inputs have inconsistent lengths"_str);
    }
    auto result = Vec<DocumentationBuildUnit>::with_capacity(units.len());
    for (auto unit = cpp::UnitId {}; unit < units.len(); ++unit) {
        auto target = cpp::project_target(units[unit].unit);
        if (target.is_none()) continue;
        auto selected = false;
        for (const auto& candidate : selected_targets) {
            if (candidate == package.targets[*target].id) {
                selected = true;
                break;
            }
        }
        if (! selected) continue;
        if (plan.nodes[unit].invocation.is_none()) {
            return compile_failure<Vec<DocumentationBuildUnit>>(
                "documentation view received an unmaterialized invocation"_str);
        }
        auto logical_module = Option<String> {};
        auto is_interface   = false;
        if (scans[unit].language.is_Cpp()) {
            const auto& facts = scans[unit].language.as_Cpp().facts;
            if (facts.provided.is_some()) {
                logical_module = Some(facts.provided->logical_name.clone());
                is_interface   = facts.provided->is_interface;
            } else if (facts.implementation_module.is_some()) {
                logical_module = Some(facts.implementation_module->clone());
            }
        }
        auto dependencies =
            Vec<DocumentationBmiDependency>::with_capacity(plan.nodes[unit].dependencies.len());
        for (const auto& dependency : plan.nodes[unit].dependencies) {
            dependencies.push(DocumentationBmiDependency {
                .logical_name      = dependency.logical_name.clone(),
                .artifact_identity = dependency.artifact.clone(),
                .path              = dependency.path.clone(),
            });
        }
        result.push(DocumentationBuildUnit {
            .target           = package.targets[*target].id.clone(),
            .package_root     = package.targets[*target].root.clone(),
            .source           = units[unit].unit.source.clone(),
            .relative_source  = units[unit].unit.relative_source.clone(),
            .kind             = documentation_unit_kind(scans[unit]),
            .is_interface     = is_interface,
            .logical_module   = rstd::move(logical_module),
            .root_module      = package.targets[*target].module_affiliation.clone(),
            .source_identity  = units[unit].source_content_identity.clone(),
            .invocation       = plan.nodes[unit].invocation->clone(),
            .bmi_dependencies = rstd::move(dependencies),
        });
    }
    return Ok(rstd::move(result));
}

auto materialize_compile_plan(const cpp::PackageSpec&         package,
                              const BuildLayout&              layout,
                              const ClangToolchain&           toolchain,
                              const cpp::BmiFormatIdentity&   bmi_format,
                              Vec<cpp::PreparedUnit>&         units,
                              const Vec<cpp::ScanResult>&     scans,
                              cpp::ResolvedSemanticBuildGraph semantics)
    -> BuildResult<CompilePlan> {
    if (scans.len() != units.len() || semantics.direct_inputs.len() != units.len() ||
        semantics.resolved_inputs.len() != units.len() ||
        semantics.public_inputs.len() != units.len() ||
        semantics.compile_order.len() != units.len() ||
        semantics.c_units.len() + semantics.cpp_units.len() != units.len()) {
        return compile_failure<CompilePlan>("compile plan inputs have inconsistent lengths"_str);
    }

    auto invocations  = Vec<Option<CompileInvocation>>::with_capacity(units.len());
    auto dependencies = Vec<Option<Vec<DependencyArtifact>>>::with_capacity(units.len());
    auto dependents   = Vec<Vec<cpp::UnitId>>::with_capacity(units.len());
    for (auto unit = cpp::UnitId {}; unit < units.len(); ++unit) {
        invocations.emplace_back();
        dependencies.emplace_back();
        dependents.emplace_back();
    }
    for (auto consumer = cpp::UnitId {}; consumer < units.len(); ++consumer) {
        for (auto provider : semantics.direct_inputs[consumer]) {
            if (provider >= units.len()) {
                return compile_failure<CompilePlan>("compile DAG contains an invalid provider"_str);
            }
            dependents[provider].emplace_back(consumer);
        }
    }

    auto format_identity = cpp::bmi_format_identity(bmi_format);
    auto format_key      = cpp::bmi_format_key(bmi_format);
    for (auto unit : semantics.compile_order) {
        if (unit >= units.len()) {
            return compile_failure<CompilePlan>("compile order contains an invalid unit"_str);
        }
        auto direct_artifacts    = Vec<DependencyArtifact>::make();
        auto recipe_dependencies = Vec<cpp::BmiRecipeDependency>::make();
        for (auto input : semantics.direct_inputs[unit]) {
            const auto* input_bmi = cpp::unit_bmi(units[input].unit);
            if (! scans[input].language.is_Cpp() ||
                scans[input].language.as_Cpp().facts.provided.is_none() || input_bmi == nullptr) {
                return compile_failure<CompilePlan>(
                    rstd::format("module dependency '{}' has no resolved BMI artifact",
                                 units[input].unit.source.as_path()));
            }
            const auto& artifact = *input_bmi;
            direct_artifacts.push(DependencyArtifact {
                .logical_name = artifact.logical_name.clone(),
                .artifact     = artifact.key.value.clone(),
                .path         = artifact.path.clone(),
            });
            recipe_dependencies.push(cpp::BmiRecipeDependency {
                .logical_name = artifact.logical_name.clone(),
                .artifact_key = artifact.key.value.clone(),
            });
        }

        const auto* scan_cpp = scans[unit].language.is_Cpp()
                                   ? rstd::addressof(scans[unit].language.as_Cpp().facts)
                                   : nullptr;
        if (scan_cpp != nullptr && scan_cpp->provided.is_some()) {
            if (! units[unit].unit.context->language.is_Cpp()) {
                return compile_failure<CompilePlan>("C source unexpectedly provided a BMI"_str);
            }
            const auto& cpp_context       = units[unit].unit.context->language.as_Cpp();
            auto        source_identity   = units[unit].unit.source.as_path().to_str();
            auto        relative_identity = units[unit].unit.relative_source.as_path().to_str();
            if (source_identity.is_none() || relative_identity.is_none()) {
                return compile_failure<CompilePlan>("BMI provider path is not valid UTF-8"_str);
            }
            auto target = cpp::project_target(units[unit].unit);
            auto provider_identity =
                target.is_some()
                    ? rstd::format(
                          "{}:{}:{}",
                          lito::package::package_target_id_text(package.targets[*target].id)
                              .as_str(),
                          *relative_identity,
                          units[unit].unit.context->id.as_str())
                    : rstd::format("standard-library:{}:{}:{}",
                                   units[unit]
                                       .unit.owner.as_StandardLibrary()
                                       .module.manifest_identity.as_str(),
                                   scan_cpp->provided->logical_name.as_str(),
                                   units[unit].unit.standard_library_context_identity.as_str());
            auto key      = cpp::make_bmi_artifact_key(cpp::BmiRecipe {
                .request                 = cpp_context.bmi,
                .logical_name            = scan_cpp->provided->logical_name.clone(),
                .provider_identity       = provider_identity.clone(),
                .source_identity         = String::make(*source_identity),
                .source_content_identity = units[unit].source_content_identity.clone(),
                .cpp_context_identity    = cpp::cpp_compile_identity(cpp_context.options),
                .public_requirements_identity =
                    cpp::cpp_public_requirements_identity(cpp_context.public_requirements),
                .format_identity     = format_identity.clone(),
                .direct_dependencies = rstd::move(recipe_dependencies),
            });
            auto bmi_path = layout.bmi(
                format_key.as_str(), key.value.as_str(), scan_cpp->provided->logical_name.as_str());
            auto direct = Vec<cpp::BmiRecipeDependency>::with_capacity(direct_artifacts.len());
            for (const auto& dependency : direct_artifacts) {
                direct.push(cpp::BmiRecipeDependency {
                    .logical_name = dependency.logical_name.clone(),
                    .artifact_key = dependency.artifact.clone(),
                });
            }
            auto assigned = cpp::assign_unit_bmi(
                units[unit].unit,
                cpp::BmiArtifact {
                    .logical_name        = scan_cpp->provided->logical_name.clone(),
                    .provider_identity   = rstd::move(provider_identity),
                    .key                 = rstd::move(key),
                    .format              = bmi_format.clone(),
                    .request             = cpp_context.bmi,
                    .path                = rstd::move(bmi_path),
                    .direct_dependencies = rstd::move(direct),
                    .paired_object       = target.is_some() ? Some(units[unit].unit.object.clone())
                                                            : Option<PathBuf> {},
                });
            if (! assigned) {
                return compile_failure<CompilePlan>("C source unexpectedly provided a BMI"_str);
            }
        }

        auto module_dependencies = Vec<cpp::ModuleArtifactDependency>::make();
        for (auto input : semantics.resolved_inputs[unit]) {
            const auto* input_bmi = cpp::unit_bmi(units[input].unit);
            if (! scans[input].language.is_Cpp() ||
                scans[input].language.as_Cpp().facts.provided.is_none() || input_bmi == nullptr) {
                return compile_failure<CompilePlan>(
                    rstd::format("module dependency '{}' has no resolved BMI artifact",
                                 units[input].unit.source.as_path()));
            }
            const auto& artifact = *input_bmi;
            module_dependencies.push(cpp::ModuleArtifactDependency {
                .logical_name = artifact.logical_name.clone(),
                .artifact_key = cpp::BmiArtifactKey { .value = artifact.key.value.clone() },
                .path         = artifact.path.clone(),
            });
        }
        auto disposition = cpp::CppCompileDisposition::ObjectOnly;
        if (scan_cpp != nullptr && scan_cpp->provided.is_some()) {
            disposition = units[unit].unit.owner.is_StandardLibrary()
                              ? cpp::CppCompileDisposition::BmiOnly
                              : cpp::CppCompileDisposition::ObjectAndBmi;
        }
        auto invocation =
            toolchain.prepare_compile(units[unit], scans[unit], module_dependencies, disposition);
        if (invocation.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(invocation).unwrap_err()));
        }
        dependencies[unit] = Some(rstd::move(direct_artifacts));
        invocations[unit]  = Some(rstd::move(invocation).unwrap());
    }

    for (auto unit = cpp::UnitId {}; unit < units.len(); ++unit) {
        if (units[unit].unit.compile_test == nullptr) continue;
        if (! dependents[unit].is_empty()) {
            return compile_failure<CompilePlan>(
                rstd::format("compile-test source '{}' cannot provide an imported artifact",
                             units[unit].unit.source.as_path()));
        }
    }

    auto nodes = Vec<CompileNodePlan>::with_capacity(units.len());
    for (auto unit = cpp::UnitId {}; unit < units.len(); ++unit) {
        if (invocations[unit].is_none() || dependencies[unit].is_none()) {
            return compile_failure<CompilePlan>("compile plan left a unit unmaterialized"_str);
        }
        auto invocation = rstd::move(invocations[unit]).unwrap_unchecked();
        nodes.push(CompileNodePlan {
            .prerequisites = rstd::move(semantics.direct_inputs[unit]),
            .dependents    = rstd::move(dependents[unit]),
            .dependencies  = rstd::move(dependencies[unit]).unwrap_unchecked(),
            .invocation    = Some(rstd::move(invocation)),
        });
    }
    return Ok(CompilePlan { .nodes = rstd::move(nodes) });
}

auto materialize_build_actions(const cpp::PackageSpec&               package,
                               const BuildLayout&                    layout,
                               const ClangToolchain&                 toolchain,
                               const cpp::BmiFormatIdentity&         bmi_format,
                               Vec<cpp::PreparedUnit>&               units,
                               const Vec<cpp::ScanResult>&           scans,
                               cpp::ResolvedSemanticBuildGraph       semantics,
                               slice<lito::package::PackageTargetId> selected_targets,
                               BuildResultProjection                 projection)
    -> BuildResult<MaterializedBuildActions> {
    auto compile = materialize_compile_plan(
        package, layout, toolchain, bmi_format, units, scans, rstd::move(semantics));
    if (compile.is_err()) return Err(rstd::move(compile).unwrap_err());

    auto documentation = Vec<DocumentationBuildUnit>::make();
    if (projection.documentation) {
        auto materialized =
            materialize_documentation_units(package, units, scans, *compile, selected_targets);
        if (materialized.is_err()) return Err(rstd::move(materialized).unwrap_err());
        documentation = rstd::move(materialized).unwrap();
    }
    return Ok(MaterializedBuildActions {
        .compile       = rstd::move(compile).unwrap(),
        .documentation = rstd::move(documentation),
    });
}

auto compile_plan_prerequisite_closure(const CompilePlan& plan, const Vec<cpp::UnitId>& roots)
    -> Vec<u8> {
    auto selected = Vec<u8>::with_capacity(plan.nodes.len());
    for (auto unit = cpp::UnitId {}; unit < plan.nodes.len(); ++unit) selected.push(u8 {});
    auto pending = roots.clone();
    while (! pending.is_empty()) {
        const auto unit = rstd::move(pending.pop()).unwrap_unchecked();
        if (unit >= plan.nodes.len() || selected[unit] != u8 {}) continue;
        selected[unit] = u8(1);
        for (auto prerequisite : plan.nodes[unit].prerequisites) pending.emplace_back(prerequisite);
    }
    return selected;
}

auto compile_plan_available_prerequisites(const CompilePlan& plan, const Vec<cpp::UnitId>& roots)
    -> Vec<u8> {
    auto selected = compile_plan_prerequisite_closure(plan, roots);
    auto blocked  = Vec<u8>::with_capacity(plan.nodes.len());
    for (auto unit = cpp::UnitId {}; unit < plan.nodes.len(); ++unit) blocked.push(u8 {});
    auto pending = roots.clone();
    while (! pending.is_empty()) {
        const auto unit = rstd::move(pending.pop()).unwrap_unchecked();
        if (unit >= plan.nodes.len() || blocked[unit] != u8 {}) continue;
        blocked[unit] = u8(1);
        for (auto dependent : plan.nodes[unit].dependents) pending.emplace_back(dependent);
    }
    for (auto unit = cpp::UnitId {}; unit < plan.nodes.len(); ++unit) {
        if (blocked[unit] != u8 {}) selected[unit] = u8 {};
    }
    return selected;
}

auto compile_plan_remaining_selection(const CompilePlan& plan, const Vec<u8>& completed)
    -> Vec<u8> {
    auto selected = Vec<u8>::with_capacity(plan.nodes.len());
    for (auto unit = cpp::UnitId {}; unit < plan.nodes.len(); ++unit) {
        selected.push(unit < completed.len() && completed[unit] != u8 {} ? u8 {} : u8(1));
    }
    return selected;
}

auto compile_plan_invocation(CompilePlan& plan, cpp::UnitId unit) -> CompileInvocation* {
    if (unit >= plan.nodes.len() || plan.nodes[unit].invocation.is_none()) return nullptr;
    return rstd::addressof(*plan.nodes[unit].invocation);
}

struct CompileActionSubmission {
    bool completed {};
};

struct CompileActionCompletion {
    cpp::UnitId        unit {};
    Option<BuildError> error;
};

struct CompileActionSessionResult {
    usize                      compiled {};
    usize                      reused {};
    Vec<CompileTestExecution>  compile_tests;
    CompileExecutionStatistics statistics;
    BuildTimingReport          timing;
};

class CompileProgressTracker {
    usize   announced_ {};
    usize   total_ {};
    Vec<u8> counted_;

public:
    CompileProgressTracker(): counted_(Vec<u8>::make()) {}

    auto include(cpp::UnitId unit, usize unit_count) -> void {
        while (counted_.len() < unit_count) counted_.push(u8 {});
        if (counted_[unit] != u8 {}) return;
        counted_[unit] = u8(1);
        ++total_;
    }

    auto announce() noexcept -> BuildProgress {
        ++announced_;
        return BuildProgress { announced_, total_ };
    }
};

class CompileActionSession {
    const cpp::PackageSpec*              package_ {};
    Vec<cpp::PreparedUnit>*              units_ {};
    CompilePlan*                         plan_ {};
    Vec<Option<CachedArtifactIdentity>>* object_identities_ {};
    CompileCacheSession*                 cache_ {};
    ClangCompileExecutor                 compile_;
    const Option<BuildEventSink>*        observer_ {};
    CompileExecutor                      executor_;
    CompileProgressTracker*              progress_ {};
    Vec<Option<CacheDecision>>           decisions_;
    Vec<Option<CompileTestExecution>>    compile_tests_;
    CompileActionSessionResult           result_;
    rstd::time::Instant                  wall_started_;
    usize                                in_flight_ {};
    usize                                capacity_ { usize(1) };
    bool                                 finished_ {};

    CompileActionSession(const cpp::PackageSpec&              package,
                         Vec<cpp::PreparedUnit>&              units,
                         CompilePlan&                         plan,
                         Vec<Option<CachedArtifactIdentity>>& object_identities,
                         CompileCacheSession&                 cache,
                         ClangCompileExecutor                 compile,
                         const Option<BuildEventSink>&        observer,
                         CompileExecutor                      executor,
                         Vec<Option<CacheDecision>>           decisions,
                         Vec<Option<CompileTestExecution>>    compile_tests,
                         CompileActionSessionResult           result,
                         rstd::time::Instant                  wall_started,
                         usize                                capacity,
                         CompileProgressTracker&              progress)
        : package_(rstd::addressof(package)),
          units_(rstd::addressof(units)),
          plan_(rstd::addressof(plan)),
          object_identities_(rstd::addressof(object_identities)),
          cache_(rstd::addressof(cache)),
          compile_(compile),
          observer_(rstd::addressof(observer)),
          executor_(rstd::move(executor)),
          progress_(rstd::addressof(progress)),
          decisions_(rstd::move(decisions)),
          compile_tests_(rstd::move(compile_tests)),
          result_(rstd::move(result)),
          wall_started_(wall_started),
          capacity_(capacity) {}

public:
    CompileActionSession(CompileActionSession&&) noexcept                    = default;
    auto operator=(CompileActionSession&&) noexcept -> CompileActionSession& = delete;

    static auto create(const cpp::PackageSpec&              package,
                       Vec<cpp::PreparedUnit>&              units,
                       CompilePlan&                         plan,
                       const Vec<u8>&                       selection,
                       Vec<Option<CachedArtifactIdentity>>& object_identities,
                       CompileCacheSession&                 cache,
                       ClangCompileExecutor                 compile,
                       const Option<BuildEventSink>&        observer,
                       ResolvedCompileExecution             policy,
                       CompileProgressTracker& progress) -> BuildResult<CompileActionSession> {
        if (selection.len() != plan.nodes.len() || object_identities.len() != plan.nodes.len()) {
            return compile_failure<CompileActionSession>(
                "compile action session inputs have inconsistent lengths"_str);
        }
        auto selected_nodes = usize {};
        for (auto unit = cpp::UnitId {}; unit < plan.nodes.len(); ++unit)
            if (selection[unit] != u8 {}) ++selected_nodes;
        auto jobs = policy.jobs < selected_nodes ? policy.jobs : selected_nodes;
        if (jobs == usize {}) jobs = usize(1);
        auto capacity =
            policy.max_in_flight < selected_nodes ? policy.max_in_flight : selected_nodes;
        if (capacity == usize {}) capacity = usize(1);
        auto created = CompileExecutor::create(jobs, capacity);
        if (created.is_err()) return Err(rstd::move(created).unwrap_err());
        auto result = CompileActionSessionResult {
            .compile_tests = Vec<CompileTestExecution>::make(),
        };
        auto retained                           = compile_plan_retained_bytes(plan);
        result.statistics.jobs                  = jobs;
        result.statistics.max_in_flight         = capacity;
        result.statistics.plan_nodes            = selected_nodes;
        result.statistics.plan_retained_bytes   = retained.total;
        result.statistics.plan_invocation_bytes = retained.invocations;
        result.statistics.plan_dependency_bytes = retained.dependencies;
        auto decisions = Vec<Option<CacheDecision>>::with_capacity(plan.nodes.len());
        auto tests     = Vec<Option<CompileTestExecution>>::with_capacity(plan.nodes.len());
        for (auto unit = cpp::UnitId {}; unit < plan.nodes.len(); ++unit) {
            decisions.emplace_back();
            tests.emplace_back();
        }
        auto cache_retained = usize {};
        for (auto unit = cpp::UnitId {}; unit < plan.nodes.len(); ++unit) {
            if (selection[unit] == u8 {}) continue;
            if (plan.nodes[unit].invocation.is_none()) {
                return compile_failure<CompileActionSession>(
                    "compile action session contains an invocation that was already consumed"_str);
            }
            auto started = rstd::time::Instant::now();
            auto target  = cpp::project_target(units[unit].unit);
            auto target_identity =
                target.is_some()
                    ? lito::package::package_target_id_text(package.targets[*target].id)
                    : rstd::format(
                          "standard-library::{}",
                          units[unit].unit.owner.as_StandardLibrary().module.logical_name.as_str());
            auto decision = cache.evaluate(target_identity.as_str(),
                                           units[unit],
                                           units[unit].source_content_identity.as_str(),
                                           *plan.nodes[unit].invocation,
                                           plan.nodes[unit].dependencies);
            result.statistics.coordinator_work =
                result.statistics.coordinator_work.saturating_add(started.elapsed());
            if (decision.is_err()) {
                return Err(rstd::into<BuildError>(rstd::move(decision).unwrap_err()));
            }
            ++result.statistics.cache_evaluations;
            cache_retained += decision->retained_bytes();
            if (cache_retained > result.statistics.cache_retained_bytes_peak) {
                result.statistics.cache_retained_bytes_peak = cache_retained;
            }
            const auto* test = units[unit].unit.compile_test;
            if (! decision->current() ||
                (test != nullptr && test->outcome != lito::manifest::CompileTestOutcome::Success)) {
                progress.include(unit, plan.nodes.len());
            }
            decisions[unit] = Some(rstd::move(decision).unwrap());
        }
        return Ok(CompileActionSession(package,
                                       units,
                                       plan,
                                       object_identities,
                                       cache,
                                       compile,
                                       observer,
                                       rstd::move(created).unwrap(),
                                       rstd::move(decisions),
                                       rstd::move(tests),
                                       rstd::move(result),
                                       rstd::time::Instant::now(),
                                       capacity,
                                       progress));
    }

    auto has_capacity() const noexcept -> bool { return in_flight_ < capacity_; }
    auto has_in_flight() const noexcept -> bool { return in_flight_ != usize {}; }
    auto object_identities() const noexcept -> const Vec<Option<CachedArtifactIdentity>>& {
        return *object_identities_;
    }

    auto submit(cpp::UnitId unit) -> BuildResult<CompileActionSubmission> {
        if (! has_capacity() || unit >= plan_->nodes.len() || decisions_[unit].is_none() ||
            plan_->nodes[unit].invocation.is_none()) {
            return compile_failure<CompileActionSubmission>(
                "compile action is not available for submission"_str);
        }
        auto        started        = rstd::time::Instant::now();
        auto        cache_decision = rstd::move(decisions_[unit]).unwrap_unchecked();
        auto        target         = cpp::project_target((*units_)[unit].unit);
        const auto* test           = (*units_)[unit].unit.compile_test;
        if (cache_decision.current() && test == nullptr) {
            auto identity = cache_decision.object_identity();
            if (identity.is_some()) (*object_identities_)[unit] = Some((**identity).clone());
            ++result_.reused;
            ++result_.statistics.reused;
            emit_compile_event(*observer_, *package_, *units_, unit, BuildEventKind::Reuse);
            result_.statistics.coordinator_work =
                result_.statistics.coordinator_work.saturating_add(started.elapsed());
            return Ok(CompileActionSubmission { .completed = true });
        }
        if (cache_decision.current() && test != nullptr &&
            test->outcome == lito::manifest::CompileTestOutcome::Success) {
            auto execution = evaluate_compile_test(package_->targets[*target].id.package.as_str(),
                                                   *test,
                                                   (*units_)[unit].unit.source.as_path(),
                                                   CompileCommandResult {});
            auto recorded  = cache_->record_compile_test(
                cache_decision, (*units_)[unit].unit.compile_test_record->as_path(), execution);
            if (recorded.is_err()) {
                return Err(rstd::into<BuildError>(rstd::move(recorded).unwrap_err()));
            }
            compile_tests_[unit] = Some(rstd::move(execution));
            ++result_.reused;
            ++result_.statistics.reused;
            emit_compile_event(*observer_, *package_, *units_, unit, BuildEventKind::Reuse);
            result_.statistics.coordinator_work =
                result_.statistics.coordinator_work.saturating_add(started.elapsed());
            return Ok(CompileActionSubmission { .completed = true });
        }
        auto begun =
            test == nullptr
                ? cache_->begin_compile(cache_decision)
                : cache_->begin_compile_test(
                      cache_decision, (*units_)[unit].unit.compile_test_record->as_path(), *test);
        if (begun.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(begun).unwrap_err()));
        }
        decisions_[unit] = Some(rstd::move(cache_decision));
        auto invocation  = rstd::move(plan_->nodes[unit].invocation).unwrap_unchecked();
        auto submitted =
            executor_.submit(unit,
                             [compile = compile_, invocation = rstd::move(invocation)]() mutable
                                 -> ToolchainResult<CompileCommandResult> {
                                 return compile.execute(invocation);
                             });
        if (submitted.is_err()) return Err(rstd::move(submitted).unwrap_err());
        auto progress = progress_->announce();
        emit_compile_event(
            *observer_, *package_, *units_, unit, BuildEventKind::Compile, Some(progress));
        ++in_flight_;
        result_.statistics.coordinator_work =
            result_.statistics.coordinator_work.saturating_add(started.elapsed());
        return Ok(CompileActionSubmission {});
    }

    auto recv() -> BuildResult<CompileActionCompletion> {
        if (! has_in_flight()) {
            return compile_failure<CompileActionCompletion>(
                "compile action session has no task in flight"_str);
        }
        auto task = executor_.recv();
        if (task.is_err()) return Err(rstd::move(task).unwrap_err());
        --in_flight_;
        auto completed = rstd::move(task).unwrap();
        auto unit      = completed.node;
        if (unit >= plan_->nodes.len() || decisions_[unit].is_none()) {
            return compile_failure<CompileActionCompletion>(
                "compile task completion does not match a submitted action"_str);
        }
        auto started = rstd::time::Instant::now();
        if (completed.outcome.is_err()) {
            return Ok(CompileActionCompletion {
                .unit  = unit,
                .error = Some(rstd::into<BuildError>(rstd::move(completed.outcome).unwrap_err())),
            });
        }
        auto output   = rstd::move(completed.outcome).unwrap();
        auto decision = rstd::move(decisions_[unit]).unwrap_unchecked();
        result_.timing.record(BuildOperation::Compile, output.elapsed);
        auto        target = cpp::project_target((*units_)[unit].unit);
        const auto* test   = (*units_)[unit].unit.compile_test;
        if (test != nullptr) {
            auto execution = evaluate_compile_test(package_->targets[*target].id.package.as_str(),
                                                   *test,
                                                   (*units_)[unit].unit.source.as_path(),
                                                   rstd::move(output));
            if (execution.exit_code == i32 {}) {
                auto committed = cache_->commit_success((*units_)[unit], decision);
                if (committed.is_err()) {
                    return Ok(CompileActionCompletion {
                        .unit  = unit,
                        .error = Some(rstd::into<BuildError>(rstd::move(committed).unwrap_err())),
                    });
                }
            }
            auto recorded = cache_->record_compile_test(
                decision, (*units_)[unit].unit.compile_test_record->as_path(), execution);
            if (recorded.is_err()) {
                return Ok(CompileActionCompletion {
                    .unit  = unit,
                    .error = Some(rstd::into<BuildError>(rstd::move(recorded).unwrap_err())),
                });
            }
            compile_tests_[unit] = Some(rstd::move(execution));
            ++result_.compiled;
        } else {
            if (output.exit_code != i32 {}) {
                return Ok(CompileActionCompletion {
                    .unit  = unit,
                    .error = Some(BuildError::Toolchain(ToolchainError::Execution(
                        rstd::format("compile '{}'", (*units_)[unit].unit.source.as_path()),
                        output.exit_code,
                        String::make(),
                        rstd::move(output.standard_error)))),
                });
            }
            auto committed = cache_->commit_success((*units_)[unit], decision);
            if (committed.is_err()) {
                return Ok(CompileActionCompletion {
                    .unit  = unit,
                    .error = Some(rstd::into<BuildError>(rstd::move(committed).unwrap_err())),
                });
            }
            (*object_identities_)[unit] = rstd::move(committed).unwrap();
            ++result_.compiled;
        }
        result_.statistics.coordinator_work =
            result_.statistics.coordinator_work.saturating_add(started.elapsed());
        return Ok(CompileActionCompletion { .unit = unit });
    }

    auto finish() -> BuildResult<CompileActionSessionResult> {
        if (has_in_flight()) {
            return compile_failure<CompileActionSessionResult>(
                "compile action session still has tasks in flight"_str);
        }
        auto executor_statistics = executor_.statistics();
        executor_.finish();
        finished_                          = true;
        result_.statistics.tasks           = executor_statistics.tasks;
        result_.statistics.max_active      = executor_statistics.max_active;
        result_.statistics.ready_wait      = executor_statistics.ready_wait;
        result_.statistics.completion_wait = executor_statistics.completion_wait;
        result_.statistics.task_work       = executor_statistics.task_work;
        result_.statistics.wall            = wall_started_.elapsed();
        for (auto unit = cpp::UnitId {}; unit < compile_tests_.len(); ++unit) {
            if (compile_tests_[unit].is_some()) {
                result_.compile_tests.push(rstd::move(compile_tests_[unit]).unwrap_unchecked());
            }
        }
        return Ok(rstd::move(result_));
    }
};

auto execute_compile_plan_selection(const cpp::PackageSpec&             package,
                                    Vec<cpp::PreparedUnit>&             units,
                                    CompilePlan&                        plan,
                                    const Vec<u8>&                      selection,
                                    Vec<Option<CachedArtifactIdentity>> object_identities,
                                    CompileCacheSession&                cache,
                                    ClangCompileExecutor                compile,
                                    const Option<BuildEventSink>&       observer,
                                    ResolvedCompileExecution            policy)
    -> BuildResult<CompileExecutionResult> {
    if (selection.len() != plan.nodes.len() || object_identities.len() != plan.nodes.len()) {
        return compile_failure<CompileExecutionResult>(
            "compile selection inputs have inconsistent lengths"_str);
    }
    auto result = CompileExecutionResult {
        .object_identities = rstd::move(object_identities),
        .compile_tests     = Vec<CompileTestExecution>::make(),
    };
    auto selected_nodes = usize {};
    for (auto unit = cpp::UnitId {}; unit < plan.nodes.len(); ++unit)
        if (selection[unit] != u8 {}) ++selected_nodes;
    if (selected_nodes == usize {}) {
        auto retained                           = compile_plan_retained_bytes(plan);
        result.statistics.jobs                  = policy.jobs;
        result.statistics.max_in_flight         = policy.max_in_flight;
        result.statistics.plan_retained_bytes   = retained.total;
        result.statistics.plan_invocation_bytes = retained.invocations;
        result.statistics.plan_dependency_bytes = retained.dependencies;
        return Ok(rstd::move(result));
    }

    auto progress       = CompileProgressTracker {};
    auto session_result = CompileActionSession::create(package,
                                                       units,
                                                       plan,
                                                       selection,
                                                       result.object_identities,
                                                       cache,
                                                       compile,
                                                       observer,
                                                       policy,
                                                       progress);
    if (session_result.is_err()) return Err(rstd::move(session_result).unwrap_err());
    auto session = rstd::move(session_result).unwrap();

    auto runtime    = Vec<CompileNodeRuntime>::with_capacity(plan.nodes.len());
    auto errors     = Vec<Option<BuildError>>::with_capacity(plan.nodes.len());
    auto dependents = Vec<Vec<cpp::UnitId>>::with_capacity(plan.nodes.len());
    for (auto unit = cpp::UnitId {}; unit < plan.nodes.len(); ++unit) {
        const auto& node = plan.nodes[unit];
        if (selection[unit] == u8 {}) {
            runtime.push(CompileNodeRuntime { .status = CompileNodeStatus::Succeeded });
            errors.emplace_back();
            dependents.emplace_back();
            continue;
        }
        auto remaining = usize {};
        for (auto prerequisite : node.prerequisites)
            if (selection[prerequisite] != u8 {}) ++remaining;
        runtime.push(CompileNodeRuntime {
            .status = remaining == usize {} ? CompileNodeStatus::Ready : CompileNodeStatus::Pending,
            .remaining = remaining,
        });
        errors.emplace_back();
        auto selected_dependents = Vec<cpp::UnitId>::make();
        for (auto dependent : node.dependents)
            if (selection[dependent] != u8 {}) selected_dependents.emplace_back(dependent);
        dependents.push(rstd::move(selected_dependents));
    }

    auto terminal           = usize {};
    auto runtime_statistics = CompileExecutionStatistics {};
    while (terminal < selected_nodes) {
        while (session.has_capacity()) {
            auto selected = next_ready(runtime);
            if (selected.is_none()) break;
            auto unit            = *selected;
            runtime[unit].status = CompileNodeStatus::Running;
            auto submitted       = session.submit(unit);
            if (submitted.is_err()) {
                fail_node(unit,
                          rstd::move(submitted).unwrap_err(),
                          dependents,
                          runtime,
                          errors,
                          terminal,
                          runtime_statistics);
                continue;
            }
            if (! submitted->completed) continue;
            auto succeeded = succeed_node(unit, dependents, runtime, terminal);
            if (succeeded.is_err()) return Err(rstd::move(succeeded).unwrap_err());
        }

        if (session.has_in_flight()) {
            auto completed = session.recv();
            if (completed.is_err()) return Err(rstd::move(completed).unwrap_err());
            if (completed->unit >= runtime.len() ||
                runtime[completed->unit].status != CompileNodeStatus::Running) {
                return compile_failure<CompileExecutionResult>(
                    "compile action completion does not match a running node"_str);
            }
            if (completed->error.is_some()) {
                fail_node(completed->unit,
                          rstd::move(completed->error).unwrap(),
                          dependents,
                          runtime,
                          errors,
                          terminal,
                          runtime_statistics);
                continue;
            }
            auto succeeded = succeed_node(completed->unit, dependents, runtime, terminal);
            if (succeeded.is_err()) return Err(rstd::move(succeeded).unwrap_err());
            continue;
        }
        if (terminal != selected_nodes) {
            return compile_failure<CompileExecutionResult>(
                "compile DAG has pending nodes without a ready frontier"_str);
        }
    }

    auto finished = session.finish();
    if (finished.is_err()) return Err(rstd::move(finished).unwrap_err());
    result.compiled      = finished->compiled;
    result.reused        = finished->reused;
    result.compile_tests = rstd::move(finished->compile_tests);
    result.statistics    = finished->statistics;
    result.statistics.failed += runtime_statistics.failed;
    result.statistics.blocked += runtime_statistics.blocked;
    result.timing = rstd::move(finished->timing);
    for (auto unit = cpp::UnitId {}; unit < errors.len(); ++unit) {
        if (errors[unit].is_some()) return Err(rstd::move(errors[unit]).unwrap_unchecked());
    }
    return Ok(rstd::move(result));
}
auto execute_compile_plan(const cpp::PackageSpec&       package,
                          Vec<cpp::PreparedUnit>&       units,
                          CompilePlan                   plan,
                          CompileCacheSession&          cache,
                          ClangCompileExecutor          compile,
                          const Option<BuildEventSink>& observer,
                          ResolvedCompileExecution policy) -> BuildResult<CompileExecutionResult> {
    auto selection  = Vec<u8>::with_capacity(plan.nodes.len());
    auto identities = Vec<Option<CachedArtifactIdentity>>::with_capacity(plan.nodes.len());
    for (auto unit = cpp::UnitId {}; unit < plan.nodes.len(); ++unit) {
        selection.push(u8(1));
        identities.emplace_back();
    }
    return execute_compile_plan_selection(
        package, units, plan, selection, rstd::move(identities), cache, compile, observer, policy);
}

} // namespace lito
