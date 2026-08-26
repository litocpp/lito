export module lito.frontend.memory;

import rstd;

using namespace rstd::prelude;

export namespace lito::frontend
{

using ScanMemoryAllocator = rstd::alloc::SharedArenaAllocator<rstd::alloc::VirtualMemoryAllocator>;
using FrontendScratchAllocatorStorage =
    rstd::alloc::RecyclingArenaAllocator<rstd::alloc::VirtualMemoryAllocator>;
using FrontendScratchAllocator = ref<dyn<rstd::alloc::Allocator>>;

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

struct FrontendScratchStatistics {
    usize live_bytes {};
    usize live_bytes_peak {};
    usize reserved_bytes {};
    usize free_bytes {};
    usize reused_bytes {};
    usize ordinary_blocks {};
    usize large_blocks {};
    usize allocations {};
    usize reuses {};
    usize layout_classes {};
    usize recycled_capacity {};
    usize metadata_used_bytes {};
    usize metadata_reserved_bytes {};
    usize metadata_blocks {};
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

class FrontendScratchDomain {
    static constexpr usize DEFAULT_BLOCK_SIZE = usize(1024 * 1024);
    using Arena = rstd::alloc::RecyclingArena<rstd::alloc::VirtualMemoryAllocator>;

    Arena                           arena_;
    FrontendScratchAllocatorStorage allocator_;

    explicit FrontendScratchDomain(usize block_size)
        : arena_(block_size), allocator_(arena_.allocator()) {}

public:
    FrontendScratchDomain(const FrontendScratchDomain&)                    = delete;
    auto operator=(const FrontendScratchDomain&) -> FrontendScratchDomain& = delete;
    FrontendScratchDomain(FrontendScratchDomain&&)                         = delete;
    auto operator=(FrontendScratchDomain&&) -> FrontendScratchDomain&      = delete;

    static auto make(usize block_size = DEFAULT_BLOCK_SIZE) -> FrontendScratchDomain {
        return FrontendScratchDomain(block_size);
    }

    auto allocator() -> FrontendScratchAllocator { return rstd::alloc::allocator_ref(allocator_); }

    auto statistics() const -> FrontendScratchStatistics {
        auto arena    = arena_.stats();
        auto upstream = arena_.upstream_statistics();
        return FrontendScratchStatistics {
            .live_bytes              = arena.live_bytes,
            .live_bytes_peak         = arena.peak_live_bytes,
            .reserved_bytes          = arena.reserved_bytes,
            .free_bytes              = arena.free_bytes,
            .reused_bytes            = arena.reused_bytes,
            .ordinary_blocks         = arena.ordinary_slabs,
            .large_blocks            = arena.large_slabs,
            .allocations             = arena.allocations,
            .reuses                  = arena.reuses,
            .layout_classes          = arena.layout_classes,
            .recycled_capacity       = arena.recycled_capacity,
            .metadata_used_bytes     = arena.metadata_used_bytes,
            .metadata_reserved_bytes = arena.metadata_reserved_bytes,
            .metadata_blocks         = arena.metadata_blocks,
            .mapped_bytes            = upstream.mapped_bytes,
            .mapped_bytes_peak       = upstream.mapped_bytes_peak,
            .mappings                = upstream.mappings,
            .mappings_peak           = upstream.mappings_peak,
        };
    }
};

} // namespace lito::frontend
