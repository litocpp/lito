export module lito.driver:build.compile_executor;

import rstd;
import lito.core;
import lito.cpp;
import :build.event;
import :build.request;
import :build.artifact;
import :build.documentation;
import :build.error;
import lito.toolchain.common;
import lito.system;
import lito.toolchain;
import :build.layout;
import :build.profiling;

using namespace rstd::prelude;
using namespace lito::system;
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
        return compile_failure<ResolvedCompileExecution>(
            "compile jobs must be greater than zero"_str);
    }
    auto max_in_flight = policy.max_in_flight.is_some() ? *policy.max_in_flight : jobs;
    if (max_in_flight == usize {}) {
        return compile_failure<ResolvedCompileExecution>(
            "compile task capacity must be greater than zero"_str);
    }
    return Ok(ResolvedCompileExecution {
        .jobs          = jobs,
        .max_in_flight = max_in_flight,
    });
}

struct CompileWorkerResult {
    cpp::UnitId                           node {};
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
            return compile_failure<CompileExecutor>(
                "compile execution requires non-zero jobs and capacity"_str);
        }
        auto pool = rstd::thread::ThreadPoolBuilder::make()
                        .worker_count(jobs)
                        .thread_name(String::make("lito-compile"_str))
                        .build();
        if (pool.is_err()) {
            return Err(
                BuildError::System(SystemError::Io(String::make("create compile worker pool"_str),
                                                   PathBuf::make(),
                                                   rstd::move(pool).unwrap_err_unchecked())));
        }
        auto value = rstd::move(pool).unwrap_unchecked();
        auto tasks =
            rstd::thread::BlockingTaskSet<CompileWorkerResult>::make(value.handle(), max_in_flight);
        if (tasks.is_err()) {
            return Err(
                BuildError::System(SystemError::Io(String::make("create compile task set"_str),
                                                   PathBuf::make(),
                                                   rstd::move(tasks).unwrap_err_unchecked())));
        }
        return Ok(
            CompileExecutor(rstd::move(value),
                            rstd::move(tasks).unwrap_unchecked(),
                            SharedExecutionState::make(ExecutionFields { jobs, max_in_flight })));
    }

    template<typename Function>
    auto submit(cpp::UnitId node, Function function) -> BuildResult<empty> {
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
            return compile_failure<CompileWorkerResult>(
                "compile task set closed before a completion arrived"_str);
        }
        auto value = rstd::move(completion).unwrap_unchecked();
        if (value.is_cancelled()) {
            return compile_failure<CompileWorkerResult>("compile task was cancelled"_str);
        }
        auto result = rstd::move(value).into_value();
        if (result.is_none()) {
            return compile_failure<CompileWorkerResult>(
                "compile task completed without a result"_str);
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

} // namespace lito
