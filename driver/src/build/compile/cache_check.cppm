export module lito.driver:build.compile.cache_check;

import rstd;
import :build.error;
import :build.profiling;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito
{

template<typename T>
struct CompileCacheCheckBatch {
    Vec<Option<T>>              decisions;
    CompileCacheCheckStatistics statistics;
};

// Inputs remain borrowed until every accepted check has finished.
template<typename T, typename Evaluate, typename Notify>
auto check_compile_cache_tasks(slice<usize> units,
                               usize        unit_count,
                               usize        jobs,
                               usize        capacity,
                               Evaluate     evaluate,
                               Notify       notify) -> BuildResult<CompileCacheCheckBatch<T>> {
    if (jobs == usize {} || capacity == usize {})
        return Err(BuildError::Message("cache check requires non-zero jobs and capacity"_Str));
    auto result = CompileCacheCheckBatch<T> {};
    for (usize unit {}; unit < unit_count; ++unit) result.decisions.emplace_back();
    if (units.is_empty()) return Ok(rstd::move(result));
    for (usize index {}; index < units.len(); ++index) {
        if (units[index] >= unit_count ||
            (index != usize {} && units[index - usize(1)] >= units[index]))
            return Err(BuildError::Message("cache check units must be ordered and unique"_Str));
    }
    if (jobs > units.len()) jobs = units.len();
    if (capacity > units.len()) capacity = units.len();
    if (jobs > capacity) jobs = capacity;
    auto started                    = rstd::time::Instant::now();
    result.statistics.batches       = usize(1);
    result.statistics.jobs          = jobs;
    result.statistics.max_in_flight = capacity;
    struct Completion {
        usize                unit;
        BuildResult<T>       outcome;
        rstd::time::Duration elapsed;
    };
    struct Activity {
        usize active {};
        usize peak {};
    };
    auto               activity = rstd::sync::Mutex<Activity>(Activity {});
    Option<BuildError> error;
    auto               failed_unit = usize::MAX;
    auto               completed   = usize {};
    auto               run         = [&](usize unit) -> Completion {
        auto task_started = rstd::time::Instant::now();
        {
            auto state = activity.lock().unwrap_unchecked();
            ++state->active;
            if (state->active > state->peak) state->peak = state->active;
        }
        auto outcome = evaluate(unit);
        {
            auto state = activity.lock().unwrap_unchecked();
            --state->active;
        }
        return Completion { unit, rstd::move(outcome), task_started.elapsed() };
    };
    auto accept = [&](Completion completion) {
        auto coordinator_started = rstd::time::Instant::now();
        ++completed;
        ++result.statistics.evaluations;
        result.statistics.task_work =
            result.statistics.task_work.saturating_add(completion.elapsed);
        if (completion.outcome.is_err()) {
            if (completion.unit < failed_unit) {
                failed_unit = completion.unit;
                error       = Some(rstd::move(completion.outcome).unwrap_err());
            }
        } else {
            auto value = rstd::move(completion.outcome).unwrap();
            if (value.current())
                ++result.statistics.hits;
            else
                ++result.statistics.misses;
            result.decisions[completion.unit] = Some(rstd::move(value));
        }
        notify(completed, false, error.is_some(), started.elapsed(), Some(completion.unit));
        result.statistics.coordinator_work =
            result.statistics.coordinator_work.saturating_add(coordinator_started.elapsed());
    };
    notify(usize {}, false, false, rstd::time::Duration {}, Option<usize> {});
    auto execute = [&]() -> BuildResult<empty> {
        if (jobs == usize(1)) {
            for (auto unit : units) {
                accept(run(unit));
                if (error.is_some()) break;
            }
            return Ok(empty {});
        }
        auto created = rstd::thread::ThreadPoolBuilder::make()
                           .worker_count(jobs)
                           .thread_name("lito-cache-check"_Str)
                           .build();
        if (created.is_err())
            return Err(BuildError::Message(
                rstd::format("create cache check pool: {}", created.unwrap_err())));
        auto pool = rstd::move(created).unwrap();
        auto made = rstd::thread::BlockingTaskSet<Completion>::make(pool.handle(), capacity);
        if (made.is_err())
            return Err(BuildError::Message(
                rstd::format("create cache check task set: {}", made.unwrap_err())));
        auto tasks   = rstd::move(made).unwrap();
        auto next    = usize {};
        auto pending = usize {};
        while (next < units.len() || pending != usize {}) {
            while (error.is_none() && next < units.len() && pending < capacity) {
                auto coordinator_started = rstd::time::Instant::now();
                auto unit                = units[next];
                auto submitted           = tasks.try_submit([&, unit] {
                    return run(unit);
                });
                if (submitted.is_err())
                    return Err(BuildError::Message("cache check task submission failed"_Str));
                ++next;
                ++pending;
                result.statistics.coordinator_work =
                    result.statistics.coordinator_work.saturating_add(
                        coordinator_started.elapsed());
            }
            if (pending == usize {}) break;
            auto received = tasks.recv();
            if (received.is_none() || received->is_cancelled())
                return Err(BuildError::Message("cache check ended without a result"_Str));
            accept(rstd::move(*received).into_value().unwrap());
            --pending;
        }
        tasks.close();
        rstd::move(pool).join();
        return Ok(empty {});
    }();
    result.statistics.wall       = started.elapsed();
    result.statistics.max_active = activity.lock().unwrap_unchecked()->peak;
    notify(completed,
           true,
           execute.is_err() || error.is_some(),
           result.statistics.wall,
           Option<usize> {});
    if (execute.is_err()) return Err(rstd::move(execute).unwrap_err());
    if (error.is_some()) return Err(rstd::move(error).unwrap());
    return Ok(rstd::move(result));
}

} // namespace lito
