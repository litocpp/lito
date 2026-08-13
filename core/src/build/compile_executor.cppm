export module lito.build.compile_executor;

import rstd;
import lito.error;
import lito.cpp.bmi;
import lito.manifest.contract;
import lito.package.target_contract;
import lito.build.identity;
import lito.build.plan_contract;
import lito.build.contract;
import lito.build.error_contract;
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
auto compile_failure(String message) -> BuildResult<T> {
    return Err(BuildError::Message(rstd::move(message)));
}

template<typename T>
auto compile_failure(ref<str> message) -> BuildResult<T> {
    return Err(BuildError::Message(String::make(message)));
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
               BuildError                  error,
               const Vec<Vec<UnitId>>&     dependents,
               Vec<CompileNodeRuntime>&    runtime,
               Vec<Option<BuildError>>&    errors,
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
                  usize&                   terminal) -> BuildResult<empty> {
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

auto emit_compile_event(const Option<BuildObserver>& observer,
                        const PackageSpec&           package,
                        const Vec<PreparedUnit>&     units,
                        UnitId                       unit,
                        BuildEventKind               kind,
                        Option<BuildProgress>         progress = None()) noexcept -> void {
    if (observer.is_none() || observer->notify == nullptr) return;
    const auto target      = units[unit].unit.target;
    auto       target_name = package_target_id_text(package.targets[target].id);
    observer->notify(observer->context,
                     BuildEvent {
                         .kind     = kind,
                         .target   = target_name.as_str(),
                         .path     = units[unit].unit.source.as_path(),
                         .progress = rstd::move(progress),
                     });
}

} // namespace lito

export namespace lito
{

struct ResolvedCompileExecution {
    usize jobs { usize(1) };
    usize max_in_flight { usize(1) };
};

auto resolve_compile_execution(const CompileExecutionPolicy& policy)
    -> BuildResult<ResolvedCompileExecution> {
    auto jobs = usize(1);
    if (policy.jobs.is_some()) {
        jobs = *policy.jobs;
    } else {
        auto available = rstd::thread::available_parallelism();
        if (available.is_ok()) jobs = available->get();
    }
    if (jobs == usize {}) {
        return compile_failure<ResolvedCompileExecution>("compile jobs must be greater than zero"_str);
    }
    auto max_in_flight = policy.max_in_flight.is_some() ? *policy.max_in_flight : jobs;
    if (max_in_flight == usize {}) {
        return compile_failure<ResolvedCompileExecution>("compile task capacity must be greater than zero"_str);
    }
    return Ok(ResolvedCompileExecution {
        .jobs          = jobs,
        .max_in_flight = max_in_flight,
    });
}

struct CompileWorkerResult {
    UnitId                       node {};
    ToolchainResult<CompileCommandResult> outcome;
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

    static auto create(usize jobs, usize max_in_flight) -> BuildResult<CompileExecutor> {
        if (jobs == usize {} || max_in_flight == usize {}) {
            return compile_failure<CompileExecutor>("compile execution requires non-zero jobs and capacity"_str);
        }
        auto pool = rstd::thread::ThreadPoolBuilder::make()
                        .worker_count(jobs)
                        .thread_name(String::make("lito-compile"_str))
                        .build();
        if (pool.is_err()) {
            return Err(BuildError::System(SystemError::Io(
                String::make("create compile worker pool"_str),
                PathBuf::make(),
                rstd::move(pool).unwrap_err_unchecked())));
        }
        auto value = rstd::move(pool).unwrap_unchecked();
        auto tasks =
            rstd::thread::BlockingTaskSet<CompileWorkerResult>::make(value.handle(), max_in_flight);
        if (tasks.is_err()) {
            return Err(BuildError::System(SystemError::Io(
                String::make("create compile task set"_str),
                PathBuf::make(),
                rstd::move(tasks).unwrap_err_unchecked())));
        }
        return Ok(
            CompileExecutor(rstd::move(value),
                            rstd::move(tasks).unwrap_unchecked(),
                            SharedExecutionState::make(ExecutionFields { jobs, max_in_flight })));
    }

    template<typename Function>
    auto submit(UnitId node, Function function) -> BuildResult<empty> {
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
            return compile_failure<empty>("compile task set is full"_str);
        }
        if (error == rstd::thread::BlockingTaskSetSubmitError::Cancelled) {
            return compile_failure<empty>("compile task set is cancelled"_str);
        }
        return compile_failure<empty>("compile task set is closed"_str);
    }

    auto recv() -> BuildResult<CompileWorkerResult> {
        auto started    = rstd::time::Instant::now();
        auto completion = tasks_.recv();
        {
            auto fields = state_->lock().unwrap_unchecked();
            fields->statistics.completion_wait =
                fields->statistics.completion_wait.saturating_add(started.elapsed());
        }
        if (completion.is_none()) {
            return compile_failure<CompileWorkerResult>("compile task set closed before a completion arrived"_str);
        }
        auto value = rstd::move(completion).unwrap_unchecked();
        if (value.is_cancelled()) {
            return compile_failure<CompileWorkerResult>("compile task was cancelled"_str);
        }
        auto result = rstd::move(value).into_value();
        if (result.is_none()) {
            return compile_failure<CompileWorkerResult>("compile task completed without a result"_str);
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
                              const ModulePlan&      modules) -> BuildResult<CompilePlan> {
    if (scans.len() != units.len() || modules.direct_inputs.len() != units.len() ||
        modules.resolved_inputs.len() != units.len() ||
        modules.compile_order.len() != units.len()) {
        return compile_failure<CompilePlan>("compile plan inputs have inconsistent lengths"_str);
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
                return compile_failure<CompilePlan>("compile DAG contains an invalid provider"_str);
            }
            dependents[provider].emplace_back(consumer);
        }
    }

    auto format_identity = bmi_format_identity(toolchain.bmi_format());
    auto format_key      = bmi_format_key(toolchain.bmi_format());
    for (auto unit : modules.compile_order) {
        if (unit >= units.len()) {
            return compile_failure<CompilePlan>("compile order contains an invalid unit"_str);
        }
        auto direct_artifacts    = Vec<DependencyArtifact>::make();
        auto recipe_dependencies = Vec<BmiRecipeDependency>::make();
        for (auto input : modules.direct_inputs[unit]) {
            if (scans[input].provided.is_none() || units[input].unit.bmi.is_none()) {
                return compile_failure<CompilePlan>(rstd::format("module dependency '{}' has no resolved BMI artifact",
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
                return compile_failure<CompilePlan>("BMI provider path is not valid UTF-8"_str);
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
                return compile_failure<CompilePlan>(rstd::format("module dependency '{}' has no resolved BMI artifact",
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
        if (invocation.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(invocation).unwrap_err()));
        }
        dependencies[unit] = Some(rstd::move(direct_artifacts));
        invocations[unit]  = Some(rstd::move(invocation).unwrap());
    }

    for (auto unit = UnitId {}; unit < units.len(); ++unit) {
        if (units[unit].unit.compile_test == nullptr) continue;
        if (! dependents[unit].is_empty()) {
            return compile_failure<CompilePlan>(rstd::format("compile-test source '{}' cannot provide an imported artifact",
                             units[unit].unit.source.as_path()));
        }
    }

    auto nodes = Vec<CompileNodePlan>::with_capacity(units.len());
    for (auto unit = UnitId {}; unit < units.len(); ++unit) {
        if (invocations[unit].is_none() || dependencies[unit].is_none()) {
            return compile_failure<CompilePlan>("compile plan left a unit unmaterialized"_str);
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
                          ResolvedCompileExecution     policy) -> BuildResult<CompileExecutionResult> {
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
    auto runtime    = Vec<CompileNodeRuntime>::with_capacity(plan.nodes.len());
    auto errors     = Vec<Option<BuildError>>::with_capacity(plan.nodes.len());
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
    auto compile_total = usize {};
    for (auto unit = UnitId {}; unit < plan.nodes.len(); ++unit) {
        const auto target          = units[unit].unit.target;
        auto       started         = rstd::time::Instant::now();
        auto       target_identity = package_target_id_text(package.targets[target].id);
        auto decision = cache.evaluate(target_identity.as_str(),
                                       units[unit],
                                       units[unit].frontend_analysis->receipt.as_str(),
                                       *plan.nodes[unit].invocation,
                                       plan.nodes[unit].dependencies);
        result.statistics.coordinator_work =
            result.statistics.coordinator_work.saturating_add(started.elapsed());
        if (decision.is_err()) {
            return Err(rstd::into<BuildError>(rstd::move(decision).unwrap_err()));
        }
        const auto* test = units[unit].unit.compile_test;
        if (! decision->current() ||
            (test != nullptr && test->outcome != CompileTestOutcome::Success)) {
            ++compile_total;
        }
        runtime[unit].decision = Some(rstd::move(decision).unwrap());
    }

    auto created = CompileExecutor::create(jobs, capacity);
    if (created.is_err()) return Err(rstd::move(created).unwrap_err());
    auto executor          = rstd::move(created).unwrap();
    auto terminal          = usize {};
    auto in_flight         = usize {};
    auto compile_announced = usize {};
    while (terminal < plan.nodes.len()) {
        while (in_flight < capacity) {
            auto selected = next_ready(runtime);
            if (selected.is_none()) break;
            const auto unit                = *selected;
            auto&      node                = plan.nodes[unit];
            const auto target              = units[unit].unit.target;
            auto       coordinator_started = rstd::time::Instant::now();
            auto        cache_decision = rstd::move(runtime[unit].decision).unwrap_unchecked();
            const auto* test           = units[unit].unit.compile_test;
            if (cache_decision.current() && test == nullptr) {
                ++result.reused;
                ++result.statistics.reused;
                emit_compile_event(observer, package, units, unit, BuildEventKind::Reuse);
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
                              rstd::into<BuildError>(rstd::move(recorded).unwrap_err()),
                              dependents,
                              runtime,
                              errors,
                              terminal,
                              result.statistics);
                    continue;
                }
                runtime[unit].compile_test = Some(rstd::move(execution));
                ++result.reused;
                ++result.statistics.reused;
                emit_compile_event(observer, package, units, unit, BuildEventKind::Reuse);
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
                          rstd::into<BuildError>(rstd::move(begun).unwrap_err()),
                          dependents,
                          runtime,
                          errors,
                          terminal,
                          result.statistics);
                continue;
            }
            runtime[unit].status   = CompileNodeStatus::Running;
            runtime[unit].decision = Some(rstd::move(cache_decision));
            auto invocation        = rstd::move(node.invocation).unwrap_unchecked();
            auto submitted         = executor.submit(
                unit,
                [compile,
                 invocation = rstd::move(invocation)]() mutable
                    -> ToolchainResult<CompileCommandResult> {
                    return compile.execute(invocation);
                });
            result.statistics.coordinator_work =
                result.statistics.coordinator_work.saturating_add(coordinator_started.elapsed());
            if (submitted.is_err()) {
                executor.cancel();
                executor.finish();
                return Err(rstd::move(submitted).unwrap_err());
            }
            ++compile_announced;
            emit_compile_event(observer,
                               package,
                               units,
                               unit,
                               BuildEventKind::Compile,
                               Some(BuildProgress { compile_announced, compile_total }));
            ++in_flight;
        }

        if (in_flight == usize {}) {
            if (terminal == plan.nodes.len()) break;
            executor.cancel();
            executor.finish();
            return compile_failure<CompileExecutionResult>("compile DAG has pending nodes without a ready frontier"_str);
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
            return compile_failure<CompileExecutionResult>("compile task completion does not match a running node"_str);
        }

        const auto unit                = task.node;
        auto       coordinator_started = rstd::time::Instant::now();
        if (task.outcome.is_err()) {
            fail_node(unit,
                      rstd::into<BuildError>(rstd::move(task.outcome).unwrap_err()),
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
                              rstd::into<BuildError>(rstd::move(committed).unwrap_err()),
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
                          rstd::into<BuildError>(rstd::move(recorded).unwrap_err()),
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
                          BuildError::Toolchain(ToolchainError::Execution(
                              rstd::format("compile '{}'", units[unit].unit.source.as_path()),
                              output.exit_code,
                              String::make(),
                              rstd::move(output.standard_error))),
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
                          rstd::into<BuildError>(rstd::move(committed).unwrap_err()),
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
