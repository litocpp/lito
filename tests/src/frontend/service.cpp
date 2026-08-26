#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import lito.frontend;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace lito::frontend;
using lito::frontend::preprocessor::SourceLoadRole;
using PathBuf = rstd::path::PathBuf;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;

namespace
{

struct SourceReadGate {
    Atomic<bool> entered { false };
    Atomic<bool> release { false };
};

struct HeaderDomain {
    String value;
};

struct HeaderDomains {
    PathBuf first;
    String  first_domain;
    String  second_domain;
};

auto classify_header_domain(const void* context, ref<rstd::path::Path>)
    -> HeaderCacheClassification {
    const auto& domain = *static_cast<const HeaderDomain*>(context);
    return HeaderCacheClassification { .retention_domain = Some(domain.value.clone()) };
}

auto classify_distinct_header_domains(const void* context, ref<rstd::path::Path> path)
    -> HeaderCacheClassification {
    const auto& domains = *static_cast<const HeaderDomains*>(context);
    return HeaderCacheClassification {
        .retention_domain = Some(path == domains.first.as_path() ? domains.first_domain.clone()
                                                                 : domains.second_domain.clone()),
    };
}

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

TEST(FrontendSourceStore, RetainsLexedSourcesForTheScanSession) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner   = rstd::move(temporary).unwrap();
    auto source  = source_fixture(owner.path());
    auto store   = FrontendSourceStore::make();
    auto service = FrontendService::with_store(store);

    {
        auto first = service.load(source.as_path(), SourceLoadRole::Include);
        ASSERT_TRUE(first.is_ok());
        auto second = service.load(source.as_path(), SourceLoadRole::Include);
        ASSERT_TRUE(second.is_ok());
        EXPECT_EQ(service.statistics().lex_builds, usize(1));
        EXPECT_GE(store.statistics().cache_hits, usize(1));
        EXPECT_EQ(store.statistics().live_payloads, usize(1));
        EXPECT_GT(store.statistics().retained_bytes, usize {});
        auto first_token = (**first).token(usize(9), usize {});
        EXPECT_EQ(first_token.text(), "int"_str);
        EXPECT_EQ(first_token.location().source, usize(9));
        EXPECT_EQ(first_token.location().line, usize(1));
        EXPECT_EQ(first_token.location().column, usize(1));
    }

    auto reloaded = service.load(source.as_path(), SourceLoadRole::Include);
    ASSERT_TRUE(reloaded.is_ok());
    EXPECT_EQ(service.statistics().lex_builds, usize(1));
    EXPECT_GE(store.statistics().cache_hits, usize(2));
}

TEST(FrontendSourceStore, DropsStorageAfterStoreAndConsumerRelease) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner  = rstd::move(temporary).unwrap();
    auto source = source_fixture(owner.path());
    auto store  = FrontendSourceStore::make();
    auto weak   = [&] {
        auto service = FrontendService::with_store(store);
        auto loaded  = service.load(source.as_path(), SourceLoadRole::Include);
        EXPECT_TRUE(loaded.is_ok());
        auto result   = (*loaded).downgrade();
        auto released = store.release();
        EXPECT_TRUE(released.is_ok());
        EXPECT_FALSE(released->released_immediately());
        EXPECT_TRUE(static_cast<bool>(result.upgrade()));
        return result;
    }();

    EXPECT_TRUE(weak.expired());
}

TEST(FrontendSourceStore, ReleasesOnlyTheRequestedHeaderDomain) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner   = rstd::move(temporary).unwrap();
    auto source  = source_fixture(owner.path());
    auto store   = FrontendSourceStore::make();
    auto domain  = HeaderDomain { .value = String::make("target:fixture"_str) };
    auto service = FrontendService::with_store(store,
                                               None(),
                                               Some(FrontendHeaderClassifier {
                                                   .context  = rstd::addressof(domain),
                                                   .classify = classify_header_domain,
                                               }));

    auto loaded = service.load(source.as_path(), SourceLoadRole::Include);
    ASSERT_TRUE(loaded.is_ok());
    auto retained = store.statistics();
    ASSERT_EQ(retained.live_payloads, usize(1));
    EXPECT_GT(retained.storage_bytes, usize {});
    EXPECT_GT(retained.token_bytes, usize {});
    EXPECT_GT(retained.arena_used_bytes, usize {});
    EXPECT_GE(retained.arena_reserved_bytes, retained.arena_used_bytes);
    EXPECT_GT(retained.domain_reserved_bytes, usize {});
    EXPECT_GT(retained.domain_mapped_bytes, usize {});
    EXPECT_GT(retained.domain_mappings, usize {});
    EXPECT_GT(retained.metadata_reserved_bytes, usize {});

    store.release_domain(domain.value.as_str());

    auto released = store.statistics();
    EXPECT_EQ(released.live_payloads, usize {});
    EXPECT_EQ(released.retained_bytes, usize {});
    EXPECT_EQ(released.storage_bytes, usize {});
    EXPECT_EQ(released.token_bytes, usize {});
    EXPECT_EQ(released.arena_used_bytes, usize {});
    EXPECT_EQ(released.arena_reserved_bytes, usize {});
    EXPECT_GT(released.domain_reserved_bytes, usize {});
    EXPECT_GT(released.domain_mapped_bytes, usize {});
    EXPECT_EQ(released.metadata_reserved_bytes, usize {});
    EXPECT_EQ(released.domain_releases, usize(1));
    EXPECT_FALSE((**loaded).tokens.is_empty());
}

TEST(FrontendSourceStore, ReleasesMemoryDomainImmediatelyWithoutExternalConsumer) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner   = rstd::move(temporary).unwrap();
    auto source  = source_fixture(owner.path());
    auto store   = FrontendSourceStore::make();
    auto service = FrontendService::with_store(store);

    ASSERT_TRUE(service.load(source.as_path(), SourceLoadRole::Include).is_ok());
    auto before = store.statistics();
    ASSERT_GT(before.domain_mapped_bytes, usize {});

    auto released = store.release();
    ASSERT_TRUE(released.is_ok());
    EXPECT_TRUE(released->released_immediately());
    EXPECT_TRUE(released->released());
    EXPECT_EQ(store.statistics().domain_mapped_bytes, usize {});
    EXPECT_TRUE(service.load(source.as_path(), SourceLoadRole::Include).is_err());
    auto repeated = store.release();
    ASSERT_TRUE(repeated.is_err());
    EXPECT_EQ(repeated.unwrap_err_unchecked().kind, SourceCacheReleaseErrorKind::Closed);
}

TEST(FrontendSourceStore, DelaysMemoryDomainReleaseForExternalConsumer) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner   = rstd::move(temporary).unwrap();
    auto source  = source_fixture(owner.path());
    auto store   = FrontendSourceStore::make();
    auto service = FrontendService::with_store(store);
    auto receipt = Option<SourceCacheReleaseReceipt> {};
    {
        auto loaded = service.load(source.as_path(), SourceLoadRole::Include);
        ASSERT_TRUE(loaded.is_ok());
        auto released = store.release();
        ASSERT_TRUE(released.is_ok());
        EXPECT_FALSE(released->released_immediately());
        EXPECT_FALSE(released->released());
        EXPECT_EQ((**loaded).token(usize(1), usize {}).text(), "int"_str);
        receipt = Some(rstd::move(released).unwrap());
    }

    EXPECT_TRUE(receipt->released());
}

TEST(FrontendSourceStore, RejectsReleaseWhileSourceLoadIsInFlight) {
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

    auto worker = rstd::thread::spawn([service = FrontendService::with_store(store, Some(observer)),
                                       source  = source.clone()]() mutable {
                      return service.load(source.as_path(), SourceLoadRole::Include).is_ok();
                  }).unwrap();
    while (! gate.entered.load(Ordering::Acquire)) rstd::thread::yield_now();

    auto rejected = store.release();
    ASSERT_TRUE(rejected.is_err());
    const auto& error = rejected.unwrap_err_unchecked();
    EXPECT_EQ(error.kind, SourceCacheReleaseErrorKind::InFlight);
    EXPECT_GT(error.in_flight_entries, usize {});
    EXPECT_GT(error.active_loads, usize {});

    gate.release.store(true, Ordering::Release);
    EXPECT_TRUE(rstd::move(worker).join().unwrap());
    auto released = store.release();
    ASSERT_TRUE(released.is_ok());
    EXPECT_TRUE(released->released_immediately());
}

TEST(FrontendSourceStore, RejectsReleaseWhilePrimarySourceLoadIsActive) {
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

    auto worker = rstd::thread::spawn([service = FrontendService::with_store(store, Some(observer)),
                                       source  = source.clone()]() mutable {
                      return service.load(source.as_path(), SourceLoadRole::Primary).is_ok();
                  }).unwrap();
    while (! gate.entered.load(Ordering::Acquire)) rstd::thread::yield_now();

    auto rejected = store.release();
    ASSERT_TRUE(rejected.is_err());
    const auto& error = rejected.unwrap_err_unchecked();
    EXPECT_EQ(error.kind, SourceCacheReleaseErrorKind::InFlight);
    EXPECT_EQ(error.in_flight_entries, usize {});
    EXPECT_GT(error.active_loads, usize {});

    gate.release.store(true, Ordering::Release);
    EXPECT_TRUE(rstd::move(worker).join().unwrap());
    auto released = store.release();
    ASSERT_TRUE(released.is_ok());
    EXPECT_TRUE(released->released_immediately());
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

    auto first  = service.load(source.as_path(), SourceLoadRole::Include);
    auto second = service.load(linked.as_path(), SourceLoadRole::Include);

    ASSERT_TRUE(first.is_ok());
    ASSERT_TRUE(second.is_ok());
    EXPECT_EQ(service.statistics().lex_builds, usize(1));
    EXPECT_GE(store.statistics().cache_hits, usize(1));
}

TEST(FrontendSourceStore, PromotesConflictingPhysicalDomainsToSessionRetention) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner  = rstd::move(temporary).unwrap();
    auto source = source_fixture(owner.path());
    auto linked = PathBuf::from(owner.path()).join(PathBuf::from("linked.cpp"_str).as_path());
    ASSERT_TRUE(rstd::fs::hard_link(source.as_path(), linked.as_path()).is_ok());
    auto store   = FrontendSourceStore::make();
    auto domains = HeaderDomains {
        .first         = source.clone(),
        .first_domain  = String::make("target:first"_str),
        .second_domain = String::make("target:second"_str),
    };
    auto service = FrontendService::with_store(store,
                                               None(),
                                               Some(FrontendHeaderClassifier {
                                                   .context  = rstd::addressof(domains),
                                                   .classify = classify_distinct_header_domains,
                                               }));

    ASSERT_TRUE(service.load(source.as_path(), SourceLoadRole::Include).is_ok());
    ASSERT_TRUE(service.load(linked.as_path(), SourceLoadRole::Include).is_ok());
    EXPECT_EQ(service.statistics().lex_builds, usize(1));

    store.release_domain(domains.first_domain.as_str());
    store.release_domain(domains.second_domain.as_str());

    EXPECT_EQ(store.statistics().live_payloads, usize(1));
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
                     return service.load(source.as_path(), SourceLoadRole::Include).is_ok();
                 }).unwrap();
    while (! gate.entered.load(Ordering::Acquire)) rstd::thread::yield_now();
    auto second = rstd::thread::spawn([service = FrontendService::with_store(store),
                                       source  = source.clone()]() mutable {
                      return service.load(source.as_path(), SourceLoadRole::Include).is_ok();
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

    EXPECT_TRUE(service.load(source.as_path(), SourceLoadRole::Include).is_err());
    ASSERT_TRUE(rstd::fs::remove_dir(source.as_path()).is_ok());
    ASSERT_TRUE(rstd::fs::write(source.as_path(), "int value = 1;\n"_str.as_bytes()).is_ok());
    EXPECT_TRUE(service.load(source.as_path(), SourceLoadRole::Include).is_ok());
    EXPECT_EQ(store.statistics().in_flight_entries, usize {});
}

TEST(FrontendSourceStore, DoesNotCachePrimarySources) {
    auto temporary = rstd::test::TempDir::make();
    ASSERT_TRUE(temporary.is_ok());
    auto owner   = rstd::move(temporary).unwrap();
    auto source  = source_fixture(owner.path());
    auto store   = FrontendSourceStore::make();
    auto service = FrontendService::with_store(store);

    auto first  = service.load(source.as_path(), SourceLoadRole::Primary);
    auto second = service.load(source.as_path(), SourceLoadRole::Primary);

    ASSERT_TRUE(first.is_ok());
    ASSERT_TRUE(second.is_ok());
    EXPECT_EQ(service.statistics().lex_builds, usize(2));
    EXPECT_EQ(service.statistics().source_hits, usize {});
    EXPECT_EQ(store.statistics().ready_entries, usize {});
    EXPECT_EQ(store.statistics().in_flight_entries, usize {});
}
