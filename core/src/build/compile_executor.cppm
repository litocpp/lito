export module lito.build.compile_executor;

import rstd;
import lito.error;
import lito.cpp.bmi;
import lito.manifest.contract;
import lito.package.target_contract;
import lito.build.identity;
import lito.build.plan_contract;
import lito.build.contract;
import lito.toolchain.contract;
import lito.system.process;
import lito.cache;
import lito.toolchain;
import lito.build.layout;
import lito.build.compile_test;
import lito.build.profiling;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace lito
{

template<typename T>
auto compile_failure(ErrorKind kind, String message) -> Result<T> {
    return Err(Error::make(kind, rstd::move(message)));
}

template<typename T>
auto compile_failure(ErrorKind kind, ref<str> message) -> Result<T> {
    return Err(Error::make(kind, message));
}

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
    Option<BuildEventKind>       event;
    Option<CompileTestExecution> compile_test;
};

auto next_ready(const Vec<CompileNodeRuntime>& runtime) -> Option<UnitId> {
    for (auto unit = UnitId {}; unit < runtime.len(); ++unit) {
        if (runtime[unit].status == CompileNodeStatus::Ready) return Some(unit);
    }
    return None();
}

auto block_node(UnitId                      unit,
                const Vec<Vec<UnitId>>&     dependents,
                Vec<CompileNodeRuntime>&    runtime,
                usize&                      terminal,
                CompileExecutionStatistics& statistics) -> void {
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

auto fail_node(UnitId                      unit,
               Error                       error,
               const Vec<Vec<UnitId>>&     dependents,
               Vec<CompileNodeRuntime>&    runtime,
               Vec<Option<Error>>&         errors,
               usize&                      terminal,
               CompileExecutionStatistics& statistics) -> void {
    runtime[unit].status = CompileNodeStatus::Failed;
    errors[unit]         = Some(rstd::move(error));
    ++terminal;
    ++statistics.failed;
    for (auto dependent : dependents[unit]) {
        block_node(dependent, dependents, runtime, terminal, statistics);
    }
}

auto succeed_node(UnitId                   unit,
                  const Vec<Vec<UnitId>>&  dependents,
                  Vec<CompileNodeRuntime>& runtime,
                  usize&                   terminal) -> Result<empty> {
    runtime[unit].status = CompileNodeStatus::Succeeded;
    ++terminal;
    for (auto dependent : dependents[unit]) {
        if (runtime[dependent].status == CompileNodeStatus::Blocked) continue;
        if (runtime[dependent].remaining == usize {}) {
            return compile_failure<empty>(ErrorKind::Artifact,
                                          "compile DAG prerequisite underflow"_str);
        }
        --runtime[dependent].remaining;
        if (runtime[dependent].remaining == usize {}) {
            runtime[dependent].status = CompileNodeStatus::Ready;
        }
    }
    return Ok(empty {});
}

auto emit_compile_events(const Option<BuildObserver>&   observer,
                         const PackageSpec&             package,
                         const Vec<PreparedUnit>&       units,
                         const Vec<CompileNodeRuntime>& runtime) noexcept -> void {
    if (observer.is_none() || observer->notify == nullptr) return;
    for (auto unit = UnitId {}; unit < runtime.len(); ++unit) {
        if (runtime[unit].event.is_none()) continue;
        const auto target      = units[unit].unit.target;
        auto       target_name = package_target_id_text(package.targets[target].id);
        observer->notify(observer->context,
                         BuildEvent {
                             .kind   = *runtime[unit].event,
                             .target = target_name.as_str(),
                             .path   = units[unit].unit.source.as_path(),
                         });
    }
}

} // namespace lito

export namespace lito
{

struct ResolvedCompileExecution {
    usize jobs { usize(1) };
    usize max_in_flight { usize(1) };
};

auto resolve_compile_execution(const CompileExecutionPolicy& policy)
    -> Result<ResolvedCompileExecution> {
    auto jobs = usize(1);
    if (policy.jobs.is_some()) {
        jobs = *policy.jobs;
    } else {
        auto available = rstd::thread::available_parallelism();
        if (available.is_ok()) jobs = available->get();
    }
    if (jobs == usize {}) {
        return compile_failure<ResolvedCompileExecution>(
            ErrorKind::InvalidRequest, "compile jobs must be greater than zero"_str);
    }
    auto max_in_flight = policy.max_in_flight.is_some() ? *policy.max_in_flight : jobs;
    if (max_in_flight == usize {}) {
        return compile_failure<ResolvedCompileExecution>(
            ErrorKind::InvalidRequest, "compile task capacity must be greater than zero"_str);
    }
    return Ok(ResolvedCompileExecution {
        .jobs          = jobs,
        .max_in_flight = max_in_flight,
    });
}

struct CompileWorkerResult {
    UnitId                       node {};
    Result<CompileCommandResult> outcome;
};

class CompileExecutor {
    struct ExecutionFields {
        CompileExecutionStatistics statistics;
        usize                      active {};

        ExecutionFields(usize jobs, usize max_in_flight)
            : statistics {
                  .jobs          = jobs,
                  .max_in_flight = max_in_flight,
              } {}
    };

    using ExecutionState       = rstd::sync::Mutex<ExecutionFields>;
    using SharedExecutionState = rstd::sync::Arc<ExecutionState>;

    rstd::thread::ThreadPool                           pool_;
    rstd::thread::BlockingTaskSet<CompileWorkerResult> tasks_;
    SharedExecutionState                               state_;
    bool                                               finished_ {};

    CompileExecutor(rstd::thread::ThreadPool                           pool,
                    rstd::thread::BlockingTaskSet<CompileWorkerResult> tasks,
                    SharedExecutionState                               state)
        : pool_(rstd::move(pool)), tasks_(rstd::move(tasks)), state_(rstd::move(state)) {}

public:
    CompileExecutor(CompileExecutor&&) noexcept                    = default;
    auto operator=(CompileExecutor&&) noexcept -> CompileExecutor& = delete;

    static auto create(usize jobs, usize max_in_flight) -> Result<CompileExecutor> {
        if (jobs == usize {} || max_in_flight == usize {}) {
            return compile_failure<CompileExecutor>(
                ErrorKind::InvalidRequest,
                "compile execution requires non-zero jobs and capacity"_str);
        }
        auto pool = rstd::thread::ThreadPoolBuilder::make()
                        .worker_count(jobs)
                        .thread_name(String::make("lito-compile"_str))
                        .build();
        if (pool.is_err()) {
            return compile_failure<CompileExecutor>(
                ErrorKind::Artifact,
                rstd::format("cannot create compile worker pool: {}",
                             rstd::move(pool).unwrap_err_unchecked()));
        }
        auto value = rstd::move(pool).unwrap_unchecked();
        auto tasks =
            rstd::thread::BlockingTaskSet<CompileWorkerResult>::make(value.handle(), max_in_flight);
        if (tasks.is_err()) {
            return compile_failure<CompileExecutor>(
                ErrorKind::Artifact,
                rstd::format("cannot create compile task set: {}",
                             rstd::move(tasks).unwrap_err_unchecked()));
        }
        return Ok(
            CompileExecutor(rstd::move(value),
                            rstd::move(tasks).unwrap_unchecked(),
                            SharedExecutionState::make(ExecutionFields { jobs, max_in_flight })));
    }

    template<typename Function>
    auto submit(UnitId node, Function function) -> Result<empty> {
        auto state        = state_.clone();
        auto submitted_at = rstd::time::Instant::now();
        auto submitted    = tasks_.try_submit([node,
                                               function = rstd::move(function),
                                               state    = rstd::move(state),
                                               submitted_at]() mutable -> CompileWorkerResult {
            auto started = rstd::time::Instant::now();
            {
                auto fields = state->lock().unwrap_unchecked();
                ++fields->statistics.tasks;
                ++fields->active;
                fields->statistics.ready_wait =
                    fields->statistics.ready_wait.saturating_add(submitted_at.elapsed());
                if (fields->active > fields->statistics.max_active) {
                    fields->statistics.max_active = fields->active;
                }
            }
            auto outcome = function();
            {
                auto fields = state->lock().unwrap_unchecked();
                --fields->active;
                fields->statistics.task_work =
                    fields->statistics.task_work.saturating_add(started.elapsed());
            }
            return CompileWorkerResult {
                .node    = node,
                .outcome = rstd::move(outcome),
            };
        });
        if (submitted.is_ok()) return Ok(empty {});
        auto error = rstd::move(submitted).unwrap_err_unchecked();
        if (error == rstd::thread::BlockingTaskSetSubmitError::Full) {
            return compile_failure<empty>(ErrorKind::Artifact, "compile task set is full"_str);
        }
        if (error == rstd::thread::BlockingTaskSetSubmitError::Cancelled) {
            return compile_failure<empty>(ErrorKind::Artifact, "compile task set is cancelled"_str);
        }
        return compile_failure<empty>(ErrorKind::Artifact, "compile task set is closed"_str);
    }

    auto recv() -> Result<CompileWorkerResult> {
        auto started    = rstd::time::Instant::now();
        auto completion = tasks_.recv();
        {
            auto fields = state_->lock().unwrap_unchecked();
            fields->statistics.completion_wait =
                fields->statistics.completion_wait.saturating_add(started.elapsed());
        }
        if (completion.is_none()) {
            return compile_failure<CompileWorkerResult>(
                ErrorKind::Artifact, "compile task set closed before a completion arrived"_str);
        }
        auto value = rstd::move(completion).unwrap_unchecked();
        if (value.is_cancelled()) {
            return compile_failure<CompileWorkerResult>(ErrorKind::Artifact,
                                                        "compile task was cancelled"_str);
        }
        auto result = rstd::move(value).into_value();
        if (result.is_none()) {
            return compile_failure<CompileWorkerResult>(
                ErrorKind::Artifact, "compile task completed without a result"_str);
        }
        return Ok(rstd::move(result).unwrap_unchecked());
    }

    auto cancel() -> void { tasks_.cancel_pending(); }

    auto finish() -> void {
        if (finished_) return;
        tasks_.close();
        rstd::move(pool_).join();
        finished_ = true;
    }

    auto statistics() const -> CompileExecutionStatistics {
        return state_->lock().unwrap_unchecked()->statistics;
    }
};

struct CompileNodePlan {
    UnitId                    unit {};
    Vec<UnitId>               prerequisites;
    Vec<UnitId>               dependents;
    Vec<DependencyArtifact>   dependencies;
    Option<CompileInvocation> invocation;
    String                    command;
};

struct CompilePlan {
    Vec<CompileNodePlan> nodes;
};

struct CompileExecutionResult {
    usize                      compiled {};
    usize                      reused {};
    Vec<CompileTestExecution>  compile_tests;
    CompileExecutionStatistics statistics;
    BuildTimingReport          timing;
};

auto materialize_compile_plan(const PackageSpec&     package,
                              const BuildLayout&     layout,
                              const ClangToolchain&  toolchain,
                              Vec<PreparedUnit>&     units,
                              const Vec<ScanResult>& scans,
                              const ModulePlan&      modules) -> Result<CompilePlan> {
    if (scans.len() != units.len() || modules.direct_inputs.len() != units.len() ||
        modules.resolved_inputs.len() != units.len() ||
        modules.compile_order.len() != units.len()) {
        return compile_failure<CompilePlan>(ErrorKind::Artifact,
                                            "compile plan inputs have inconsistent lengths"_str);
    }

    auto invocations  = Vec<Option<CompileInvocation>>::with_capacity(units.len());
    auto dependencies = Vec<Option<Vec<DependencyArtifact>>>::with_capacity(units.len());
    auto dependents   = Vec<Vec<UnitId>>::with_capacity(units.len());
    for (auto unit = UnitId {}; unit < units.len(); ++unit) {
        invocations.emplace_back();
        dependencies.emplace_back();
        dependents.emplace_back();
    }
    for (auto consumer = UnitId {}; consumer < units.len(); ++consumer) {
        for (auto provider : modules.direct_inputs[consumer]) {
            if (provider >= units.len()) {
                return compile_failure<CompilePlan>(ErrorKind::Dependency,
                                                    "compile DAG contains an invalid provider"_str);
            }
            dependents[provider].emplace_back(consumer);
        }
    }

    auto format_identity = bmi_format_identity(toolchain.bmi_format());
    auto format_key      = bmi_format_key(toolchain.bmi_format());
    for (auto unit : modules.compile_order) {
        if (unit >= units.len()) {
            return compile_failure<CompilePlan>(ErrorKind::Dependency,
                                                "compile order contains an invalid unit"_str);
        }
        auto direct_artifacts    = Vec<DependencyArtifact>::make();
        auto recipe_dependencies = Vec<BmiRecipeDependency>::make();
        for (auto input : modules.direct_inputs[unit]) {
            if (scans[input].provided.is_none() || units[input].unit.bmi.is_none()) {
                return compile_failure<CompilePlan>(
                    ErrorKind::Dependency,
                    rstd::format("module dependency '{}' has no resolved BMI artifact",
                                 units[input].unit.source.as_path()));
            }
            const auto& artifact = *units[input].unit.bmi;
            direct_artifacts.push(DependencyArtifact {
                .logical_name = artifact.logical_name.clone(),
                .artifact     = artifact.key.value.clone(),
            });
            recipe_dependencies.push(BmiRecipeDependency {
                .logical_name = artifact.logical_name.clone(),
                .artifact_key = artifact.key.value.clone(),
            });
        }

        if (scans[unit].provided.is_some()) {
            auto source_identity   = units[unit].unit.source.as_path().to_str();
            auto relative_identity = units[unit].unit.relative_source.as_path().to_str();
            if (source_identity.is_none() || relative_identity.is_none()) {
                return compile_failure<CompilePlan>(ErrorKind::Artifact,
                                                    "BMI provider path is not valid UTF-8"_str);
            }
            const auto target = units[unit].unit.target;
            auto       provider_identity =
                rstd::format("{}:{}:{}",
                             package_target_id_text(package.targets[target].id).as_str(),
                             *relative_identity,
                             units[unit].unit.context->id.as_str());
            auto key      = make_bmi_artifact_key(BmiRecipe {
                .request                 = units[unit].unit.context->bmi,
                .logical_name            = scans[unit].provided->logical_name.clone(),
                .provider_identity       = provider_identity.clone(),
                .source_identity         = String::make(*source_identity),
                .source_content_identity = units[unit].frontend_analysis->receipt.clone(),
                .cpp_context_identity    = cpp_compile_identity(units[unit].unit.context->cpp),
                .public_requirements_identity =
                    cpp_public_requirements_identity(units[unit].unit.context->public_requirements),
                .format_identity     = format_identity.clone(),
                .direct_dependencies = rstd::move(recipe_dependencies),
            });
            auto bmi_path = layout.bmi(format_key.as_str(),
                                       key.value.as_str(),
                                       scans[unit].provided->logical_name.as_str());
            auto direct   = Vec<BmiRecipeDependency>::with_capacity(direct_artifacts.len());
            for (const auto& dependency : direct_artifacts) {
                direct.push(BmiRecipeDependency {
                    .logical_name = dependency.logical_name.clone(),
                    .artifact_key = dependency.artifact.clone(),
                });
            }
            units[unit].unit.bmi = Some(BmiArtifact {
                .logical_name        = scans[unit].provided->logical_name.clone(),
                .provider_identity   = rstd::move(provider_identity),
                .key                 = rstd::move(key),
                .format              = as<rstd::clone::Clone>(toolchain.bmi_format()).clone(),
                .request             = units[unit].unit.context->bmi,
                .path                = rstd::move(bmi_path),
                .direct_dependencies = rstd::move(direct),
                .paired_object       = Some(units[unit].unit.object.clone()),
            });
        }

        auto module_dependencies = Vec<ModuleArtifactDependency>::make();
        for (auto input : modules.resolved_inputs[unit]) {
            if (scans[input].provided.is_none() || units[input].unit.bmi.is_none()) {
                return compile_failure<CompilePlan>(
                    ErrorKind::Dependency,
                    rstd::format("module dependency '{}' has no resolved BMI artifact",
                                 units[input].unit.source.as_path()));
            }
            const auto& artifact = *units[input].unit.bmi;
            module_dependencies.push(ModuleArtifactDependency {
                .logical_name = artifact.logical_name.clone(),
                .artifact_key = BmiArtifactKey { .value = artifact.key.value.clone() },
                .path         = artifact.path.clone(),
            });
        }
        auto invocation = toolchain.prepare_compile(units[unit], scans[unit], module_dependencies);
        if (invocation.is_err()) return Err(rstd::move(invocation).unwrap_err());
        dependencies[unit] = Some(rstd::move(direct_artifacts));
        invocations[unit]  = Some(rstd::move(invocation).unwrap());
    }

    for (auto unit = UnitId {}; unit < units.len(); ++unit) {
        if (units[unit].unit.compile_test == nullptr) continue;
        if (! dependents[unit].is_empty()) {
            return compile_failure<CompilePlan>(
                ErrorKind::Dependency,
                rstd::format("compile-test source '{}' cannot provide an imported artifact",
                             units[unit].unit.source.as_path()));
        }
    }

    auto nodes = Vec<CompileNodePlan>::with_capacity(units.len());
    for (auto unit = UnitId {}; unit < units.len(); ++unit) {
        if (invocations[unit].is_none() || dependencies[unit].is_none()) {
            return compile_failure<CompilePlan>(ErrorKind::Artifact,
                                                "compile plan left a unit unmaterialized"_str);
        }
        auto invocation = rstd::move(invocations[unit]).unwrap_unchecked();
        auto command    = command_text(invocation.arguments);
        nodes.push(CompileNodePlan {
            .unit          = unit,
            .prerequisites = modules.direct_inputs[unit].clone(),
            .dependents    = rstd::move(dependents[unit]),
            .dependencies  = rstd::move(dependencies[unit]).unwrap_unchecked(),
            .invocation    = Some(rstd::move(invocation)),
            .command       = rstd::move(command),
        });
    }
    return Ok(CompilePlan { .nodes = rstd::move(nodes) });
}

auto execute_compile_plan(const PackageSpec&           package,
                          Vec<PreparedUnit>&           units,
                          CompilePlan                  plan,
                          CompileCacheSession&         cache,
                          ClangCompileExecutor         compile,
                          const Option<BuildObserver>& observer,
                          ResolvedCompileExecution     policy) -> Result<CompileExecutionResult> {
    auto result = CompileExecutionResult {
        .compile_tests = Vec<CompileTestExecution>::make(),
    };
    if (plan.nodes.is_empty()) {
        result.statistics.jobs          = policy.jobs;
        result.statistics.max_in_flight = policy.max_in_flight;
        return Ok(rstd::move(result));
    }

    auto jobs = policy.jobs < plan.nodes.len() ? policy.jobs : plan.nodes.len();
    auto capacity =
        policy.max_in_flight < plan.nodes.len() ? policy.max_in_flight : plan.nodes.len();
    auto created = CompileExecutor::create(jobs, capacity);
    if (created.is_err()) return Err(rstd::move(created).unwrap_err());
    auto executor = rstd::move(created).unwrap();

    auto runtime    = Vec<CompileNodeRuntime>::with_capacity(plan.nodes.len());
    auto errors     = Vec<Option<Error>>::with_capacity(plan.nodes.len());
    auto dependents = Vec<Vec<UnitId>>::with_capacity(plan.nodes.len());
    for (const auto& node : plan.nodes) {
        const auto remaining = node.prerequisites.len();
        runtime.push(CompileNodeRuntime {
            .status = remaining == usize {} ? CompileNodeStatus::Ready : CompileNodeStatus::Pending,
            .remaining = remaining,
        });
        errors.emplace_back();
        dependents.push(node.dependents.clone());
    }

    auto wall_started = rstd::time::Instant::now();
    auto terminal     = usize {};
    auto in_flight    = usize {};
    while (terminal < plan.nodes.len()) {
        while (in_flight < capacity) {
            auto selected = next_ready(runtime);
            if (selected.is_none()) break;
            const auto unit                = *selected;
            auto&      node                = plan.nodes[unit];
            const auto target              = units[unit].unit.target;
            auto       coordinator_started = rstd::time::Instant::now();
            auto       target_identity     = package_target_id_text(package.targets[target].id);
            auto       decision = cache.evaluate(target_identity.as_str(),
                                                 units[unit],
                                                 units[unit].frontend_analysis->receipt.as_str(),
                                                 *node.invocation,
                                                 node.dependencies);
            if (decision.is_err()) {
                result.statistics.coordinator_work =
                    result.statistics.coordinator_work.saturating_add(
                        coordinator_started.elapsed());
                fail_node(unit,
                          rstd::move(decision).unwrap_err(),
                          dependents,
                          runtime,
                          errors,
                          terminal,
                          result.statistics);
                continue;
            }
            auto        cache_decision = rstd::move(decision).unwrap();
            const auto* test           = units[unit].unit.compile_test;
            if (cache_decision.current() && test == nullptr) {
                runtime[unit].event = Some(BuildEventKind::Reuse);
                ++result.reused;
                ++result.statistics.reused;
                auto succeeded = succeed_node(unit, dependents, runtime, terminal);
                result.statistics.coordinator_work =
                    result.statistics.coordinator_work.saturating_add(
                        coordinator_started.elapsed());
                if (succeeded.is_err()) {
                    executor.cancel();
                    executor.finish();
                    return Err(rstd::move(succeeded).unwrap_err());
                }
                continue;
            }
            if (cache_decision.current() && test != nullptr &&
                test->outcome == CompileTestOutcome::Success) {
                auto execution = evaluate_compile_test(package.targets[target].id.package.as_str(),
                                                       *test,
                                                       units[unit].unit.source.as_path(),
                                                       CompileCommandResult {});
                auto recorded  = cache.record_compile_test(
                    cache_decision, units[unit].unit.compile_test_record->as_path(), execution);
                if (recorded.is_err()) {
                    result.statistics.coordinator_work =
                        result.statistics.coordinator_work.saturating_add(
                            coordinator_started.elapsed());
                    fail_node(unit,
                              rstd::move(recorded).unwrap_err(),
                              dependents,
                              runtime,
                              errors,
                              terminal,
                              result.statistics);
                    continue;
                }
                runtime[unit].compile_test = Some(rstd::move(execution));
                runtime[unit].event        = Some(BuildEventKind::Reuse);
                ++result.reused;
                ++result.statistics.reused;
                auto succeeded = succeed_node(unit, dependents, runtime, terminal);
                result.statistics.coordinator_work =
                    result.statistics.coordinator_work.saturating_add(
                        coordinator_started.elapsed());
                if (succeeded.is_err()) {
                    executor.cancel();
                    executor.finish();
                    return Err(rstd::move(succeeded).unwrap_err());
                }
                continue;
            }

            auto begun =
                test == nullptr
                    ? cache.begin_compile(cache_decision)
                    : cache.begin_compile_test(
                          cache_decision, units[unit].unit.compile_test_record->as_path(), *test);
            if (begun.is_err()) {
                result.statistics.coordinator_work =
                    result.statistics.coordinator_work.saturating_add(
                        coordinator_started.elapsed());
                fail_node(unit,
                          rstd::move(begun).unwrap_err(),
                          dependents,
                          runtime,
                          errors,
                          terminal,
                          result.statistics);
                continue;
            }
            runtime[unit].status   = CompileNodeStatus::Running;
            runtime[unit].event    = Some(BuildEventKind::Compile);
            runtime[unit].decision = Some(rstd::move(cache_decision));
            auto invocation        = rstd::move(node.invocation).unwrap_unchecked();
            auto submitted         = executor.submit(
                unit,
                [compile,
                 invocation = rstd::move(invocation)]() mutable -> Result<CompileCommandResult> {
                    return compile.execute(invocation);
                });
            result.statistics.coordinator_work =
                result.statistics.coordinator_work.saturating_add(coordinator_started.elapsed());
            if (submitted.is_err()) {
                executor.cancel();
                executor.finish();
                return Err(rstd::move(submitted).unwrap_err());
            }
            ++in_flight;
        }

        if (in_flight == usize {}) {
            if (terminal == plan.nodes.len()) break;
            executor.cancel();
            executor.finish();
            return compile_failure<CompileExecutionResult>(
                ErrorKind::Artifact, "compile DAG has pending nodes without a ready frontier"_str);
        }

        auto completed = executor.recv();
        if (completed.is_err()) {
            executor.cancel();
            executor.finish();
            return Err(rstd::move(completed).unwrap_err());
        }
        --in_flight;
        auto task = rstd::move(completed).unwrap();
        if (task.node >= plan.nodes.len() ||
            runtime[task.node].status != CompileNodeStatus::Running ||
            runtime[task.node].decision.is_none()) {
            executor.cancel();
            executor.finish();
            return compile_failure<CompileExecutionResult>(
                ErrorKind::Artifact, "compile task completion does not match a running node"_str);
        }

        const auto unit                = task.node;
        auto&      node                = plan.nodes[unit];
        auto       coordinator_started = rstd::time::Instant::now();
        if (task.outcome.is_err()) {
            fail_node(unit,
                      rstd::move(task.outcome).unwrap_err(),
                      dependents,
                      runtime,
                      errors,
                      terminal,
                      result.statistics);
            result.statistics.coordinator_work =
                result.statistics.coordinator_work.saturating_add(coordinator_started.elapsed());
            continue;
        }
        auto output   = rstd::move(task.outcome).unwrap();
        auto decision = rstd::move(runtime[unit].decision).unwrap_unchecked();
        result.timing.record(BuildOperation::Compile, output.elapsed);
        const auto  target = units[unit].unit.target;
        const auto* test   = units[unit].unit.compile_test;
        if (test != nullptr) {
            auto execution = evaluate_compile_test(package.targets[target].id.package.as_str(),
                                                   *test,
                                                   units[unit].unit.source.as_path(),
                                                   rstd::move(output));
            if (execution.exit_code == i32 {}) {
                auto committed = cache.commit_success(units[unit], decision);
                if (committed.is_err()) {
                    fail_node(unit,
                              rstd::move(committed).unwrap_err(),
                              dependents,
                              runtime,
                              errors,
                              terminal,
                              result.statistics);
                    result.statistics.coordinator_work =
                        result.statistics.coordinator_work.saturating_add(
                            coordinator_started.elapsed());
                    continue;
                }
            }
            auto recorded = cache.record_compile_test(
                decision, units[unit].unit.compile_test_record->as_path(), execution);
            if (recorded.is_err()) {
                fail_node(unit,
                          rstd::move(recorded).unwrap_err(),
                          dependents,
                          runtime,
                          errors,
                          terminal,
                          result.statistics);
                result.statistics.coordinator_work =
                    result.statistics.coordinator_work.saturating_add(
                        coordinator_started.elapsed());
                continue;
            }
            runtime[unit].compile_test = Some(rstd::move(execution));
            ++result.compiled;
        } else {
            if (output.exit_code != i32 {}) {
                fail_node(unit,
                          Error::make(ErrorKind::Toolchain,
                                      rstd::format("clang++ failed for '{}'\n{}\n{}",
                                                   units[unit].unit.source.as_path(),
                                                   node.command.as_str(),
                                                   output.standard_error.as_str())),
                          dependents,
                          runtime,
                          errors,
                          terminal,
                          result.statistics);
                result.statistics.coordinator_work =
                    result.statistics.coordinator_work.saturating_add(
                        coordinator_started.elapsed());
                continue;
            }
            auto committed = cache.commit_success(units[unit], decision);
            if (committed.is_err()) {
                fail_node(unit,
                          rstd::move(committed).unwrap_err(),
                          dependents,
                          runtime,
                          errors,
                          terminal,
                          result.statistics);
                result.statistics.coordinator_work =
                    result.statistics.coordinator_work.saturating_add(
                        coordinator_started.elapsed());
                continue;
            }
            ++result.compiled;
        }
        auto succeeded = succeed_node(unit, dependents, runtime, terminal);
        result.statistics.coordinator_work =
            result.statistics.coordinator_work.saturating_add(coordinator_started.elapsed());
        if (succeeded.is_err()) {
            executor.cancel();
            executor.finish();
            return Err(rstd::move(succeeded).unwrap_err());
        }
    }

    auto executor_statistics = executor.statistics();
    executor.finish();
    executor_statistics.reused           = result.statistics.reused;
    executor_statistics.failed           = result.statistics.failed;
    executor_statistics.blocked          = result.statistics.blocked;
    executor_statistics.coordinator_work = result.statistics.coordinator_work;
    executor_statistics.wall             = wall_started.elapsed();
    result.statistics                    = executor_statistics;

    emit_compile_events(observer, package, units, runtime);
    for (auto unit = UnitId {}; unit < runtime.len(); ++unit) {
        if (runtime[unit].compile_test.is_some()) {
            result.compile_tests.push(rstd::move(runtime[unit].compile_test).unwrap_unchecked());
        }
    }
    for (auto unit = UnitId {}; unit < errors.len(); ++unit) {
        if (errors[unit].is_some()) {
            return Err(rstd::move(errors[unit]).unwrap_unchecked());
        }
    }
    return Ok(rstd::move(result));
}

} // namespace lito
