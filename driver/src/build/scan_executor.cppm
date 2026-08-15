module lito.driver:build.scan_executor;

import rstd;
import lito.core;
import :build.error;
import :build.frontend_analysis;
import :build.profiling;
import lito.system;

using namespace rstd::prelude;
using namespace lito::system;
using namespace rstd::literals;

namespace lito
{

struct FrontendScanTaskResult {
    usize                                    node {};
    BuildResult<FrontendAnalysisTaskOutcome> outcome;
};

class FrontendScanExecutor {
    struct ExecutionFields {
        ScanExecutionStatistics statistics;
        usize                   active {};

        ExecutionFields(usize jobs, usize max_in_flight)
            : statistics {
                  .jobs          = jobs,
                  .max_in_flight = max_in_flight,
              } {}
    };

    using ExecutionState       = rstd::sync::Mutex<ExecutionFields>;
    using SharedExecutionState = rstd::sync::Arc<ExecutionState>;

    rstd::thread::ThreadPool                              pool_;
    rstd::thread::BlockingTaskSet<FrontendScanTaskResult> tasks_;
    SharedExecutionState                                  state_;
    bool                                                  finished_ {};

    FrontendScanExecutor(rstd::thread::ThreadPool                              pool,
                         rstd::thread::BlockingTaskSet<FrontendScanTaskResult> tasks,
                         SharedExecutionState                                  state)
        : pool_(rstd::move(pool)), tasks_(rstd::move(tasks)), state_(rstd::move(state)) {}

public:
    FrontendScanExecutor(FrontendScanExecutor&&) noexcept                    = default;
    auto operator=(FrontendScanExecutor&&) noexcept -> FrontendScanExecutor& = delete;

    static auto create(usize jobs, usize max_in_flight) -> BuildResult<FrontendScanExecutor> {
        if (jobs == usize {} || max_in_flight == usize {}) {
            return Err(BuildError::Message(
                String::make("scan execution requires non-zero jobs and capacity"_str)));
        }
        auto pool = rstd::thread::ThreadPoolBuilder::make()
                        .worker_count(jobs)
                        .thread_name(String::make("lito-scan"_str))
                        .build();
        if (pool.is_err()) {
            return Err(
                BuildError::System(SystemError::Io(String::make("create scan worker pool"_str),
                                                   PathBuf::make(),
                                                   rstd::move(pool).unwrap_err_unchecked())));
        }
        auto value = rstd::move(pool).unwrap_unchecked();
        auto tasks = rstd::thread::BlockingTaskSet<FrontendScanTaskResult>::make(value.handle(),
                                                                                 max_in_flight);
        if (tasks.is_err()) {
            return Err(
                BuildError::System(SystemError::Io(String::make("create scan task set"_str),
                                                   PathBuf::make(),
                                                   rstd::move(tasks).unwrap_err_unchecked())));
        }
        return Ok(FrontendScanExecutor(
            rstd::move(value),
            rstd::move(tasks).unwrap_unchecked(),
            SharedExecutionState::make(ExecutionFields { jobs, max_in_flight })));
    }

    auto submit(usize node, FrontendAnalysisTask task) -> BuildResult<empty> {
        auto state        = state_.clone();
        auto submitted_at = rstd::time::Instant::now();
        auto submitted    = tasks_.try_submit([node,
                                               task  = rstd::move(task),
                                               state = rstd::move(state),
                                               submitted_at]() mutable -> FrontendScanTaskResult {
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
            auto outcome = rstd::move(task).run();
            {
                auto fields = state->lock().unwrap_unchecked();
                --fields->active;
                fields->statistics.task_work =
                    fields->statistics.task_work.saturating_add(started.elapsed());
            }
            return FrontendScanTaskResult {
                .node    = node,
                .outcome = rstd::move(outcome),
            };
        });
        if (submitted.is_ok()) return Ok(empty {});
        auto error = rstd::move(submitted).unwrap_err_unchecked();
        if (error == rstd::thread::BlockingTaskSetSubmitError::Full) {
            return Err(BuildError::Message(String::make("scan task set is full"_str)));
        }
        if (error == rstd::thread::BlockingTaskSetSubmitError::Cancelled) {
            return Err(BuildError::Message(String::make("scan task set is cancelled"_str)));
        }
        return Err(BuildError::Message(String::make("scan task set is closed"_str)));
    }

    auto recv() -> BuildResult<FrontendScanTaskResult> {
        auto started    = rstd::time::Instant::now();
        auto completion = tasks_.recv();
        {
            auto fields = state_->lock().unwrap_unchecked();
            fields->statistics.completion_wait =
                fields->statistics.completion_wait.saturating_add(started.elapsed());
        }
        if (completion.is_none()) {
            return Err(BuildError::Message(
                String::make("scan task set closed before a completion arrived"_str)));
        }
        auto value = rstd::move(completion).unwrap_unchecked();
        if (value.is_cancelled()) {
            return Err(BuildError::Message(String::make("scan task was cancelled"_str)));
        }
        auto result = rstd::move(value).into_value();
        if (result.is_none()) {
            return Err(
                BuildError::Message(String::make("scan task completed without a result"_str)));
        }
        return Ok(rstd::move(result).unwrap_unchecked());
    }

    auto finish() -> void {
        if (finished_) return;
        tasks_.close();
        rstd::move(pool_).join();
        finished_ = true;
    }

    auto statistics() const -> ScanExecutionStatistics {
        return state_->lock().unwrap_unchecked()->statistics;
    }

    auto cancel() -> void { tasks_.cancel_pending(); }
};

class FrontendScanExecution {
    usize                             requested_jobs_ {};
    usize                             requested_max_in_flight_ {};
    Option<Box<FrontendScanExecutor>> executor_;
    ScanExecutionStatistics           statistics_;

    FrontendScanExecution(usize jobs, usize max_in_flight)
        : requested_jobs_(jobs), requested_max_in_flight_(max_in_flight) {}

    auto merge(ScanExecutionStatistics statistics) noexcept -> void {
        if (statistics.jobs > statistics_.jobs) statistics_.jobs = statistics.jobs;
        if (statistics.max_in_flight > statistics_.max_in_flight) {
            statistics_.max_in_flight = statistics.max_in_flight;
        }
        statistics_.tasks += statistics.tasks;
        if (statistics.max_active > statistics_.max_active) {
            statistics_.max_active = statistics.max_active;
        }
        statistics_.ready_wait = statistics_.ready_wait.saturating_add(statistics.ready_wait);
        statistics_.completion_wait =
            statistics_.completion_wait.saturating_add(statistics.completion_wait);
        statistics_.task_work = statistics_.task_work.saturating_add(statistics.task_work);
    }

    auto finish_executor() -> void {
        if (executor_.is_none()) return;
        auto executor = rstd::move(executor_).unwrap_unchecked();
        auto current  = executor->statistics();
        executor->finish();
        merge(current);
    }

public:
    FrontendScanExecution(FrontendScanExecution&&) noexcept                    = default;
    auto operator=(FrontendScanExecution&&) noexcept -> FrontendScanExecution& = delete;

    static auto create(usize jobs, usize max_in_flight) -> BuildResult<FrontendScanExecution> {
        if (jobs == usize {} || max_in_flight == usize {}) {
            return Err(BuildError::Message(
                String::make("scan execution requires non-zero jobs and capacity"_str)));
        }
        return Ok(FrontendScanExecution(jobs, max_in_flight));
    }

    auto prepare(usize ready) -> BuildResult<empty> {
        if (ready == usize {}) {
            return Err(BuildError::Message(
                String::make("scan execution cannot prepare an empty frontier"_str)));
        }
        auto jobs          = requested_jobs_ < ready ? requested_jobs_ : ready;
        auto max_in_flight = requested_max_in_flight_ < ready ? requested_max_in_flight_ : ready;
        if (executor_.is_some()) {
            auto current = (**executor_).statistics();
            if (current.jobs >= jobs && current.max_in_flight >= max_in_flight) {
                return Ok(empty {});
            }
            finish_executor();
        }
        auto created = FrontendScanExecutor::create(jobs, max_in_flight);
        if (created.is_err()) return Err(rstd::move(created).unwrap_err());
        executor_ = Some(Box<FrontendScanExecutor>::make(rstd::move(created).unwrap()));
        return Ok(empty {});
    }

    auto submit(usize node, FrontendAnalysisTask task) -> BuildResult<empty> {
        if (executor_.is_none()) {
            return Err(
                BuildError::Message(String::make("scan execution has no prepared frontier"_str)));
        }
        return (**executor_).submit(node, rstd::move(task));
    }

    auto recv() -> BuildResult<FrontendScanTaskResult> {
        if (executor_.is_none()) {
            return Err(
                BuildError::Message(String::make("scan execution has no prepared frontier"_str)));
        }
        return (**executor_).recv();
    }

    auto cancel() -> void {
        if (executor_.is_some()) (**executor_).cancel();
    }

    auto finish() -> void { finish_executor(); }

    auto statistics() const noexcept -> const ScanExecutionStatistics& { return statistics_; }
};

} // namespace lito
