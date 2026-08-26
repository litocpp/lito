export module lito.frontend.memory;

import rstd;

using namespace rstd::prelude;

export namespace lito::frontend
{

using ScanMemoryAllocator = rstd::alloc::SharedArenaAllocator<rstd::alloc::VirtualMemoryAllocator>;

struct ScanMemoryDomainStatistics {
    usize used_bytes {};
    usize used_bytes_peak {};
    usize reserved_bytes {};
    usize padding_bytes {};
    usize ordinary_blocks {};
    usize large_blocks {};
    usize mapped_bytes {};
    usize mapped_bytes_peak {};
    usize mappings {};
    usize mappings_peak {};
};

class ScanMemoryDomainObserver {
    using Weak = rstd::alloc::WeakSharedArena<rstd::alloc::VirtualMemoryAllocator>;

    Weak weak_;

    explicit ScanMemoryDomainObserver(Weak weak): weak_(rstd::move(weak)) {}

    friend class ScanMemoryDomain;

public:
    ScanMemoryDomainObserver(const ScanMemoryDomainObserver&)                        = delete;
    auto operator=(const ScanMemoryDomainObserver&) -> ScanMemoryDomainObserver&     = delete;
    ScanMemoryDomainObserver(ScanMemoryDomainObserver&&) noexcept                    = default;
    auto operator=(ScanMemoryDomainObserver&&) noexcept -> ScanMemoryDomainObserver& = default;

    auto expired() const noexcept -> bool { return weak_.expired(); }
};

class ScanMemoryDomain {
    static constexpr usize DEFAULT_BLOCK_SIZE = usize(4 * 1024 * 1024);
    using Arena = rstd::alloc::SharedBumpArena<rstd::alloc::VirtualMemoryAllocator>;

    Arena arena_;

    explicit ScanMemoryDomain(Arena arena): arena_(rstd::move(arena)) {}

public:
    ScanMemoryDomain(const ScanMemoryDomain&)                        = delete;
    auto operator=(const ScanMemoryDomain&) -> ScanMemoryDomain&     = delete;
    ScanMemoryDomain(ScanMemoryDomain&&) noexcept                    = default;
    auto operator=(ScanMemoryDomain&&) noexcept -> ScanMemoryDomain& = default;

    static auto make(usize block_size = DEFAULT_BLOCK_SIZE) -> ScanMemoryDomain {
        return ScanMemoryDomain(Arena::make(block_size));
    }

    auto allocator() const -> ScanMemoryAllocator { return arena_.allocator(); }

    auto downgrade() const -> ScanMemoryDomainObserver {
        return ScanMemoryDomainObserver(arena_.downgrade());
    }

    auto statistics() const -> ScanMemoryDomainStatistics {
        auto statistics = arena_.statistics();
        return ScanMemoryDomainStatistics {
            .used_bytes        = statistics.arena.used_bytes,
            .used_bytes_peak   = statistics.arena.peak_used_bytes,
            .reserved_bytes    = statistics.arena.reserved_bytes,
            .padding_bytes     = statistics.arena.padding_bytes,
            .ordinary_blocks   = statistics.arena.ordinary_slabs,
            .large_blocks      = statistics.arena.large_slabs,
            .mapped_bytes      = statistics.upstream.mapped_bytes,
            .mapped_bytes_peak = statistics.upstream.mapped_bytes_peak,
            .mappings          = statistics.upstream.mappings,
            .mappings_peak     = statistics.upstream.mappings_peak,
        };
    }
};

} // namespace lito::frontend
