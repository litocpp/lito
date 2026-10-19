#include <rstd/test/gtest.hpp>
#include <atomic>

import rstd;
import rstd.test;
import lito.driver;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace
{
auto wait_for_flag(const std::atomic<bool>& flag) -> bool {
    auto started = rstd::time::Instant::now();
    while (! flag.load()) {
        if (started.elapsed() >= rstd::time::Duration::from_secs(u64(2))) return false;
        rstd::thread::yield_now();
    }
    return true;
}

struct Decision {
    usize unit;
    explicit Decision(usize value): unit(value) {}
    Decision(const Decision&)               = delete;
    Decision(Decision&&)                    = default;
    auto operator=(Decision&&) -> Decision& = default;
    auto current() const -> bool { return unit != usize(1); }
};
} // namespace

TEST(CompileCacheCheck, BoundsTasksAndReceivesOutOfOrder) {
    auto units = Vec<usize>::make();
    for (usize i {}; i < usize(4); ++i) units.emplace_back(i);
    std::atomic<bool> first_entered { false };
    std::atomic<bool> release { false };
    auto              caller           = rstd::thread::current().id();
    auto              notified         = usize {};
    bool              coordinator_only = true;
    bool              second_first     = false;
    auto              result           = lito::check_compile_cache_tasks<Decision>(
        units.as_slice(),
        units.len(),
        usize(2),
        usize(2),
        [&](usize unit) -> lito::BuildResult<Decision> {
            if (unit == usize {}) {
                first_entered.store(true);
                if (! wait_for_flag(release))
                    return Err(lito::BuildError::Message("gate timed out"_Str));
            } else if (unit == usize(1)) {
                if (! wait_for_flag(first_entered))
                    return Err(lito::BuildError::Message("gate timed out"_Str));
            }
            return Ok(Decision(unit));
        },
        [&](usize completed, bool finished, bool, rstd::time::Duration, Option<usize>) {
            coordinator_only = coordinator_only && rstd::thread::current().id() == caller;
            if (completed == usize(1) && ! finished) {
                second_first = ! release.load();
                release.store(true);
            }
            notified = completed;
        });
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(coordinator_only);
    EXPECT_TRUE(second_first);
    EXPECT_EQ(notified, usize(4));
    EXPECT_EQ(result->statistics.max_active, usize(2));
    EXPECT_EQ(result->statistics.max_in_flight, usize(2));
    EXPECT_EQ(result->statistics.hits, usize(3));
    EXPECT_EQ(result->statistics.misses, usize(1));
    for (auto unit : units) EXPECT_EQ(result->decisions[unit]->unit, unit);
}

TEST(CompileCacheCheck, DrainsFailuresAndSelectsLowestUnit) {
    auto units = Vec<usize>::make();
    for (usize i {}; i < usize(3); ++i) units.emplace_back(i);
    std::atomic<bool> release { false };
    std::atomic<int>  finished { 0 };
    auto              notifications = usize {};
    auto              result        = lito::check_compile_cache_tasks<Decision>(
        units.as_slice(),
        units.len(),
        usize(2),
        usize(2),
        [&](usize unit) -> lito::BuildResult<Decision> {
            if (unit == usize {} && ! wait_for_flag(release))
                return Err(lito::BuildError::Message("gate timed out"_Str));
            finished.fetch_add(1);
            return Err(lito::BuildError::Message(rstd::format("failure {}", unit)));
        },
        [&](usize completed, bool done, bool failed, rstd::time::Duration, Option<usize>) {
            if (failed) release.store(true);
            if (done) notifications = completed;
        });
    ASSERT_TRUE(result.is_err());
    EXPECT_TRUE(rstd::format("{}", result.unwrap_err()).as_str().contains("failure 0"_str));
    EXPECT_EQ(finished.load(), 2);
    EXPECT_EQ(notifications, usize(2));
}

TEST(CompileCacheCheck, EmptySerialAndInvalidSelections) {
    auto units         = Vec<usize>::make();
    auto notifications = usize {};
    auto evaluate      = [](usize unit) -> lito::BuildResult<Decision> {
        return Ok(Decision(unit));
    };
    auto notify = [&](usize, bool, bool, rstd::time::Duration, Option<usize>) {
        ++notifications;
    };
    auto empty = lito::check_compile_cache_tasks<Decision>(
        units.as_slice(), usize(3), usize(1), usize(1), evaluate, notify);
    ASSERT_TRUE(empty.is_ok());
    EXPECT_EQ(notifications, usize {});
    EXPECT_EQ(empty->statistics.batches, usize {});
    units.push(usize(1));
    auto one = lito::check_compile_cache_tasks<Decision>(
        units.as_slice(), usize(3), usize(1), usize(1), evaluate, notify);
    ASSERT_TRUE(one.is_ok());
    EXPECT_EQ(one->statistics.max_active, usize(1));
    EXPECT_TRUE(one->decisions[usize {}].is_none());
    EXPECT_TRUE(one->decisions[usize(2)].is_none());
    EXPECT_EQ(one->statistics.misses, usize(1));
    EXPECT_TRUE(lito::check_compile_cache_tasks<Decision>(
                    units.as_slice(), usize(3), usize {}, usize(1), evaluate, notify)
                    .is_err());
    units.push(usize {});
    EXPECT_TRUE(lito::check_compile_cache_tasks<Decision>(
                    units.as_slice(), usize(3), usize(2), usize(2), evaluate, notify)
                    .is_err());
}
