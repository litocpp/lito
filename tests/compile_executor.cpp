#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.model;
import lito.compile_executor;

using namespace rstd::prelude;

namespace
{

struct GateFields {
    usize started {};
    bool  release {};
};

struct CompileGate {
    rstd::sync::Mutex<GateFields> fields;
    rstd::sync::Condvar           changed;

    CompileGate(): fields(GateFields {}), changed(rstd::sync::Condvar::make()) {}
};

auto wait_for_release(rstd::sync::Arc<CompileGate> gate)
    -> lito::Result<lito::CompileCommandResult> {
    auto fields = gate->fields.lock().unwrap_unchecked();
    ++fields->started;
    gate->changed.notify_all();
    gate->changed.wait_while(fields, [](const GateFields& value) {
        return ! value.release;
    });
    return Ok(lito::CompileCommandResult {});
}

} // namespace

TEST(CompileExecutor, RunsBoundedTasksConcurrently) {
    auto created = lito::CompileExecutor::create(usize(2), usize(2));
    ASSERT_TRUE(created.is_ok());
    auto executor = rstd::move(created).unwrap();
    auto gate     = rstd::sync::Arc<CompileGate>::make();

    auto first  = executor.submit(usize {}, [gate = gate.clone()]() mutable {
        return wait_for_release(rstd::move(gate));
    });
    auto second = executor.submit(usize(1), [gate = gate.clone()]() mutable {
        return wait_for_release(rstd::move(gate));
    });
    ASSERT_TRUE(first.is_ok());
    ASSERT_TRUE(second.is_ok());

    auto started   = usize {};
    auto timed_out = false;
    {
        auto fields = gate->fields.lock().unwrap_unchecked();
        auto waited = gate->changed.wait_timeout_while(
            fields, rstd::time::Duration::from_secs(u64(2)), [](const GateFields& value) {
                return value.started != usize(2);
            });
        started         = fields->started;
        timed_out       = waited.timed_out();
        fields->release = true;
        gate->changed.notify_all();
    }

    EXPECT_FALSE(timed_out);
    EXPECT_EQ(started, usize(2));
    auto first_result  = executor.recv();
    auto second_result = executor.recv();
    ASSERT_TRUE(first_result.is_ok());
    ASSERT_TRUE(second_result.is_ok());
    EXPECT_TRUE(first_result->outcome.is_ok());
    EXPECT_TRUE(second_result->outcome.is_ok());
    auto statistics = executor.statistics();
    executor.finish();
    EXPECT_EQ(statistics.tasks, usize(2));
    EXPECT_EQ(statistics.max_active, usize(2));
}

TEST(CompileExecutor, RejectsZeroLimits) {
    EXPECT_TRUE(lito::CompileExecutor::create(usize {}, usize(1)).is_err());
    EXPECT_TRUE(lito::CompileExecutor::create(usize(1), usize {}).is_err());
}
