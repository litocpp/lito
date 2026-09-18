export module lito.driver:command.format_execution;

import rstd;
import :command.error;
import lito.system;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace lito::format_execution
{

auto worker_count(usize files, Option<usize> available) -> usize {
    auto jobs = available.is_some() && *available > usize {} ? *available : usize(1);
    if (jobs > usize(4)) jobs = usize(4);
    return files < jobs ? files : jobs;
}

template<typename Function>
auto run(usize files, usize jobs, const Function& function) -> CommandResult<Vec<bool>> {
    auto results = Vec<bool>::make();
    if (files == usize {}) return Ok(rstd::move(results));
    jobs         = worker_count(files, Some(jobs));
    auto created = rstd::thread::ThreadPoolBuilder::make()
                       .worker_count(jobs)
                       .thread_name("lito-format"_Str)
                       .build();
    if (created.is_err()) {
        return Err(CommandError::System(system::SystemError::Io("create format worker pool"_Str,
                                                                rstd::path::PathBuf::make(),
                                                                rstd::move(created).unwrap_err())));
    }
    auto pool    = rstd::move(created).unwrap();
    auto bounded = rstd::thread::BlockingTaskSet<CommandResult<bool>>::make(pool.handle(), jobs);
    if (bounded.is_err()) {
        rstd::move(pool).join();
        return Err(CommandError::System(system::SystemError::Io("create format task set"_Str,
                                                                rstd::path::PathBuf::make(),
                                                                rstd::move(bounded).unwrap_err())));
    }
    auto tasks = rstd::move(bounded).unwrap();
    for (usize index {}; index < files; ++index) results.push(false);
    auto next        = usize {};
    auto active      = usize {};
    auto first_error = files;
    auto error       = Option<CommandError> {};
    while (next < files || active > usize {}) {
        while (error.is_none() && next < files && active < jobs) {
            auto submitted = tasks.try_submit([&function, index = next]() {
                return function(index);
            });
            if (submitted.is_err()) {
                error = Some(CommandError::Message("could not submit format task"_Str));
                break;
            }
            ++next;
            ++active;
        }
        if (active == usize {}) break;
        auto completion = tasks.recv();
        if (completion.is_none()) {
            error = Some(CommandError::Message("format task set closed before completion"_Str));
            break;
        }
        --active;
        auto index = completion->id();
        auto value = rstd::move(*completion).into_value();
        if (value.is_none()) {
            if (index < first_error) {
                first_error = index;
                error = Some(CommandError::Message("format task completed without a result"_Str));
            }
        } else if (value->is_err()) {
            if (index < first_error) {
                first_error = index;
                error       = Some(rstd::move(*value).unwrap_err());
            }
        } else {
            results[index] = **value;
        }
    }
    tasks.close();
    rstd::move(pool).join();
    if (error.is_some()) return Err(rstd::move(*error));
    return Ok(rstd::move(results));
}

} // namespace lito::format_execution
