#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.frontend;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::frontend;
using PathBuf = rstd::path::PathBuf;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;

namespace
{

struct SourceReadGate {
    Atomic<bool> entered { false };
    Atomic<bool> release { false };
};

auto begin_source_activity(void* context, FrontendActivity activity) noexcept -> void {
    if (activity != FrontendActivity::SourceRead) return;
    auto& gate = *static_cast<SourceReadGate*>(context);
    gate.entered.store(true, Ordering::Release);
    while (! gate.release.load(Ordering::Acquire)) rstd::thread::yield_now();
}

auto source_fixture(ref<rstd::path::Path> root) -> PathBuf {
    auto source = PathBuf::from(root).join(PathBuf::from("source.cpp"_str).as_path());
    EXPECT_TRUE(rstd::fs::write(source.as_path(), "int value = 1;\n"_str.as_bytes()).is_ok());
    return source;
}

} // namespace

TEST(FrontendSourceStore, SharesOnlyLiveLexedSources) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner   = rstd::move(temporary).unwrap();
    auto source  = source_fixture(owner.path());
    auto store   = FrontendSourceStore::make();
    auto service = FrontendService::with_store(store);

    {
        auto first = service.load(source.as_path());
        ASSERT_TRUE(first.is_ok());
        auto second = service.load(source.as_path());
        ASSERT_TRUE(second.is_ok());
        EXPECT_EQ(service.statistics().lex_builds, usize(1));
        EXPECT_GE(store.statistics().weak_hits, usize(1));
        EXPECT_EQ(store.statistics().live_payloads, usize(1));
        EXPECT_GT(store.statistics().retained_bytes, usize {});
    }

    auto reloaded = service.load(source.as_path());
    ASSERT_TRUE(reloaded.is_ok());
    EXPECT_EQ(service.statistics().lex_builds, usize(2));
    EXPECT_GE(store.statistics().expired_entries, usize(1));
}

TEST(FrontendSourceStore, SharesLiveSourcesByFileIdentity) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner  = rstd::move(temporary).unwrap();
    auto source = source_fixture(owner.path());
    auto linked = PathBuf::from(owner.path()).join(PathBuf::from("linked.cpp"_str).as_path());
    ASSERT_TRUE(rstd::fs::hard_link(source.as_path(), linked.as_path()).is_ok());
    auto store   = FrontendSourceStore::make();
    auto service = FrontendService::with_store(store);

    auto first  = service.load(source.as_path());
    auto second = service.load(linked.as_path());

    ASSERT_TRUE(first.is_ok());
    ASSERT_TRUE(second.is_ok());
    EXPECT_EQ(service.statistics().lex_builds, usize(1));
    EXPECT_GE(store.statistics().weak_hits, usize(1));
}

TEST(FrontendSourceStore, CoalescesConcurrentLoads) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner    = rstd::move(temporary).unwrap();
    auto source   = source_fixture(owner.path());
    auto store    = FrontendSourceStore::make();
    auto gate     = SourceReadGate {};
    auto observer = FrontendObserver {
        .context = rstd::addressof(gate),
        .begin   = begin_source_activity,
    };

    auto first = rstd::thread::spawn([service = FrontendService::with_store(store, Some(observer)),
                                      source  = source.clone()]() mutable {
                     return service.load(source.as_path()).is_ok();
                 }).unwrap();
    while (! gate.entered.load(Ordering::Acquire)) rstd::thread::yield_now();
    auto second = rstd::thread::spawn([service = FrontendService::with_store(store),
                                       source  = source.clone()]() mutable {
                      return service.load(source.as_path()).is_ok();
                  }).unwrap();

    for (auto attempt = usize {}; attempt < usize(1000); ++attempt) {
        if (store.statistics().flight_waits != usize {}) break;
        rstd::thread::sleep(rstd::time::Duration::from_millis(u64(1)));
    }
    gate.release.store(true, Ordering::Release);
    auto first_result  = rstd::move(first).join().unwrap();
    auto second_result = rstd::move(second).join().unwrap();

    EXPECT_TRUE(first_result);
    EXPECT_TRUE(second_result);
    EXPECT_GE(store.statistics().flight_waits, usize(1));
    EXPECT_EQ(store.statistics().in_flight_entries, usize {});
}

TEST(FrontendSourceStore, RetriesFailedFlights) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner  = rstd::move(temporary).unwrap();
    auto source = PathBuf::from(owner.path()).join(PathBuf::from("source.cpp"_str).as_path());
    ASSERT_TRUE(rstd::fs::create_dir(source.as_path()).is_ok());
    auto store   = FrontendSourceStore::make();
    auto service = FrontendService::with_store(store);

    EXPECT_TRUE(service.load(source.as_path()).is_err());
    ASSERT_TRUE(rstd::fs::remove_dir(source.as_path()).is_ok());
    ASSERT_TRUE(rstd::fs::write(source.as_path(), "int value = 1;\n"_str.as_bytes()).is_ok());
    EXPECT_TRUE(service.load(source.as_path()).is_ok());
    EXPECT_EQ(store.statistics().in_flight_entries, usize {});
}
